#pragma once

// Vulkan implementation of Rhi::CommandList. Internal to rhi/rhis/vulkan/.

#include "rhi/command_list.h"
#include "rhi/rhis/vulkan/vulkan_common.h"

namespace Lmao::Rhi::Vulkan
{

    class VulkanDevice;
    struct Texture;

    class VulkanCommandList final : public CommandList
    {
    public:
        explicit VulkanCommandList(VulkanDevice &device) : mDevice(device) {}

        using CommandList::BeginRendering; // keep the single-attachment overload visible
        void BeginRendering(std::span<const ColorAttachment> color_attachments) override;
        void EndRendering() override;
        void BeginMarker(std::string_view name) override;
        void EndMarker() override;

        // Backend internals
        void Begin(VkCommandBuffer command_buffer); // starts recording into command_buffer
        void End();
        void TransitionForPresent(TextureHandle texture);

    private:
        void transition(Texture &texture, VkImageLayout new_layout);

        VulkanDevice &mDevice;
        VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
        bool mRendering = false;
        int mMarkerDepth = 0;
    };

} // namespace Lmao::Rhi::Vulkan
