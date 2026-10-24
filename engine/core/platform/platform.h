#pragma once

// Entry point to the OS layer: windows and input events. One Platform per process, created
// and used on the main thread only.
//
//     auto platform = Platform::Create();
//     auto window = platform->OpenWindow({.Title = "Game"});
//     while (!window->ShouldClose())
//     {
//         platform->PollEvents();
//         for (const Event &event : platform->GetEvents()) { ... }
//     }

#include "core/platform/event.h"
#include "core/platform/window.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lmao
{

    enum class DisplayBackend : uint8_t
    {
        Auto,   // the backend's choice (Wayland on Wayland sessions)
        X11,    // Linux only; falls back to Auto elsewhere
        Wayland, // Linux only; falls back to Auto elsewhere
        Headless // no display at all (CI, tools): windows exist only in memory, no input events
    };

    struct PlatformDesc
    {
        DisplayBackend Display = DisplayBackend::Auto;
    };

    struct MonitorInfo
    {
        IVec2 Position{0, 0}; // screen coordinates
        UVec2 Size{0, 0};
        IVec2 WorkPosition{0, 0}; // without task bars, docks and menu bars
        UVec2 WorkSize{0, 0};
        float ContentScale = 1.0f; // DPI scale, 1.0 = 96 DPI
    };

    class Platform
    {
    public:
        // Creates the platform backend built into this binary (GLFW for now), or the headless one
        // for DisplayBackend::Headless. nullptr on failure.
        static std::unique_ptr<Platform> Create(const PlatformDesc &desc = {});

        virtual ~Platform() = default;

        Platform(const Platform &) = delete;
        Platform &operator=(const Platform &) = delete;

        // nullptr on failure (the reason is logged).
        virtual std::unique_ptr<Window> OpenWindow(const WindowDesc &desc) = 0;

        // Replaces GetEvents() with the events that arrived since the last call.
        virtual void PollEvents() = 0;
        // Like PollEvents(), but sleeps until at least one event arrives (e.g. while minimized).
        virtual void WaitEvents() = 0;

        virtual std::span<const Event> GetEvents() const = 0;

        virtual std::string_view GetName() const = 0; // e.g. "GLFW 3.6.0 (X11)"

        // False where applications can't place their own windows or read their position
        // (Wayland). Tools that need it (e.g. ImGui multi-viewport) should turn themselves off.
        virtual bool SupportsWindowPositioning() const = 0;
        virtual std::vector<MonitorInfo> GetMonitors() const = 0; // the primary monitor first

        // UTF-8 text on the system clipboard. GetClipboardText returns "" when it holds no text.
        virtual void SetClipboardText(std::string_view text) = 0;
        virtual std::string GetClipboardText() const = 0;

    protected:
        Platform() = default;
    };

} // namespace Lmao
