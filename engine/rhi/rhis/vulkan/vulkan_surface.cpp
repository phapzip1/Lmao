#include "rhi/rhis/vulkan/vulkan_surface.h"

#include "core/profile/profile.h"
#include "rhi/rhis/vulkan/vulkan_instance.h"

// Window-system headers are confined to this file and included last: <X11/Xlib.h> defines
// None, Bool, Status, Success, ... and <windows.h> defines near, far, CreateWindow, ...
// The creation functions are loaded here with vkGetInstanceProcAddr, so volk needs no
// VK_USE_PLATFORM_* defines (which would pull these headers into every backend file).
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>
#endif

// From here on, don't spell X11 macro names (None, Bool, Status, Success, ...) outside strings.

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        const char *window_type_name(NativeWindowType type)
        {
            switch (type)
            {
            case NativeWindowType::Win32: return "Win32";
            case NativeWindowType::X11: return "X11";
            case NativeWindowType::Wayland: return "Wayland";
            case NativeWindowType::Cocoa: return "Cocoa";
            default: return "none";
            }
        }

        template <typename Function>
        Function load_function(VkInstance instance, const char *name)
        {
            return reinterpret_cast<Function>(vkGetInstanceProcAddr(instance, name));
        }

        // VK_ERROR_EXTENSION_NOT_PRESENT if this build has no path for the window's type.
        VkResult create_platform_surface(VkInstance instance, const NativeWindowHandle &window, VkSurfaceKHR &surface)
        {
            switch (window.Type)
            {
#if defined(_WIN32)
            case NativeWindowType::Win32:
            {
                const auto create = load_function<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR");
                const VkWin32SurfaceCreateInfoKHR info{
                    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                    .hinstance = static_cast<HINSTANCE>(window.Display),
                    .hwnd = reinterpret_cast<HWND>(window.Handle),
                };
                return create ? create(instance, &info, nullptr, &surface) : VK_ERROR_EXTENSION_NOT_PRESENT;
            }
#elif defined(__linux__)
            case NativeWindowType::X11:
            {
                const auto create = load_function<PFN_vkCreateXlibSurfaceKHR>(instance, "vkCreateXlibSurfaceKHR");
                const VkXlibSurfaceCreateInfoKHR info{
                    .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                    .dpy = static_cast<::Display *>(window.Display),
                    .window = static_cast<::Window>(window.Handle), // ::Window is the X11 XID, not Lmao::Window
                };
                return create ? create(instance, &info, nullptr, &surface) : VK_ERROR_EXTENSION_NOT_PRESENT;
            }
            case NativeWindowType::Wayland:
            {
                const auto create = load_function<PFN_vkCreateWaylandSurfaceKHR>(instance, "vkCreateWaylandSurfaceKHR");
                const VkWaylandSurfaceCreateInfoKHR info{
                    .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                    .display = static_cast<wl_display *>(window.Display),
                    .surface = reinterpret_cast<wl_surface *>(window.Handle),
                };
                return create ? create(instance, &info, nullptr, &surface) : VK_ERROR_EXTENSION_NOT_PRESENT;
            }
#endif
            default:
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
        }

    } // namespace

    std::unique_ptr<Surface> Surface::Create(const Instance &instance, const NativeWindowHandle &window)
    {
        LMAO_PROFILE_ZONE();

        if (!instance.SupportsWindowType(window.Type))
        {
            LMAO_LOG_ERROR(LogChannel(), "Cannot create a Vulkan surface for a {} window: the instance has no surface "
                                         "extension for it",
                           window_type_name(window.Type));
            return nullptr;
        }

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result = create_platform_surface(instance.GetHandle(), window, surface);
        if (!Check(result, "vkCreate*SurfaceKHR"))
            return nullptr;

        LMAO_LOG_DEBUG(LogChannel(), "Created {} surface", window_type_name(window.Type));
        return std::unique_ptr<Surface>(new Surface(instance.GetHandle(), surface));
    }

    Surface::~Surface()
    {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    }

    bool Surface::SupportsPresent(VkPhysicalDevice physical_device, uint32_t queue_family) const
    {
        VkBool32 supported = VK_FALSE;
        if (!Check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family, mSurface, &supported),
                   "vkGetPhysicalDeviceSurfaceSupportKHR"))
            return false;
        return supported == VK_TRUE;
    }

} // namespace Lmao::Rhi::Vulkan
