#include "core/platform/platforms/glfw/glfw_native.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

// OS macros are live from here on: don't spell None, Bool, Status, etc. below.

namespace Lmao::Glfw
{

    NativeWindowHandle GetNativeHandle(GLFWwindow *window)
    {
        NativeWindowHandle handle;
#if defined(_WIN32)
        handle.Type = NativeWindowType::Win32;
        handle.Display = GetModuleHandleW(nullptr);
        handle.Handle = reinterpret_cast<uint64_t>(glfwGetWin32Window(window));
#elif defined(__linux__)
        switch (glfwGetPlatform())
        {
        case GLFW_PLATFORM_X11:
            handle.Type = NativeWindowType::X11;
            handle.Display = glfwGetX11Display();
            handle.Handle = static_cast<uint64_t>(glfwGetX11Window(window));
            break;
        case GLFW_PLATFORM_WAYLAND:
            handle.Type = NativeWindowType::Wayland;
            handle.Display = glfwGetWaylandDisplay();
            handle.Handle = reinterpret_cast<uint64_t>(glfwGetWaylandWindow(window));
            break;
        default:
            break;
        }
#else
        (void)window;
#endif
        return handle;
    }

} // namespace Lmao::Glfw
