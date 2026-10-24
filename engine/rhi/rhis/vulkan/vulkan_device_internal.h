#pragma once

// VulkanDevice: the Vulkan implementation of Rhi::Device, shared by the backend's .cpp files
// (swapchain, command list). Internal to rhi/rhis/vulkan/; vulkan_device.h is the public entry.

#include "core/containers/slot_map.h"
#include "rhi/device.h"
#include "rhi/rhis/vulkan/vulkan_command_list.h"
#include "rhi/rhis/vulkan/vulkan_common.h"
#include "rhi/rhis/vulkan/vulkan_instance.h"

#include <array>
#include <memory>
#include <vector>

namespace Lmao::Rhi::Vulkan
{

    class VulkanSwapchain;

    // A texture as the backend sees it. Layout is the layout the last recorded command left
    // the image in; the command list uses it to insert transitions.
    struct Texture
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkExtent2D Extent{};
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct Pipeline
    {
        VkPipeline Handle = VK_NULL_HANDLE;
    };

    class VulkanDevice final : public Device
    {
    public:
        VulkanDevice(std::unique_ptr<Instance> instance, VkPhysicalDevice physical, VkDevice device,
                     uint32_t queue_family, AdapterInfo info);
        ~VulkanDevice() override;

        // Rhi::Device
        Backend GetBackend() const override { return Backend::Vulkan; }
        const AdapterInfo &GetAdapterInfo() const override { return mAdapterInfo; }
        void WaitIdle() override;
        std::unique_ptr<Swapchain> CreateSwapchain(const SwapchainDesc &desc) override;
        PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
        void DestroyPipeline(PipelineHandle pipeline) override;
        CommandList *BeginFrame(Swapchain &swapchain) override;
        void EndFrame() override;

        // Backend internals
        VkDevice GetHandle() const { return mDevice; }
        VkPhysicalDevice GetPhysicalDevice() const { return mPhysicalDevice; }
        const Instance &GetInstance() const { return *mInstance; }
        uint32_t GetQueueFamily() const { return mGraphicsQueueFamily; }
        VkQueue GetQueue() const { return mGraphicsQueue; }

        TextureHandle RegisterTexture(const Texture &texture) { return mTextures.Insert(texture); }
        void UnregisterTexture(TextureHandle handle) { mTextures.Remove(handle); }
        Texture &GetTexture(TextureHandle handle) { return mTextures[handle]; }
        Pipeline &GetPipeline(PipelineHandle handle) { return mPipelines[handle]; }

        // Shared by every pipeline: the 128-byte push constant range of the binding convention.
        // Descriptor sets (set0 bindless, set1 per-frame) are added with bindless in M2.
        VkPipelineLayout GetPipelineLayout() const { return mPipelineLayout; }

        // Called once by CreateDevice. false on failure (the reason is logged).
        bool Initialize();

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
            VkSemaphore image_acquired = VK_NULL_HANDLE; // binary: signaled by vkAcquireNextImageKHR
            uint64_t timeline_value = 0;
        };

        // A Vulkan object whose handle is gone but which frames up to `frame` may still use.
        struct PendingDestroy
        {
            uint64_t frame;
            VkPipeline pipeline;
        };

        void destroy_frame_resources();
        void destroy_pending(bool everything); // everything: only after WaitIdle

        std::unique_ptr<Instance> mInstance; // declared first: destroyed after the device
        VkPhysicalDevice mPhysicalDevice;
        VkDevice mDevice;
        uint32_t mGraphicsQueueFamily;
        VkQueue mGraphicsQueue = VK_NULL_HANDLE;
        AdapterInfo mAdapterInfo;

        std::array<Frame, MaxFramesInFlight> mFrames{};
        uint32_t mFrameIndex = 0;
        VkSemaphore mFrameTimeline = VK_NULL_HANDLE; // counts submitted frames
        uint64_t mSubmittedFrames = 0;
        VulkanSwapchain *mFrameSwapchain = nullptr; // set between BeginFrame and EndFrame
        VulkanCommandList mCommandList;

        SlotMap<Texture, TextureTag> mTextures;
        SlotMap<Pipeline, PipelineTag> mPipelines;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        std::vector<PendingDestroy> mPendingDestroys;
        int mSwapchainCount = 0;
    };

} // namespace Lmao::Rhi::Vulkan
