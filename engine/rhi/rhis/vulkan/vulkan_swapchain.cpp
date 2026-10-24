#include "rhi/rhis/vulkan/vulkan_swapchain.h"

#include "core/assert/assert.h"
#include "core/profile/profile.h"
#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include <algorithm>
#include <string>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        const char *present_mode_name(VkPresentModeKHR mode)
        {
            switch (mode)
            {
            case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
            case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
            case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
            default: return "other";
            }
        }

        // FIFO is the only mode every implementation must support.
        VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical, VkSurfaceKHR surface, bool vsync)
        {
            if (vsync)
                return VK_PRESENT_MODE_FIFO_KHR;

            uint32_t count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
            std::vector<VkPresentModeKHR> modes(count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
            for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
                if (std::find(modes.begin(), modes.end(), preferred) != modes.end())
                    return preferred;
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported)
        {
            for (VkCompositeAlphaFlagBitsKHR alpha :
                 {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
                if (supported & alpha)
                    return alpha;
            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }

    } // namespace

    std::unique_ptr<VulkanSwapchain> VulkanSwapchain::Create(VulkanDevice &device, const SwapchainDesc &desc)
    {
        LMAO_PROFILE_ZONE();

        std::unique_ptr<Surface> surface = Surface::Create(device.GetInstance(), desc.Window);
        if (!surface)
            return nullptr;

        // Every GPU we know of can present from its graphics queue; a separate present queue
        // isn't worth the complexity until one can't.
        if (!surface->SupportsPresent(device.GetPhysicalDevice(), device.GetQueueFamily()))
        {
            LMAO_LOG_ERROR(LogChannel(), "The graphics queue (family {}) cannot present to this window",
                           device.GetQueueFamily());
            return nullptr;
        }

        auto swapchain = std::unique_ptr<VulkanSwapchain>(new VulkanSwapchain(device, std::move(surface), desc));
        if (!swapchain->choose_format())
            return nullptr;
        // A minimized window has no size yet: the first BeginFrame with a real size creates it.
        if (swapchain->recreate() == RecreateResult::Failed)
            return nullptr;
        return swapchain;
    }

    VulkanSwapchain::VulkanSwapchain(VulkanDevice &device, std::unique_ptr<Surface> surface, const SwapchainDesc &desc)
        : mDevice(device), mSurface(std::move(surface)), mDesiredExtent{desc.Width, desc.Height}, mVSync(desc.VSync)
    {
        mDevice.OnSwapchainCreated();
        const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (uint32_t i = 0; i < mAcquireSemaphores.size(); ++i)
        {
            Check(vkCreateSemaphore(mDevice.GetHandle(), &semaphore_info, nullptr, &mAcquireSemaphores[i]),
                  "vkCreateSemaphore (acquire)");
            mDevice.SetName(VK_OBJECT_TYPE_SEMAPHORE, mAcquireSemaphores[i], fmt::format("Swapchain acquire (frame {})", i).c_str());
        }
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        mDevice.WaitIdle(); // images and semaphores may still be in use by queued frames
        destroy_images();
        if (mSwapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(mDevice.GetHandle(), mSwapchain, nullptr);
        for (VkSemaphore semaphore : mAcquireSemaphores)
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(mDevice.GetHandle(), semaphore, nullptr);
        mSurface.reset(); // after the swapchain that uses it
        mDevice.OnSwapchainDestroyed();
    }

    void VulkanSwapchain::Resize(uint32_t width, uint32_t height)
    {
        if (width == mDesiredExtent.width && height == mDesiredExtent.height && !mNeedsRecreate)
            return;
        mDesiredExtent = {width, height};
        mNeedsRecreate = true;
    }

    void VulkanSwapchain::SetVSync(bool vsync)
    {
        if (vsync == mVSync)
            return;
        mVSync = vsync;
        mNeedsRecreate = true;
    }

    TextureHandle VulkanSwapchain::GetCurrentTexture() const
    {
        LMAO_ASSERT(mCurrentImage < mImages.size(), "GetCurrentTexture outside BeginFrame/EndFrame");
        return mImages[mCurrentImage].texture;
    }

    VkSemaphore VulkanSwapchain::GetRenderFinishedSemaphore() const
    {
        LMAO_ASSERT(mCurrentImage < mImages.size());
        return mImages[mCurrentImage].render_finished;
    }

    bool VulkanSwapchain::choose_format()
    {
        VkPhysicalDevice physical = mDevice.GetPhysicalDevice();
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, mSurface->GetHandle(), &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, mSurface->GetHandle(), &count, formats.data());
        if (formats.empty())
        {
            LMAO_LOG_ERROR(LogChannel(), "The surface reports no formats");
            return false;
        }

        // sRGB first: shaders write linear color and the hardware encodes on store.
        for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM,
                                   VK_FORMAT_R8G8B8A8_UNORM})
        {
            for (const VkSurfaceFormatKHR &format : formats)
            {
                if (format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    mSurfaceFormat = format;
                    mFormat = FromVkFormat(format.format);
                    return true;
                }
            }
        }

        mSurfaceFormat = formats.front();
        mFormat = FromVkFormat(mSurfaceFormat.format);
        LMAO_LOG_WARN(LogChannel(), "No preferred swapchain format; using VkFormat {} (RHI format {})",
                      static_cast<int>(mSurfaceFormat.format), ToString(mFormat));
        return true;
    }

    VulkanSwapchain::RecreateResult VulkanSwapchain::recreate()
    {
        LMAO_PROFILE_ZONE();
        VkPhysicalDevice physical = mDevice.GetPhysicalDevice();
        VkSurfaceKHR surface = mSurface->GetHandle();

        VkSurfaceCapabilitiesKHR caps{};
        if (!Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
            return RecreateResult::Failed;

        // Win32/X11 dictate the size (currentExtent); Wayland leaves it to us (0xFFFFFFFF).
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            extent.width = std::clamp(mDesiredExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(mDesiredExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0)
            return RecreateResult::Minimized; // keep mNeedsRecreate and try again next frame

        // Simple and safe: nothing in flight may still reference the old images. A brand-new
        // swapchain has none, so creating one (e.g. a new window) doesn't stall.
        if (mSwapchain != VK_NULL_HANDLE)
            mDevice.WaitIdle();

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount != 0)
            image_count = std::min(image_count, caps.maxImageCount);

        mPresentMode = choose_present_mode(physical, surface, mVSync);

        VkSwapchainKHR old_swapchain = mSwapchain;
        const VkSwapchainCreateInfoKHR create_info{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = image_count,
            .imageFormat = mSurfaceFormat.format,
            .imageColorSpace = mSurfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT),
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = caps.currentTransform,
            .compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha),
            .presentMode = mPresentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = old_swapchain,
        };
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (!Check(vkCreateSwapchainKHR(mDevice.GetHandle(), &create_info, nullptr, &swapchain), "vkCreateSwapchainKHR"))
            return RecreateResult::Failed;

        destroy_images();
        if (old_swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(mDevice.GetHandle(), old_swapchain, nullptr);
        mSwapchain = swapchain;
        mExtent = extent;
        ++mGeneration;
        mDevice.SetName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, mSwapchain, fmt::format("Swapchain #{}", mGeneration).c_str());

        if (!create_images())
            return RecreateResult::Failed;

        mNeedsRecreate = false;
        LMAO_LOG_INFO(LogChannel(), "Swapchain {}x{}, {} images, {}, {}", extent.width, extent.height, mImages.size(),
                      ToString(mFormat), present_mode_name(mPresentMode));
        return RecreateResult::Ok;
    }

    bool VulkanSwapchain::create_images()
    {
        VkDevice device = mDevice.GetHandle();
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(device, mSwapchain, &count, nullptr);
        std::vector<VkImage> images(count);
        vkGetSwapchainImagesKHR(device, mSwapchain, &count, images.data());

        mImages.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            Image &image = mImages[i];
            image.image = images[i];

            const VkImageViewCreateInfo view_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = mSurfaceFormat.format,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
            };
            if (!Check(vkCreateImageView(device, &view_info, nullptr, &image.view), "vkCreateImageView"))
                return false;

            const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (!Check(vkCreateSemaphore(device, &semaphore_info, nullptr, &image.render_finished), "vkCreateSemaphore"))
                return false;

            image.texture = mDevice.RegisterTexture({
                .Image = image.image,
                .View = image.view,
                .Format = mSurfaceFormat.format,
                .Extent = mExtent,
                .Layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            });

            const std::string prefix = fmt::format("Swapchain #{} image {}", mGeneration, i);
            mDevice.SetName(VK_OBJECT_TYPE_IMAGE, image.image, prefix.c_str());
            mDevice.SetName(VK_OBJECT_TYPE_IMAGE_VIEW, image.view, (prefix + " view").c_str());
            mDevice.SetName(VK_OBJECT_TYPE_SEMAPHORE, image.render_finished, (prefix + " render finished").c_str());
        }
        return true;
    }

    void VulkanSwapchain::destroy_images()
    {
        VkDevice device = mDevice.GetHandle();
        for (Image &image : mImages)
        {
            if (image.texture)
                mDevice.UnregisterTexture(image.texture);
            if (image.view != VK_NULL_HANDLE)
                vkDestroyImageView(device, image.view, nullptr);
            if (image.render_finished != VK_NULL_HANDLE)
                vkDestroySemaphore(device, image.render_finished, nullptr);
        }
        mImages.clear(); // the VkImages belong to the swapchain
        mCurrentImage = UINT32_MAX;
    }

    bool VulkanSwapchain::Acquire(uint32_t frame_slot)
    {
        mAcquireSlot = frame_slot;
        const VkSemaphore acquired = mAcquireSemaphores[frame_slot];
        LMAO_PROFILE_ZONE();
        // One retry: an out-of-date swapchain is recreated and acquired again in the same frame.
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if ((mNeedsRecreate || mSwapchain == VK_NULL_HANDLE) && recreate() != RecreateResult::Ok)
                return false;

            const VkResult result =
                vkAcquireNextImageKHR(mDevice.GetHandle(), mSwapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &mCurrentImage);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                mNeedsRecreate = true; // `acquired` was not signaled; safe to reuse
                continue;
            }
            if (result == VK_SUBOPTIMAL_KHR)
            {
                mNeedsRecreate = true; // `acquired` is signaled: finish this frame, recreate next
                return true;
            }
            return Check(result, "vkAcquireNextImageKHR");
        }
        return false;
    }

    void VulkanSwapchain::Present(VkQueue queue)
    {
        LMAO_PROFILE_ZONE();
        LMAO_ASSERT(mCurrentImage < mImages.size(), "Present without a successful Acquire");

        const VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &mImages[mCurrentImage].render_finished,
            .swapchainCount = 1,
            .pSwapchains = &mSwapchain,
            .pImageIndices = &mCurrentImage,
        };
        const VkResult result = vkQueuePresentKHR(queue, &present_info);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            mNeedsRecreate = true;
        else
            Check(result, "vkQueuePresentKHR");
        mCurrentImage = UINT32_MAX;
    }

} // namespace Lmao::Rhi::Vulkan
