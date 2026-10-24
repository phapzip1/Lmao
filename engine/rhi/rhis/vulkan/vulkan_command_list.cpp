#include "rhi/rhis/vulkan/vulkan_command_list.h"

#include "core/assert/assert.h"
#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        // What has to finish before / wait for a layout transition, per layout. Swapchain images
        // arrive in UNDEFINED or PRESENT_SRC after an acquire whose semaphore is waited at
        // COLOR_ATTACHMENT_OUTPUT, so those layouts use that stage to chain with the wait.
        struct LayoutSync
        {
            VkPipelineStageFlags2 stage;
            VkAccessFlags2 access;
        };

        LayoutSync layout_sync(VkImageLayout layout)
        {
            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE};
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
            default:
                return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
            }
        }

        VkAttachmentLoadOp to_vk(LoadOp op)
        {
            switch (op)
            {
            case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        VkAttachmentStoreOp to_vk(StoreOp op)
        {
            return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

    } // namespace

    void VulkanCommandList::Begin(VkCommandBuffer command_buffer)
    {
        LMAO_ASSERT(mCommandBuffer == VK_NULL_HANDLE, "command list is already recording");
        mCommandBuffer = command_buffer;
        mPipelineBound = false;
        const VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        Check(vkBeginCommandBuffer(mCommandBuffer, &begin_info), "vkBeginCommandBuffer");
    }

    void VulkanCommandList::End()
    {
        LMAO_ASSERT(!mRendering, "EndRendering was not called");
        LMAO_ASSERT(mMarkerDepth == 0, "{} BeginMarker call(s) without EndMarker", mMarkerDepth);
        Check(vkEndCommandBuffer(mCommandBuffer), "vkEndCommandBuffer");
        mCommandBuffer = VK_NULL_HANDLE;
    }

    void VulkanCommandList::BeginRendering(std::span<const ColorAttachment> color_attachments)
    {
        LMAO_ASSERT(mCommandBuffer != VK_NULL_HANDLE, "command list is not recording");
        LMAO_ASSERT(!mRendering, "BeginRendering called twice without EndRendering");
        LMAO_ASSERT(!color_attachments.empty(), "BeginRendering needs at least one attachment");

        std::vector<VkRenderingAttachmentInfo> attachments;
        attachments.reserve(color_attachments.size());
        VkExtent2D area{UINT32_MAX, UINT32_MAX};
        for (const ColorAttachment &attachment : color_attachments)
        {
            Texture &texture = mDevice.GetTexture(attachment.Texture);
            // Clear and DontCare overwrite everything: skip preserving the old contents.
            if (attachment.Load != LoadOp::Load)
                texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
            transition(texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            const Vec4 &c = attachment.ClearColor;
            attachments.push_back({
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = texture.View,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = to_vk(attachment.Load),
                .storeOp = to_vk(attachment.Store),
                .clearValue = {.color = {.float32 = {c.x, c.y, c.z, c.w}}},
            });
            area.width = std::min(area.width, texture.Extent.width);
            area.height = std::min(area.height, texture.Extent.height);
        }

        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {0, 0}, .extent = area},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
            .pColorAttachments = attachments.data(),
        };
        vkCmdBeginRendering(mCommandBuffer, &rendering_info);
        mRendering = true;
    }

    void VulkanCommandList::EndRendering()
    {
        LMAO_ASSERT(mRendering, "EndRendering without BeginRendering");
        vkCmdEndRendering(mCommandBuffer);
        mRendering = false;
    }

    void VulkanCommandList::SetViewport(const Viewport &viewport)
    {
        // Y flip: a negative height makes clip space Y-up like the other backends (engine convention).
        const VkViewport vk_viewport{
            .x = viewport.X,
            .y = viewport.Y + viewport.Height,
            .width = viewport.Width,
            .height = -viewport.Height,
            .minDepth = viewport.MinDepth,
            .maxDepth = viewport.MaxDepth,
        };
        vkCmdSetViewport(mCommandBuffer, 0, 1, &vk_viewport);
    }

    void VulkanCommandList::SetScissor(const Rect &scissor)
    {
        const VkRect2D rect{.offset = {scissor.X, scissor.Y}, .extent = {scissor.Width, scissor.Height}};
        vkCmdSetScissor(mCommandBuffer, 0, 1, &rect);
    }

    void VulkanCommandList::BindPipeline(PipelineHandle pipeline)
    {
        vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mDevice.GetPipeline(pipeline).Handle);
        mPipelineBound = true;
    }

    void VulkanCommandList::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                                 uint32_t first_instance)
    {
        LMAO_ASSERT(mRendering, "Draw outside BeginRendering/EndRendering");
        LMAO_ASSERT(mPipelineBound, "Draw without a bound pipeline");
        vkCmdDraw(mCommandBuffer, vertex_count, instance_count, first_vertex, first_instance);
    }

    void VulkanCommandList::BeginMarker(std::string_view name)
    {
        ++mMarkerDepth;
        if (!mDevice.GetInstance().HasDebugUtils())
            return;
        const std::string label_name(name);
        const VkDebugUtilsLabelEXT label{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = label_name.c_str(),
        };
        vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, &label);
    }

    void VulkanCommandList::EndMarker()
    {
        LMAO_ASSERT(mMarkerDepth > 0, "EndMarker without BeginMarker");
        --mMarkerDepth;
        if (mDevice.GetInstance().HasDebugUtils())
            vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
    }

    void VulkanCommandList::TransitionForPresent(TextureHandle texture)
    {
        transition(mDevice.GetTexture(texture), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    void VulkanCommandList::transition(Texture &texture, VkImageLayout new_layout)
    {
        if (texture.Layout == new_layout)
            return;

        const LayoutSync src = layout_sync(texture.Layout);
        // Presenting waits on the submit's semaphore signal, which covers all prior work.
        const LayoutSync dst = new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                   ? LayoutSync{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE}
                                   : layout_sync(new_layout);
        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src.stage,
            .srcAccessMask = src.access,
            .dstStageMask = dst.stage,
            .dstAccessMask = dst.access,
            .oldLayout = texture.Layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture.Image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(mCommandBuffer, &dependency);
        texture.Layout = new_layout;
    }

} // namespace Lmao::Rhi::Vulkan
