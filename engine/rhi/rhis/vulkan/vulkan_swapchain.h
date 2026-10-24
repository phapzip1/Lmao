#pragma once

// Vulkan implementation of Rhi::Swapchain. Internal to rhi/rhis/vulkan/.
//
// Synchronization (docs: M1 exit criteria):
// - The device owns one "image acquired" semaphore per frame in flight; Acquire signals it.
// - The swapchain owns one "render finished" semaphore per swapchain image; the frame's submit
//   signals it and Present waits on it. Per image, not per frame: a present semaphore may only
//   be reused once that image has been acquired again.

#include "rhi/rhis/vulkan/vulkan_common.h"
#include "rhi/rhis/vulkan/vulkan_surface.h"
#include "rhi/swapchain.h"

#include <memory>
#include <vector>

namespace Lmao::Rhi::Vulkan
{

    class VulkanDevice;

    class VulkanSwapchain final : public Swapchain
    {
    public:
        // nullptr on failure (the reason is logged).
        static std::unique_ptr<VulkanSwapchain> Create(VulkanDevice &device, const SwapchainDesc &desc);

        ~VulkanSwapchain() override;

        // Rhi::Swapchain
        void Resize(uint32_t width, uint32_t height) override;
        void SetVSync(bool vsync) override;
        UVec2 GetSize() const override { return UVec2(mExtent.width, mExtent.height); }
        Format GetFormat() const override { return mFormat; }
        uint32_t GetImageCount() const override { return static_cast<uint32_t>(mImages.size()); }
        TextureHandle GetCurrentTexture() const override;

        // Backend internals
        // Recreates the swapchain if needed, then acquires the next image, signaling `acquired`.
        // false: skip this frame (`acquired` is not signaled).
        bool Acquire(VkSemaphore acquired);
        // Semaphore the frame's submit must signal for the acquired image.
        VkSemaphore GetRenderFinishedSemaphore() const;
        void Present(VkQueue queue);

    private:
        struct Image
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSemaphore render_finished = VK_NULL_HANDLE;
            TextureHandle texture;
        };

        VulkanSwapchain(VulkanDevice &device, std::unique_ptr<Surface> surface, const SwapchainDesc &desc);

        enum class RecreateResult
        {
            Ok,
            Minimized, // the surface has no size: skip frames until it does
            Failed     // logged; retried next frame
        };

        bool choose_format();
        RecreateResult recreate();
        bool create_images();
        void destroy_images();

        VulkanDevice &mDevice;
        std::unique_ptr<Surface> mSurface;
        VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
        VkSurfaceFormatKHR mSurfaceFormat{};
        Format mFormat = Format::Unknown;
        VkPresentModeKHR mPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkExtent2D mExtent{};         // current swapchain size
        VkExtent2D mDesiredExtent{};  // from SwapchainDesc / Resize, used when the surface doesn't dictate one
        bool mVSync = true;
        bool mNeedsRecreate = true;
        std::vector<Image> mImages;
        uint32_t mCurrentImage = UINT32_MAX; // valid between Acquire and Present
        uint32_t mGeneration = 0;            // increments on every recreation, for debug names
    };

} // namespace Lmao::Rhi::Vulkan
