#pragma once

// VulkanDevice: the Vulkan implementation of Rhi::Device, shared by the backend's .cpp files
// (swapchain, command list). Internal to rhi/rhis/vulkan/; vulkan_device.h is the public entry.

#include "core/containers/slot_map.h"
#include "rhi/device.h"
#include "rhi/rhis/vulkan/vulkan_command_list.h"
#include "rhi/rhis/vulkan/vulkan_common.h"
#include "rhi/rhis/vulkan/vulkan_instance.h"

#include <vk_mem_alloc.h> // after volk (vulkan_common.h): VMA needs the Vulkan types

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Lmao::Rhi::Vulkan
{

    class VulkanSwapchain;

    // A texture as the backend sees it. Layout is the layout the last recorded command left
    // the image in (all mips); the command list uses it to insert transitions.
    struct Texture
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkExtent2D Extent{};
        uint32_t MipLevels = 1;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageUsageFlags Usage = 0;
        VmaAllocation Allocation = VK_NULL_HANDLE; // null for images owned by a swapchain
        uint32_t PendingUploads = 0; // queued operations not recorded yet (QueueUpload / QueueGenerateMips)
        bool Destroyed = false; // DestroyTexture was called; the slot is freed once the GPU is done
    };

    inline VkExtent2D MipExtent(const Texture &texture, uint32_t mip)
    {
        return {std::max(texture.Extent.width >> mip, 1u), std::max(texture.Extent.height >> mip, 1u)};
    }

    // Bytes of one tightly packed row / mip; 0 for formats without a fixed texel size.
    inline uint64_t MipRowBytes(const Texture &texture, uint32_t mip)
    {
        return uint64_t(MipExtent(texture, mip).width) * BytesPerTexel(FromVkFormat(texture.Format));
    }

    inline uint64_t MipBytes(const Texture &texture, uint32_t mip)
    {
        return MipRowBytes(texture, mip) * MipExtent(texture, mip).height;
    }

    struct Buffer
    {
        VkBuffer Handle = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        uint64_t Size = 0;
        VkBufferUsageFlags Usage = 0;
        void *Mapped = nullptr; // Upload and Readback buffers stay mapped
        uint32_t PendingUploads = 0;
        bool Destroyed = false;
    };

    // Mapped, host-coherent memory the CPU fills and a copy command reads.
    struct StagingSpace
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        uint64_t Offset = 0;
        std::byte *Data = nullptr;
    };

    struct Sampler
    {
        VkSampler Handle = VK_NULL_HANDLE;
        bool Destroyed = false;
    };

    // Set 0 array sizes: the engine's targets, clamped to the GPU's update-after-bind limits.
    struct BindlessCapacity
    {
        uint32_t SampledImages = 16384;
        uint32_t StorageImages = 4096;
        uint32_t StorageBuffers = 16384;
        uint32_t Samplers = 1024;
    };

    struct Pipeline
    {
        VkPipeline Handle = VK_NULL_HANDLE;
    };

    class VulkanDevice final : public Device
    {
    public:
        // Per frame in flight. Uploads that don't fit what's left get a one-off staging buffer.
        static constexpr uint64_t UploadRingSize = 32ull << 20;

        VulkanDevice(std::unique_ptr<Instance> instance, VkPhysicalDevice physical, VkDevice device,
                     uint32_t queue_family, AdapterInfo info);
        ~VulkanDevice() override;

        // Rhi::Device
        Backend GetBackend() const override { return Backend::Vulkan; }
        const AdapterInfo &GetAdapterInfo() const override { return mAdapterInfo; }
        void WaitIdle() override;
        std::unique_ptr<Swapchain> CreateSwapchain(const SwapchainDesc &desc) override;
        BufferHandle CreateBuffer(const BufferDesc &desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        void *GetMappedData(BufferHandle buffer) override;
        TextureHandle CreateTexture(const TextureDesc &desc) override;
        void DestroyTexture(TextureHandle texture) override;
        SamplerHandle CreateSampler(const SamplerDesc &desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        MemoryStats GetMemoryStats() const override;
        void QueueUpload(BufferHandle buffer, std::vector<std::byte> data, uint64_t offset) override;
        void QueueUpload(TextureHandle texture, uint32_t mip, std::vector<std::byte> data) override;
        void QueueGenerateMips(TextureHandle texture) override;
        bool IsUploadPending(BufferHandle buffer) const override;
        bool IsUploadPending(TextureHandle texture) const override;
        TextureIndex GetTextureIndex(TextureHandle texture, TextureHandle placeholder) const override;
        uint64_t GetPendingUploadBytes() const override { return mQueuedBytes; }
        PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
        void DestroyPipeline(PipelineHandle pipeline) override;
        bool ReplacePipeline(PipelineHandle pipeline, const GraphicsPipelineDesc &desc) override;
        using Device::BeginFrame;
        CommandList *BeginFrame(std::span<Swapchain *const> swapchains) override;
        void EndFrame() override;

        // Backend internals
        VkDevice GetHandle() const { return mDevice; }
        VkPhysicalDevice GetPhysicalDevice() const { return mPhysicalDevice; }
        const Instance &GetInstance() const { return *mInstance; }
        uint32_t GetQueueFamily() const { return mGraphicsQueueFamily; }
        VkQueue GetQueue() const { return mGraphicsQueue; }

        TextureHandle RegisterTexture(const Texture &texture) { return mTextures.Insert(texture); }
        void UnregisterTexture(TextureHandle handle) { mTextures.Remove(handle); }
        // Assert that the handle is live and wasn't destroyed.
        Texture &GetTexture(TextureHandle handle);
        Buffer &GetBuffer(BufferHandle handle);
        Pipeline &GetPipeline(PipelineHandle handle) { return mPipelines[handle]; }

        // Set 0 (bindless) and this frame's set 1, bound at the start of every command buffer.
        VkDescriptorSet GetBindlessSet() const { return mBindlessSet; }
        VkDescriptorSet GetFrameSet() const { return mFrames[mFrameIndex].frame_set; }
        // This frame's set 1 uniform buffer (MaxFrameConstantsSize bytes, mapped).
        std::byte *GetFrameConstantsData() const { return mFrames[mFrameIndex].constants_data; }
        VmaAllocator GetAllocator() const { return mAllocator; }

        // `size` bytes of staging memory for the frame being recorded, valid until the GPU has
        // finished that frame. Offsets are aligned for buffer-to-image copies.
        StagingSpace AllocateStaging(uint64_t size);

        // Shared by every pipeline: set 0 bindless, set 1 per frame, 128 bytes of push constants.
        VkPipelineLayout GetPipelineLayout() const { return mPipelineLayout; }

        // Called once by CreateDevice. false on failure (the reason is logged).
        // memory_budget: VK_EXT_memory_budget is enabled on the device.
        // pipeline_cache_directory: where the pipeline cache is loaded from and saved to; empty
        // keeps it in memory only.
        bool Initialize(bool memory_budget, uint64_t upload_budget_per_frame, std::string_view pipeline_cache_directory);

        void OnSwapchainCreated() { ++mSwapchainCount; }
        void OnSwapchainDestroyed() { --mSwapchainCount; }

        // Shows up in validation messages, RenderDoc and Nsight instead of a raw handle.
        template <typename T>
        void SetName(VkObjectType type, T handle, const char *name) const
        {
            if (!mInstance->HasDebugUtils())
                return;
            const VkDebugUtilsObjectNameInfoEXT info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = type,
                .objectHandle = reinterpret_cast<uint64_t>(handle),
                .pObjectName = name,
            };
            vkSetDebugUtilsObjectNameEXT(mDevice, &info);
        }

    private:
        // One slot per frame in flight. The slot is reused once the GPU has reached
        // timeline_value on mFrameTimeline.
        struct Frame
        {
            VkCommandPool command_pool = VK_NULL_HANDLE;
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            uint64_t timeline_value = 0;

            // Upload ring: bump-allocated while recording, reset when the slot is reused.
            VkBuffer upload_buffer = VK_NULL_HANDLE;
            VmaAllocation upload_allocation = VK_NULL_HANDLE;
            std::byte *upload_data = nullptr;
            uint64_t upload_used = 0;

            // Set 1: per-frame constants, written by CommandList::SetFrameConstants.
            VkBuffer constants_buffer = VK_NULL_HANDLE;
            VmaAllocation constants_allocation = VK_NULL_HANDLE;
            std::byte *constants_data = nullptr;
            VkDescriptorSet frame_set = VK_NULL_HANDLE;
        };

        // Vulkan objects whose RHI handle is gone but which frames up to `frame` may still use.
        // Any member may be null.
        struct PendingDestroy
        {
            uint64_t frame = 0;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkBuffer buffer = VK_NULL_HANDLE;
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            // Slots released together with the objects, so a descriptor index is never reused
            // while a frame in flight might still read the old descriptor.
            TextureHandle texture_slot;
            BufferHandle buffer_slot;
            SamplerHandle sampler_slot;
        };

        bool create_allocator(bool memory_budget);
        bool create_descriptors(const VkPhysicalDeviceVulkan12Properties &limits);
        bool create_frame_constants(Frame &frame, uint32_t index);
        // Write a resource's descriptors at its handle index. false if the index is past capacity.
        bool write_texture_descriptors(TextureHandle handle, const Texture &texture, const std::string &name);
        bool write_buffer_descriptor(BufferHandle handle, const Buffer &buffer, const std::string &name);
        bool write_sampler_descriptor(SamplerHandle handle, VkSampler sampler, const std::string &name);
        // Index 0 of every bindless array: a 1x1 magenta texture (also the storage-image fallback),
        // a zeroed buffer and a linear sampler. Every slot starts out (and ends up, when freed)
        // pointing at them, so no index ever reads an undefined descriptor.
        bool create_fallbacks();
        VkPipeline create_graphics_pipeline(const GraphicsPipelineDesc &desc); // VK_NULL_HANDLE on failure (logged)
        bool create_pipeline_cache(std::string_view directory, const VkPhysicalDeviceProperties &properties);
        void save_pipeline_cache(); // logs the hit count; writes the file if there is a path
        void point_slots_at_fallback(uint32_t binding, uint32_t first, uint32_t count);
        // Records and runs one command buffer outside the frame loop, waiting for it (startup only).
        bool submit_immediately(const std::function<void(VkCommandBuffer)> &record);
        // One QueueUpload / QueueGenerateMips call, recorded over one or more frames.
        struct QueuedUpload
        {
            enum class Kind : uint8_t
            {
                Buffer,
                Texture,
                GenerateMips
            };
            Kind kind = Kind::Buffer;
            BufferHandle buffer;
            TextureHandle texture;
            uint64_t offset = 0; // buffer: destination offset
            uint32_t mip = 0;    // texture
            std::vector<std::byte> data;
            uint64_t progress = 0; // buffer: bytes recorded; texture: rows recorded
        };

        // Records queued work into the frame being started, up to the per-frame budget.
        void drain_upload_queue();
        void finish_queued(const QueuedUpload &upload);
        // Drops queued work for a resource being destroyed.
        void drop_queued(BufferHandle buffer, TextureHandle texture);

        void destroy_frame_resources();
        // Queues `pending` until every frame that might use it (including the one being recorded) is done.
        void defer_destroy(PendingDestroy pending);
        void destroy_pending(bool everything); // everything: only after WaitIdle

        std::unique_ptr<Instance> mInstance; // declared first: destroyed after the device
        VkPhysicalDevice mPhysicalDevice;
        VkDevice mDevice;
        uint32_t mGraphicsQueueFamily;
        VkQueue mGraphicsQueue = VK_NULL_HANDLE;
        AdapterInfo mAdapterInfo;
        VkPhysicalDeviceLimits mLimits{};
        VmaAllocator mAllocator = VK_NULL_HANDLE;

        std::array<Frame, MaxFramesInFlight> mFrames{};
        uint32_t mFrameIndex = 0;
        VkSemaphore mFrameTimeline = VK_NULL_HANDLE; // counts submitted frames
        uint64_t mSubmittedFrames = 0;
        std::vector<VulkanSwapchain *> mFrameSwapchains; // acquired this frame, between BeginFrame and EndFrame
        bool mRecording = false;                         // between a successful BeginFrame and EndFrame
        VulkanCommandList mCommandList;

        SlotMap<Texture, TextureTag> mTextures;
        SlotMap<Pipeline, PipelineTag> mPipelines;
        SlotMap<Buffer, BufferTag> mBuffers;
        SlotMap<Sampler, SamplerTag> mSamplers;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipelineCache mPipelineCache = VK_NULL_HANDLE;
        std::filesystem::path mPipelineCachePath; // empty: not persisted
        // From VkPipelineCreationFeedback; pipelines the driver doesn't report on count as neither.
        std::atomic<uint32_t> mPipelineCacheHits = 0;
        std::atomic<uint32_t> mPipelineCacheMisses = 0;
        BindlessCapacity mBindlessCapacity;
        VkDescriptorSetLayout mBindlessLayout = VK_NULL_HANDLE; // set 0
        VkDescriptorSetLayout mFrameLayout = VK_NULL_HANDLE;    // set 1
        VkDescriptorPool mBindlessPool = VK_NULL_HANDLE;        // update-after-bind
        VkDescriptorPool mFramePool = VK_NULL_HANDLE;
        VkDescriptorSet mBindlessSet = VK_NULL_HANDLE;
        TextureHandle mFallbackTexture;
        BufferHandle mFallbackBuffer;
        SamplerHandle mFallbackSampler;
        bool mShuttingDown = false; // skip pointing freed slots at the (also freed) fallbacks
        std::vector<PendingDestroy> mPendingDestroys;
        std::deque<QueuedUpload> mUploadQueue;
        uint64_t mQueuedBytes = 0;
        uint64_t mUploadBudget = 16ull << 20;
        uint64_t mUploadBurstBytes = 0;  // since the queue was last empty, for the "done" log line
        uint32_t mUploadBurstFrames = 0;
        int mSwapchainCount = 0;
    };

} // namespace Lmao::Rhi::Vulkan
