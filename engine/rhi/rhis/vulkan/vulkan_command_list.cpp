#include "rhi/rhis/vulkan/vulkan_command_list.h"

#include "core/assert/assert.h"
#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include <algorithm>
#include <cstring>
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
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
                return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
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
        mIndexBufferBound = false;
        const VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        Check(vkBeginCommandBuffer(mCommandBuffer, &begin_info), "vkBeginCommandBuffer");

        // Sets 0 and 1 stay bound for the whole command buffer: every pipeline shares the layout.
        const VkDescriptorSet sets[] = {mDevice.GetBindlessSet(), mDevice.GetFrameSet()};
        for (VkPipelineBindPoint bind_point : {VK_PIPELINE_BIND_POINT_GRAPHICS, VK_PIPELINE_BIND_POINT_COMPUTE})
            vkCmdBindDescriptorSets(mCommandBuffer, bind_point, mDevice.GetPipelineLayout(), 0,
                                    static_cast<uint32_t>(std::size(sets)), sets, 0, nullptr);
    }

    void VulkanCommandList::End()
    {
        LMAO_ASSERT(!mRendering, "EndRendering was not called");
        LMAO_ASSERT(mMarkerDepth == 0, "{} BeginMarker call(s) without EndMarker", mMarkerDepth);
        Check(vkEndCommandBuffer(mCommandBuffer), "vkEndCommandBuffer");
        mCommandBuffer = VK_NULL_HANDLE;
    }

    void VulkanCommandList::BeginRendering(const RenderingDesc &desc)
    {
        const std::span<const ColorAttachment> color_attachments = desc.ColorAttachments;
        LMAO_ASSERT(mCommandBuffer != VK_NULL_HANDLE, "command list is not recording");
        LMAO_ASSERT(!mRendering, "BeginRendering called twice without EndRendering");
        const bool has_depth = desc.Depth.Texture.IsValid();
        LMAO_ASSERT(!color_attachments.empty() || has_depth, "BeginRendering needs a color or depth attachment");

        // Declared inputs: whatever wrote them before (a render pass, an upload) is finished and
        // they are in the layout their bindless descriptor promises.
        for (TextureHandle handle : desc.SampledTextures)
        {
            Texture &texture = mDevice.GetTexture(handle);
            LMAO_ASSERT(texture.Usage & VK_IMAGE_USAGE_SAMPLED_BIT, "BeginRendering: sampled texture (index {}) has no "
                                                                    "Sampled usage", handle.Index);
            for (const ColorAttachment &attachment : color_attachments)
                LMAO_ASSERT(attachment.Texture != handle, "BeginRendering: texture (index {}) is both sampled and "
                                                          "rendered to", handle.Index);
            LMAO_ASSERT(desc.Depth.Texture != handle, "BeginRendering: texture (index {}) is both sampled and the "
                                                      "depth attachment", handle.Index);
            if (texture.Layout != VK_IMAGE_LAYOUT_GENERAL)
                transition(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        std::vector<VkRenderingAttachmentInfo> attachments;
        attachments.reserve(color_attachments.size());
        VkExtent2D area{UINT32_MAX, UINT32_MAX};
        for (const ColorAttachment &attachment : color_attachments)
        {
            Texture &texture = mDevice.GetTexture(attachment.Texture);
            LMAO_ASSERT(texture.Aspect == VK_IMAGE_ASPECT_COLOR_BIT, "depth textures can't be color attachments");
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

        VkRenderingAttachmentInfo depth_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (has_depth)
        {
            Texture &texture = mDevice.GetTexture(desc.Depth.Texture);
            LMAO_ASSERT(texture.Aspect == VK_IMAGE_ASPECT_DEPTH_BIT, "BeginRendering: the depth attachment isn't a "
                                                                     "depth texture");
            if (desc.Depth.Load != LoadOp::Load)
                texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
            transition(texture, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            depth_attachment.imageView = texture.View;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = to_vk(desc.Depth.Load);
            depth_attachment.storeOp = to_vk(desc.Depth.Store);
            depth_attachment.clearValue = {.depthStencil = {.depth = desc.Depth.ClearDepth, .stencil = 0}};
            area.width = std::min(area.width, texture.Extent.width);
            area.height = std::min(area.height, texture.Extent.height);
        }

        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {0, 0}, .extent = area},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
            .pColorAttachments = attachments.data(),
            .pDepthAttachment = has_depth ? &depth_attachment : nullptr,
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

    void VulkanCommandList::BindIndexBuffer(BufferHandle handle, IndexType type, uint64_t offset)
    {
        const Buffer &buffer = mDevice.GetBuffer(handle);
        LMAO_ASSERT(buffer.Usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "BindIndexBuffer: the buffer has no Index usage");
        LMAO_ASSERT(offset < buffer.Size, "BindIndexBuffer: offset {} is past the end of a {}-byte buffer", offset,
                    buffer.Size);
        vkCmdBindIndexBuffer(mCommandBuffer, buffer.Handle, offset,
                             type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        mIndexBufferBound = true;
    }

    void VulkanCommandList::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                        int32_t vertex_offset, uint32_t first_instance)
    {
        LMAO_ASSERT(mRendering, "DrawIndexed outside BeginRendering/EndRendering");
        LMAO_ASSERT(mPipelineBound, "DrawIndexed without a bound pipeline");
        LMAO_ASSERT(mIndexBufferBound, "DrawIndexed without a bound index buffer");
        vkCmdDrawIndexed(mCommandBuffer, index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void VulkanCommandList::PushConstants(std::span<const std::byte> data, uint32_t offset)
    {
        LMAO_ASSERT(offset % 4 == 0 && data.size() % 4 == 0, "PushConstants: offset and size must be multiples of 4");
        LMAO_ASSERT(offset + data.size() <= MaxPushConstantsSize, "PushConstants: {} bytes at offset {} exceed {} bytes",
                    data.size(), offset, MaxPushConstantsSize);
        if (data.empty() || offset + data.size() > MaxPushConstantsSize)
            return;
        vkCmdPushConstants(mCommandBuffer, mDevice.GetPipelineLayout(), VK_SHADER_STAGE_ALL, offset,
                           static_cast<uint32_t>(data.size()), data.data());
    }

    void VulkanCommandList::SetFrameConstants(std::span<const std::byte> data)
    {
        LMAO_ASSERT(data.size() <= MaxFrameConstantsSize, "SetFrameConstants: {} bytes exceed {} bytes", data.size(),
                    MaxFrameConstantsSize);
        // This frame slot's buffer: the GPU finished the previous frame that used it (BeginFrame).
        std::memcpy(mDevice.GetFrameConstantsData(), data.data(), std::min<size_t>(data.size(), MaxFrameConstantsSize));
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
        image_barrier(texture, 0, texture.MipLevels, texture.Layout, new_layout);
        texture.Layout = new_layout;
    }

    void VulkanCommandList::image_barrier(const Texture &texture, uint32_t base_mip, uint32_t mip_count,
                                          VkImageLayout old_layout, VkImageLayout new_layout)
    {
        const LayoutSync src = layout_sync(old_layout);
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
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture.Image,
            .subresourceRange = {.aspectMask = texture.Aspect, .baseMipLevel = base_mip, .levelCount = mip_count,
                                 .layerCount = 1},
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(mCommandBuffer, &dependency);
    }

    void VulkanCommandList::barrier_before_transfer_write()
    {
        const VkMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(mCommandBuffer, &dependency);
    }

    void VulkanCommandList::barrier_after_transfer_write()
    {
        // HOST: Readback buffers are read by the CPU once the frame is done.
        const VkMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT,
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(mCommandBuffer, &dependency);
    }

    bool VulkanCommandList::check_transfer_allowed(const char *what) const
    {
        LMAO_ASSERT(mCommandBuffer != VK_NULL_HANDLE, "{}: command list is not recording", what);
        LMAO_ASSERT(!mRendering, "{} inside BeginRendering/EndRendering", what);
        return mCommandBuffer != VK_NULL_HANDLE && !mRendering;
    }

    void VulkanCommandList::UploadBuffer(BufferHandle handle, std::span<const std::byte> data, uint64_t offset)
    {
        if (!check_transfer_allowed("UploadBuffer") || data.empty())
            return;
        const Buffer &buffer = mDevice.GetBuffer(handle);
        LMAO_ASSERT(buffer.Usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT, "UploadBuffer: the buffer has no CopyDest usage");
        LMAO_ASSERT(offset + data.size() <= buffer.Size, "UploadBuffer: {} bytes at offset {} overflow a {}-byte buffer",
                    data.size(), offset, buffer.Size);

        const StagingSpace staging = mDevice.AllocateStaging(data.size());
        if (!staging.Data)
            return;
        std::memcpy(staging.Data, data.data(), data.size());

        const VkBufferCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = staging.Offset,
            .dstOffset = offset,
            .size = data.size(),
        };
        const VkCopyBufferInfo2 copy{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = staging.Buffer,
            .dstBuffer = buffer.Handle,
            .regionCount = 1,
            .pRegions = &region,
        };
        barrier_before_transfer_write();
        vkCmdCopyBuffer2(mCommandBuffer, &copy);
        barrier_after_transfer_write();
    }

    void VulkanCommandList::UploadTexture(TextureHandle handle, uint32_t mip, std::span<const std::byte> data)
    {
        if (!check_transfer_allowed("UploadTexture"))
            return;
        Texture &texture = mDevice.GetTexture(handle);
        LMAO_ASSERT(texture.Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT, "UploadTexture: the texture has no CopyDest usage");
        LMAO_ASSERT(texture.Aspect == VK_IMAGE_ASPECT_COLOR_BIT, "UploadTexture: depth textures can't be uploaded");
        LMAO_ASSERT(mip < texture.MipLevels, "UploadTexture: mip {} of a texture with {} mips", mip, texture.MipLevels);
        if (mip >= texture.MipLevels)
            return;
        const uint64_t expected = MipBytes(texture, mip);
        LMAO_ASSERT(data.size() == expected, "UploadTexture: mip {} needs {} bytes, got {}", mip, expected, data.size());
        if (data.size() != expected)
            return;

        UploadTextureRows(handle, mip, 0, MipExtent(texture, mip).height, data, true);
    }

    void VulkanCommandList::UploadTextureRows(TextureHandle handle, uint32_t mip, uint32_t first_row,
                                              uint32_t row_count, std::span<const std::byte> data, bool last)
    {
        Texture &texture = mDevice.GetTexture(handle);
        const VkExtent2D extent = MipExtent(texture, mip);
        LMAO_ASSERT(first_row + row_count <= extent.height && data.size() == MipRowBytes(texture, mip) * row_count,
                    "UploadTextureRows: rows {}..{} of a {}-row mip with {} bytes", first_row, first_row + row_count,
                    extent.height, data.size());

        const StagingSpace staging = mDevice.AllocateStaging(data.size());
        if (!staging.Data)
            return;
        std::memcpy(staging.Data, data.data(), data.size());

        transition(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
            .bufferOffset = staging.Offset,
            .imageSubresource = {.aspectMask = texture.Aspect, .mipLevel = mip, .layerCount = 1},
            .imageOffset = {0, static_cast<int32_t>(first_row), 0},
            .imageExtent = {extent.width, row_count, 1},
        };
        const VkCopyBufferToImageInfo2 copy{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
            .srcBuffer = staging.Buffer,
            .dstImage = texture.Image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &region,
        };
        vkCmdCopyBufferToImage2(mCommandBuffer, &copy);

        if (last && (texture.Usage & VK_IMAGE_USAGE_SAMPLED_BIT))
            transition(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void VulkanCommandList::GenerateMips(TextureHandle handle)
    {
        if (!check_transfer_allowed("GenerateMips"))
            return;
        Texture &texture = mDevice.GetTexture(handle);
        if (texture.MipLevels <= 1)
            return;
        constexpr VkImageUsageFlags needed_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        LMAO_ASSERT((texture.Usage & needed_usage) == needed_usage, "GenerateMips: needs CopySource | CopyDest usage");
        if ((texture.Usage & needed_usage) != needed_usage)
            return;

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(mDevice.GetPhysicalDevice(), texture.Format, &properties);
        constexpr VkFormatFeatureFlags needed_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((properties.optimalTilingFeatures & needed_features) != needed_features)
        {
            LMAO_LOG_ERROR(LogChannel(), "GenerateMips: format {} doesn't support linear blits on this GPU",
                           ToString(FromVkFormat(texture.Format)));
            return;
        }

        // Mip 0 keeps its contents; every mip starts in TRANSFER_DST. Each step turns the
        // previous mip into a blit source and writes the next one from it.
        transition(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (uint32_t mip = 1; mip < texture.MipLevels; ++mip)
        {
            image_barrier(texture, mip - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            const VkExtent2D source = MipExtent(texture, mip - 1);
            const VkExtent2D destination = MipExtent(texture, mip);
            const VkImageBlit2 blit{
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .srcSubresource = {.aspectMask = texture.Aspect, .mipLevel = mip - 1, .layerCount = 1},
                .srcOffsets = {{0, 0, 0}, {static_cast<int32_t>(source.width), static_cast<int32_t>(source.height), 1}},
                .dstSubresource = {.aspectMask = texture.Aspect, .mipLevel = mip, .layerCount = 1},
                .dstOffsets = {{0, 0, 0},
                               {static_cast<int32_t>(destination.width), static_cast<int32_t>(destination.height), 1}},
            };
            const VkBlitImageInfo2 blit_info{
                .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                .srcImage = texture.Image,
                .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .dstImage = texture.Image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1,
                .pRegions = &blit,
                .filter = VK_FILTER_LINEAR,
            };
            vkCmdBlitImage2(mCommandBuffer, &blit_info);
        }

        // Now mips 0..N-2 are TRANSFER_SRC and the last is TRANSFER_DST: bring them to one layout.
        const VkImageLayout final_layout = (texture.Usage & VK_IMAGE_USAGE_SAMPLED_BIT)
                                               ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                               : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const uint32_t last = texture.MipLevels - 1;
        if (final_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            image_barrier(texture, 0, last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, final_layout);
        image_barrier(texture, last, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout);
        texture.Layout = final_layout;
    }

    void VulkanCommandList::CopyBuffer(BufferHandle source_handle, uint64_t source_offset, BufferHandle destination_handle,
                                       uint64_t destination_offset, uint64_t size)
    {
        if (!check_transfer_allowed("CopyBuffer") || size == 0)
            return;
        const Buffer &source = mDevice.GetBuffer(source_handle);
        const Buffer &destination = mDevice.GetBuffer(destination_handle);
        LMAO_ASSERT(source.Usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "CopyBuffer: the source has no CopySource usage");
        LMAO_ASSERT(destination.Usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT, "CopyBuffer: the destination has no CopyDest usage");
        LMAO_ASSERT(source_offset + size <= source.Size && destination_offset + size <= destination.Size,
                    "CopyBuffer: {} bytes overflow the source or destination", size);

        const VkBufferCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = source_offset,
            .dstOffset = destination_offset,
            .size = size,
        };
        const VkCopyBufferInfo2 copy{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = source.Handle,
            .dstBuffer = destination.Handle,
            .regionCount = 1,
            .pRegions = &region,
        };
        barrier_before_transfer_write();
        vkCmdCopyBuffer2(mCommandBuffer, &copy);
        barrier_after_transfer_write();
    }

    void VulkanCommandList::CopyTextureToBuffer(TextureHandle texture_handle, uint32_t mip, BufferHandle buffer_handle,
                                                uint64_t offset)
    {
        if (!check_transfer_allowed("CopyTextureToBuffer"))
            return;
        Texture &texture = mDevice.GetTexture(texture_handle);
        const Buffer &buffer = mDevice.GetBuffer(buffer_handle);
        LMAO_ASSERT(texture.Usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "CopyTextureToBuffer: the texture has no CopySource usage");
        LMAO_ASSERT(buffer.Usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT, "CopyTextureToBuffer: the buffer has no CopyDest usage");
        LMAO_ASSERT(mip < texture.MipLevels, "CopyTextureToBuffer: mip {} of a texture with {} mips", mip, texture.MipLevels);
        LMAO_ASSERT(offset % 16 == 0, "CopyTextureToBuffer: the buffer offset must be a multiple of 16");
        LMAO_ASSERT(offset + MipBytes(texture, mip) <= buffer.Size, "CopyTextureToBuffer: mip {} doesn't fit the buffer", mip);
        if (mip >= texture.MipLevels)
            return;

        transition(texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkExtent2D extent = MipExtent(texture, mip);
        const VkBufferImageCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
            .bufferOffset = offset,
            .imageSubresource = {.aspectMask = texture.Aspect, .mipLevel = mip, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1},
        };
        const VkCopyImageToBufferInfo2 copy{
            .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2,
            .srcImage = texture.Image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstBuffer = buffer.Handle,
            .regionCount = 1,
            .pRegions = &region,
        };
        barrier_before_transfer_write();
        vkCmdCopyImageToBuffer2(mCommandBuffer, &copy);
        barrier_after_transfer_write();
    }

} // namespace Lmao::Rhi::Vulkan
