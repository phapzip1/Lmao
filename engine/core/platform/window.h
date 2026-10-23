#pragma once

#include "core/math/math.h"

#include <cstdint>
#include <string_view>

namespace Lmao
{

    struct WindowDesc
    {
        std::string_view Title = "Lmao"; // UTF-8
        uint32_t Width = 1280;           // client area, in screen coordinates
        uint32_t Height = 720;
        bool Resizable = true;
        bool Visible = true;
    };

    enum class NativeWindowType : uint8_t
    {
        None,
        Win32,
        X11,
        Wayland,
        Cocoa
    };

    // What an RHI backend needs to create a presentation surface. Kept as untyped values so
    // no OS headers leak out of the platform backends.
    struct NativeWindowHandle
    {
        NativeWindowType Type = NativeWindowType::None;
        void *Display = nullptr; // Win32: HINSTANCE   X11: Display*    Wayland: wl_display*   Cocoa: unused
        uint64_t Handle = 0;     // Win32: HWND        X11: Window XID  Wayland: wl_surface*   Cocoa: NSWindow*
    };

    // Created by Platform::OpenWindow(); destroyed by destroying the object. The window must be
    // destroyed before the Platform that created it. All calls must come from the main thread.
    class Window
    {
    public:
        virtual ~Window() = default;

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        virtual UVec2 GetSize() const = 0;            // client area, screen coordinates
        virtual UVec2 GetFramebufferSize() const = 0; // client area, pixels (use for swapchains)
        virtual Vec2 GetContentScale() const = 0;     // DPI scale, 1.0 = 96 DPI

        virtual bool IsMinimized() const = 0;
        virtual bool IsFocused() const = 0;

        // True once the user asked to close the window (or SetShouldClose(true) was called).
        virtual bool ShouldClose() const = 0;
        virtual void SetShouldClose(bool should_close) = 0;

        virtual void SetTitle(std::string_view title) = 0; // UTF-8

        virtual NativeWindowHandle GetNativeHandle() const = 0;

    protected:
        Window() = default;
    };

} // namespace Lmao
