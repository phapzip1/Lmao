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
#include <string_view>

namespace Lmao
{

    enum class DisplayBackend : uint8_t
    {
        Auto,   // the backend's choice (Wayland on Wayland sessions)
        X11,    // Linux only; falls back to Auto elsewhere
        Wayland // Linux only; falls back to Auto elsewhere
    };

    struct PlatformDesc
    {
        DisplayBackend Display = DisplayBackend::Auto;
    };

    class Platform
    {
    public:
        // Creates the platform backend built into this binary (GLFW for now). nullptr on failure.
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

    protected:
        Platform() = default;
    };

} // namespace Lmao
