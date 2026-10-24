#pragma once

// VkSurfaceKHR for a platform window, created from the window's NativeWindowHandle.
// Internal to rhi/rhis/vulkan/. Window-system headers stay in vulkan_surface.cpp.

#include "core/platform/window.h"
#include "rhi/rhis/vulkan/vulkan_common.h"

#include <memory>

namespace Lmao::Rhi::Vulkan
{

    class Instance;

    class Surface
    {
    public:
        // nullptr on failure (the reason is logged), e.g. when the instance has no surface
        // extension for the window's type.
        static std::unique_ptr<Surface> Create(const Instance &instance, const NativeWindowHandle &window);

        ~Surface();

        Surface(const Surface &) = delete;
        Surface &operator=(const Surface &) = delete;

        VkSurfaceKHR GetHandle() const { return mSurface; }

        // Whether queues of this family on this GPU can present to the surface.
        bool SupportsPresent(VkPhysicalDevice physical_device, uint32_t queue_family) const;

    private:
        Surface(VkInstance instance, VkSurfaceKHR surface) : mInstance(instance), mSurface(surface) {}

        VkInstance mInstance;
        VkSurfaceKHR mSurface;
    };

} // namespace Lmao::Rhi::Vulkan
