// Sandbox: opens a window, creates the GPU device and a swapchain, draws a triangle over a
// slowly changing background, logs every input event, and marks frames for Tracy.
// Esc closes the window; V toggles vsync.

#include "core/log/log.h"
#include "core/platform/crash_handler.h"
#include "core/platform/platform.h"
#include "core/platform/time.h"
#include "core/profile/profile.h"
#include "rhi/device.h"
#include "shaders/triangle.spv.h"

#include <cmath>
#include <span>
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
        auto platform = Platform::Create();
        if (!platform)
            return 1;

        auto window = platform->OpenWindow({.Title = "Lmao Sandbox"});
        if (!window)
            return 1;

        auto device = Rhi::Device::Create({.ApplicationName = "Lmao Sandbox"});
        if (!device)
            return 1;

        const UVec2 framebuffer = window->GetFramebufferSize();
        bool vsync = true;
        auto swapchain = device->CreateSwapchain({
            .Window = window->GetNativeHandle(),
            .Width = framebuffer.x,
            .Height = framebuffer.y,
            .VSync = vsync,
        });
        if (!swapchain)
            return 1;

        const Rhi::Format color_formats[] = {swapchain->GetFormat()};
        const auto triangle_code = std::as_bytes(std::span(Shaders::Triangle));
        const Rhi::PipelineHandle triangle = device->CreateGraphicsPipeline({
            .Vertex = {.Code = triangle_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = triangle_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .DebugName = "Triangle",
        });
        if (!triangle)
            return 1;

        Stopwatch clock;
        Stopwatch frame_timer;
        while (!window->ShouldClose())
        {
            if (window->IsMinimized())
                platform->WaitEvents();
            else
                platform->PollEvents();

            for (const Event &event : platform->GetEvents())
            {
                log_event(event, *window);
                if (const auto *resize = std::get_if<FramebufferResizeEvent>(&event.Data))
                    swapchain->Resize(resize->Width, resize->Height);
                if (const auto *key = std::get_if<KeyEvent>(&event.Data);
                    key && key->Code == Key::V && key->Action == KeyAction::Press)
                {
                    vsync = !vsync;
                    swapchain->SetVSync(vsync);
                    LMAO_INFO("VSync {}", vsync ? "on" : "off");
                }
            }

            if (Rhi::CommandList *commands = device->BeginFrame(*swapchain))
            {
                const float t = static_cast<float>(clock.ElapsedSeconds());
                // Dim, so the triangle stands out.
                const Vec4 background(0.1f + 0.05f * std::sin(t), 0.1f + 0.05f * std::sin(t + 2.1f),
                                      0.1f + 0.05f * std::sin(t + 4.2f), 1.0f);
                const UVec2 size = swapchain->GetSize();

                commands->BeginMarker("Triangle pass");
                commands->BeginRendering({.Texture = swapchain->GetCurrentTexture(), .ClearColor = background});
                commands->SetViewport({.Width = static_cast<float>(size.x), .Height = static_cast<float>(size.y)});
                commands->SetScissor({.Width = size.x, .Height = size.y});
                commands->BindPipeline(triangle);
                commands->Draw(3);
                commands->EndRendering();
                commands->EndMarker();
                device->EndFrame();
            }

            LMAO_PROFILE_PLOT("Frame time (ms)", frame_timer.Lap() * 1000.0);
            LMAO_PROFILE_FRAME();
        }

        device->DestroyPipeline(triangle);
    } // swapchain, device, window, then platform destroyed here (reverse order of creation)

    LMAO_INFO("Bye");
    Log::Shutdown();
    return 0;
}
