#pragma once

#include "core/math/math.h"
#include "core/platform/input.h"

#include <cstdint>
#include <variant>

namespace Lmao
{

    class Window;

    struct WindowCloseEvent
    {
    };

    struct WindowResizeEvent
    {
        uint32_t Width; // screen coordinates
        uint32_t Height;
    };

    struct WindowMoveEvent
    {
        IVec2 Position; // client area, screen coordinates (not reported on Wayland)
    };

    struct FramebufferResizeEvent
    {
        uint32_t Width; // pixels; 0 x 0 while minimized
        uint32_t Height;
    };

    struct WindowFocusEvent
    {
        bool Focused;
    };

    struct WindowMinimizeEvent
    {
        bool Minimized;
    };

    struct KeyEvent
    {
        Key Code;
        KeyAction Action;
        Modifiers Mods;
        int Scancode; // platform-specific, for keys that map to Key::Unknown
    };

    struct TextEvent
    {
        char32_t Codepoint;
    };

    struct MouseButtonEvent
    {
        MouseButton Button;
        bool Pressed;
        Modifiers Mods;
    };

    struct MouseMoveEvent
    {
        Vec2 Position;       // client area, screen coordinates, origin top-left
        Vec2 ScreenPosition; // the same point in desktop coordinates, taken when the event happened
                             // (equals Position where window positions are unknown, e.g. Wayland)
    };

    struct MouseScrollEvent
    {
        Vec2 Offset; // y > 0 scrolls up / away from the user
    };

    struct CursorEnterEvent
    {
        bool Entered;
    };

    using EventData = std::variant<WindowCloseEvent, WindowResizeEvent, FramebufferResizeEvent, WindowFocusEvent,
                                   WindowMinimizeEvent, KeyEvent, TextEvent, MouseButtonEvent, MouseMoveEvent,
                                   MouseScrollEvent, CursorEnterEvent, WindowMoveEvent>;

    struct Event
    {
        Window *Source; // valid until the next Platform::PollEvents()
        EventData Data;
    };

} // namespace Lmao
