// M0 sandbox: opens a window, logs every input event, and marks frames for Tracy.
// Esc closes the window.

#include "core/log/log.h"
#include "core/platform/crash_handler.h"
#include "core/platform/platform.h"
#include "core/platform/time.h"
#include "core/profile/profile.h"

#include <chrono>
#include <thread>
#include <variant>

using namespace Lmao;

namespace
{

    template <typename... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    void log_event(const Event &event, Window &window)
    {
        std::visit(overloaded{
                       [](const WindowCloseEvent &) { LMAO_INFO("Window close requested"); },
                       [](const WindowResizeEvent &e) { LMAO_INFO("Window resized to {}x{}", e.Width, e.Height); },
                       [](const FramebufferResizeEvent &e) { LMAO_INFO("Framebuffer resized to {}x{}", e.Width, e.Height); },
                       [](const WindowFocusEvent &e) { LMAO_INFO("Window {}", e.Focused ? "focused" : "unfocused"); },
                       [](const WindowMinimizeEvent &e) { LMAO_INFO("Window {}", e.Minimized ? "minimized" : "restored"); },
                       [&](const KeyEvent &e)
                       {
                           if (e.Action != KeyAction::Repeat)
                               LMAO_INFO("Key {} {} (scancode {}, mods 0x{:02X})", ToString(e.Code), ToString(e.Action),
                                         e.Scancode, static_cast<unsigned>(e.Mods));
                           if (e.Code == Key::Escape && e.Action == KeyAction::Press)
                               window.SetShouldClose(true);
                       },
                       [](const TextEvent &e) { LMAO_INFO("Text U+{:04X}", static_cast<uint32_t>(e.Codepoint)); },
                       [](const MouseButtonEvent &e)
                       { LMAO_INFO("Mouse {} {}", ToString(e.Button), e.Pressed ? "pressed" : "released"); },
                       // LMAO_TRACE compiles to nothing outside Debug, leaving `e` unused
                       []([[maybe_unused]] const MouseMoveEvent &e) { LMAO_TRACE("Mouse at ({:.0f}, {:.0f})", e.Position.x, e.Position.y); },
                       [](const MouseScrollEvent &e) { LMAO_INFO("Scroll ({}, {})", e.Offset.x, e.Offset.y); },
                       [](const CursorEnterEvent &e) { LMAO_INFO("Cursor {} window", e.Entered ? "entered" : "left"); },
                   },
                   event.Data);
    }

} // namespace

int main()
{
    Log::Init({.MinLevel = Log::Level::Debug, .FilePath = "logs/sandbox.log"});
    CrashHandler::Install();
    LMAO_PROFILE_THREAD_NAME("Main");

    {
        // Wayland shows nothing until a frame is presented, and there is no renderer until M1,
        // so use X11 (XWayland on Wayland sessions) for now.
        auto platform = Platform::Create({.Display = DisplayBackend::X11});
        if (!platform)
            return 1;

        auto window = platform->OpenWindow({.Title = "Lmao Sandbox"});
        if (!window)
            return 1;

        Stopwatch frame_timer;
        while (!window->ShouldClose())
        {
            if (window->IsMinimized())
                platform->WaitEvents();
            else
                platform->PollEvents();

            for (const Event &event : platform->GetEvents())
                log_event(event, *window);

            {
                LMAO_PROFILE_ZONE_NAMED("Simulated work");
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // stand-in for a frame
            }

            LMAO_PROFILE_PLOT("Frame time (ms)", frame_timer.Lap() * 1000.0);
            LMAO_PROFILE_FRAME();
        }
    } // window, then platform, destroyed here

    LMAO_INFO("Bye");
    Log::Shutdown();
    return 0;
}
