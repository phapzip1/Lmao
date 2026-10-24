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
        void BeginRendering(const RenderingDesc &desc) override;
        void EndRendering() override;
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Rect &scissor) override;
        using CommandList::PushConstants;
        using CommandList::SetFrameConstants;
        void BindPipeline(PipelineHandle pipeline) override;
        void PushConstants(std::span<const std::byte> data, uint32_t offset) override;
        void SetFrameConstants(std::span<const std::byte> data) override;
        void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                  uint32_t first_instance) override;
        void BindIndexBuffer(BufferHandle buffer, IndexType type, uint64_t offset) override;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance) override;
        void UploadBuffer(BufferHandle buffer, std::span<const std::byte> data, uint64_t offset) override;
        void UploadTexture(TextureHandle texture, uint32_t mip, std::span<const std::byte> data) override;
        void GenerateMips(TextureHandle texture) override;
        void CopyBuffer(BufferHandle source, uint64_t source_offset, BufferHandle destination,
                        uint64_t destination_offset, uint64_t size) override;
        void CopyTextureToBuffer(TextureHandle texture, uint32_t mip, BufferHandle buffer, uint64_t offset) override;
        void BeginMarker(std::string_view name) override;
        void EndMarker() override;

        // Backend internals
        void Begin(VkCommandBuffer command_buffer); // starts recording into command_buffer
        void End();
        void TransitionForPresent(TextureHandle texture);

    private:
        // All mips of `texture` to `new_layout`; no-op if already there.
        void transition(Texture &texture, VkImageLayout new_layout);
        void image_barrier(const Texture &texture, uint32_t base_mip, uint32_t mip_count, VkImageLayout old_layout,
                           VkImageLayout new_layout);
        // Conservative barriers around transfers that write buffers, until the render graph (M5)
        // tracks individual resources: earlier work vs the copy, and the copy vs later work (and the host).
        void barrier_before_transfer_write();
        void barrier_after_transfer_write();
        bool check_transfer_allowed(const char *what) const;

        VulkanDevice &mDevice;
        VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
        bool mRendering = false;
        bool mPipelineBound = false;
        bool mIndexBufferBound = false;
        int mMarkerDepth = 0;
    };

} // namespace Lmao::Rhi::Vulkan
