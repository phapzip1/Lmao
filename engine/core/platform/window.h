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
        bool Decorated = true;   // title bar and borders
        bool Floating = false;   // always on top of other windows
        bool Focused = true;     // take input focus when created
        bool FocusOnShow = true; // take input focus when Show() is called
        // Client-area position in screen coordinates; ignored where the OS places windows itself
        // (Wayland, see Platform::SupportsWindowPositioning).
        bool HasPosition = false;
        IVec2 Position{0, 0};
    };

    enum class StandardCursor : uint8_t
    {
        Arrow,
        TextInput,
        Hand,
        Crosshair,
        ResizeHorizontal,
        ResizeVertical,
        ResizeNwse, // top-left <-> bottom-right
        ResizeNesw, // top-right <-> bottom-left
        ResizeAll,
        NotAllowed,
        Count
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

    enum class CursorMode : uint8_t
    {
        Normal,  // visible, moves freely
        Hidden,  // invisible over the window, moves freely
        Captured // invisible and locked to the window; MouseMoveEvent keeps reporting unbounded
                 // movement (raw, unaccelerated motion where the OS supports it): for mouse look
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

        // Client-area position in screen coordinates. Not available everywhere (see
        // Platform::SupportsWindowPositioning): GetPosition returns (0, 0) and SetPosition does nothing there.
        virtual IVec2 GetPosition() const = 0;
        virtual void SetPosition(IVec2 position) = 0;
        virtual void SetSize(UVec2 size) = 0; // client area, screen coordinates

        virtual void Show() = 0;  // for windows created with Visible = false
        virtual void Focus() = 0; // bring to front and take input focus
        virtual void SetOpacity(float opacity) = 0; // whole window, 0..1
        // Mouse input goes through the window to whatever is below it (e.g. a window being dragged).
        virtual void SetMousePassthrough(bool passthrough) = 0;

        // Shape of the cursor over this window; shapes the OS lacks fall back to the arrow.
        virtual void SetCursor(StandardCursor cursor) = 0;

        // Cursor position in client-area screen coordinates, also while it is outside the window
        // (e.g. during a drag). Not reported outside the window on Wayland.
        virtual Vec2 GetCursorPosition() const = 0;

        virtual void SetCursorMode(CursorMode mode) = 0;
        virtual CursorMode GetCursorMode() const = 0;

        virtual NativeWindowHandle GetNativeHandle() const = 0;

    protected:
        Window() = default;
    };

} // namespace Lmao
