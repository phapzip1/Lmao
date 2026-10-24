#include "render/imgui/imgui_backend.h"

#include "core/assert/assert.h"
#include "core/log/log.h"
#include "core/profile/profile.h"
#include "shaders/imgui.spv.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <span>
#include <optional>
#include <variant>

namespace Lmao::Render
{
    namespace
    {

        // Mirrors ImGuiPush in shaders/imgui.slang.
        struct ImGuiPush
        {
            Vec2 Scale;
            Vec2 Translate;
            Rhi::BufferIndex Vertices;
            Rhi::TextureIndex Texture;
            Rhi::SamplerIndex Sampler;
            uint32_t VertexBase;
            uint32_t LinearizeColor;
        };
        static_assert(sizeof(ImGuiPush) == 36 && offsetof(ImGuiPush, Vertices) == 16);
        static_assert(sizeof(ImDrawVert) == 20, "shaders/imgui.slang expects a 20-byte ImDrawVert");
        static_assert(sizeof(ImDrawIdx) == 2, "the backend binds 16-bit indices");

        ImGuiKey to_imgui_key(Key key)
        {
            const auto offset = [](Key first, Key k) { return static_cast<int>(k) - static_cast<int>(first); };
            if (key >= Key::A && key <= Key::Z)
                return static_cast<ImGuiKey>(ImGuiKey_A + offset(Key::A, key));
            if (key >= Key::Num0 && key <= Key::Num9)
                return static_cast<ImGuiKey>(ImGuiKey_0 + offset(Key::Num0, key));
            if (key >= Key::F1 && key <= Key::F24)
                return static_cast<ImGuiKey>(ImGuiKey_F1 + offset(Key::F1, key));
            if (key >= Key::Keypad0 && key <= Key::Keypad9)
                return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + offset(Key::Keypad0, key));
            switch (key)
            {
            case Key::Space: return ImGuiKey_Space;
            case Key::Apostrophe: return ImGuiKey_Apostrophe;
            case Key::Comma: return ImGuiKey_Comma;
            case Key::Minus: return ImGuiKey_Minus;
            case Key::Period: return ImGuiKey_Period;
            case Key::Slash: return ImGuiKey_Slash;
            case Key::Semicolon: return ImGuiKey_Semicolon;
            case Key::Equal: return ImGuiKey_Equal;
            case Key::LeftBracket: return ImGuiKey_LeftBracket;
            case Key::Backslash: return ImGuiKey_Backslash;
            case Key::RightBracket: return ImGuiKey_RightBracket;
            case Key::GraveAccent: return ImGuiKey_GraveAccent;
            case Key::Escape: return ImGuiKey_Escape;
            case Key::Enter: return ImGuiKey_Enter;
            case Key::Tab: return ImGuiKey_Tab;
            case Key::Backspace: return ImGuiKey_Backspace;
            case Key::Insert: return ImGuiKey_Insert;
            case Key::Delete: return ImGuiKey_Delete;
            case Key::Right: return ImGuiKey_RightArrow;
            case Key::Left: return ImGuiKey_LeftArrow;
            case Key::Down: return ImGuiKey_DownArrow;
            case Key::Up: return ImGuiKey_UpArrow;
            case Key::PageUp: return ImGuiKey_PageUp;
            case Key::PageDown: return ImGuiKey_PageDown;
            case Key::Home: return ImGuiKey_Home;
            case Key::End: return ImGuiKey_End;
            case Key::CapsLock: return ImGuiKey_CapsLock;
            case Key::ScrollLock: return ImGuiKey_ScrollLock;
            case Key::NumLock: return ImGuiKey_NumLock;
            case Key::PrintScreen: return ImGuiKey_PrintScreen;
            case Key::Pause: return ImGuiKey_Pause;
            case Key::Menu: return ImGuiKey_Menu;
            case Key::KeypadDecimal: return ImGuiKey_KeypadDecimal;
            case Key::KeypadDivide: return ImGuiKey_KeypadDivide;
            case Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
            case Key::KeypadSubtract: return ImGuiKey_KeypadSubtract;
            case Key::KeypadAdd: return ImGuiKey_KeypadAdd;
            case Key::KeypadEnter: return ImGuiKey_KeypadEnter;
            case Key::KeypadEqual: return ImGuiKey_KeypadEqual;
            case Key::LeftShift: return ImGuiKey_LeftShift;
            case Key::LeftControl: return ImGuiKey_LeftCtrl;
            case Key::LeftAlt: return ImGuiKey_LeftAlt;
            case Key::LeftSuper: return ImGuiKey_LeftSuper;
            case Key::RightShift: return ImGuiKey_RightShift;
            case Key::RightControl: return ImGuiKey_RightCtrl;
            case Key::RightAlt: return ImGuiKey_RightAlt;
            case Key::RightSuper: return ImGuiKey_RightSuper;
            default: return ImGuiKey_None;
            }
        }

        // Identifier for ImGui's "reset render state" draw callback; never actually called.
        void reset_render_state(const ImDrawList *, const ImDrawCmd *) {}

        bool is_srgb(Rhi::Format format) { return format == Rhi::Format::RGBA8Srgb || format == Rhi::Format::BGRA8Srgb; }

        std::optional<StandardCursor> to_standard_cursor(ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
            case ImGuiMouseCursor_Arrow: return StandardCursor::Arrow;
            case ImGuiMouseCursor_TextInput: return StandardCursor::TextInput;
            case ImGuiMouseCursor_ResizeAll: return StandardCursor::ResizeAll;
            case ImGuiMouseCursor_ResizeNS: return StandardCursor::ResizeVertical;
            case ImGuiMouseCursor_ResizeEW: return StandardCursor::ResizeHorizontal;
            case ImGuiMouseCursor_ResizeNESW: return StandardCursor::ResizeNesw;
            case ImGuiMouseCursor_ResizeNWSE: return StandardCursor::ResizeNwse;
            case ImGuiMouseCursor_Hand: return StandardCursor::Hand;
            case ImGuiMouseCursor_NotAllowed: return StandardCursor::NotAllowed;
            default: return std::nullopt; // ImGuiMouseCursor_None: hide it
            }
        }

        ImVec2 to_imvec(IVec2 v) { return ImVec2(static_cast<float>(v.x), static_cast<float>(v.y)); }
        ImVec2 to_imvec(UVec2 v) { return ImVec2(static_cast<float>(v.x), static_cast<float>(v.y)); }

        UVec2 to_size(ImVec2 v)
        {
            return UVec2(static_cast<uint32_t>(std::max(v.x, 1.0f)), static_cast<uint32_t>(std::max(v.y, 1.0f)));
        }

    } // namespace

    // --- Setup ---------------------------------------------------------------------------

    ImGuiBackend &ImGuiBackend::from_io() { return *static_cast<ImGuiBackend *>(ImGui::GetIO().BackendPlatformUserData); }

    ImGuiBackend::ViewportData &ImGuiBackend::viewport_data(ImGuiViewport *viewport)
    {
        return *static_cast<ViewportData *>(viewport->PlatformUserData);
    }

    std::unique_ptr<ImGuiBackend> ImGuiBackend::Create(Rhi::Device &device, Platform &platform, Window &main_window,
                                                       Rhi::Format target_format, bool enable_viewports)
    {
        IMGUI_CHECKVERSION();
        auto backend = std::unique_ptr<ImGuiBackend>(new ImGuiBackend(device, platform, main_window, target_format));
        if (!backend->get_pipeline(target_format) || !backend->mSampler)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        io.BackendRendererName = "lmao_rhi";
        io.BackendPlatformName = "lmao_platform";
        io.BackendPlatformUserData = backend.get();
        io.BackendRendererUserData = backend.get();
        // Textures: created / updated / destroyed on request. Vertex offsets: 64K+ vertex meshes
        // with 16-bit indices. Cursors: GetMouseCursor() is honored.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset |
                           ImGuiBackendFlags_HasMouseCursors;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr; // no imgui.ini next to the executable; window layout persistence comes with the editor

        backend->mViewports = enable_viewports && platform.SupportsWindowPositioning();
        if (enable_viewports && !backend->mViewports)
            LMAO_LOG_INFO(Log::Core(), "ImGui: multi-viewport is off ({} can't position windows)", platform.GetName());
        if (backend->mViewports)
        {
            // Hovered viewport: from cursor enter/leave events; a window being dragged lets the
            // mouse through (passthrough), so the one below it is reported.
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports |
                               ImGuiBackendFlags_HasMouseHoveredViewport;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }
        backend->install_platform_callbacks();

        ImGuiStyle &style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        if (backend->mViewports)
        {
            // Platform windows are opaque rectangles: square corners and opaque backgrounds look right.
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
        const float content_scale = main_window.GetContentScale().x;
        if (content_scale > 0.0f && content_scale != 1.0f)
        {
            style.ScaleAllSizes(content_scale);
            style.FontScaleDpi = content_scale; // fonts are rasterized at this size (dynamic atlas)
        }
        return backend;
    }

    ImGuiBackend::ImGuiBackend(Rhi::Device &device, Platform &platform, Window &main_window, Rhi::Format target_format)
        : mDevice(device), mPlatform(platform), mMainWindow(&main_window), mTargetFormat(target_format)
    {
        mContext = ImGui::CreateContext();
        mMainViewport.window = &main_window;
        mSampler = mDevice.CreateSampler({
            .AddressU = Rhi::AddressMode::ClampToEdge,
            .AddressV = Rhi::AddressMode::ClampToEdge,
            .AddressW = Rhi::AddressMode::ClampToEdge,
            .DebugName = "ImGui sampler",
        });
    }

    ImGuiBackend::~ImGuiBackend()
    {
        ImGui::SetCurrentContext(mContext);
        if (mViewports)
            ImGui::DestroyPlatformWindows(); // secondary windows and their swapchains
        // ImGui owns the texture data; we own the GPU copies.
        for (ImTextureData *texture : ImGui::GetPlatformIO().Textures)
            if (texture->RefCount == 1)
                destroy_texture(*texture);
        destroy_buffers(mMainViewport);
        if (mSampler)
            mDevice.DestroySampler(mSampler);
        for (const auto &[format, pipeline] : mPipelines)
            mDevice.DestroyPipeline(pipeline);
        // ImGui asserts that the backend cleared these (DestroyPlatformWindows only does it when
        // viewports are on).
        ImGuiViewport *main = ImGui::GetMainViewport();
        main->PlatformUserData = nullptr;
        main->PlatformHandle = nullptr;
        main->RendererUserData = nullptr;
        ImGui::GetIO().BackendPlatformUserData = nullptr;
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui::DestroyContext(mContext);
    }

    Rhi::PipelineHandle ImGuiBackend::get_pipeline(Rhi::Format format)
    {
        if (const auto found = mPipelines.find(format); found != mPipelines.end())
            return found->second;
        const auto code = std::as_bytes(std::span(Shaders::Imgui));
        const Rhi::Format color_formats[] = {format};
        const Rhi::BlendState blend[] = {Rhi::BlendState::AlphaBlend()};
        const Rhi::PipelineHandle pipeline = mDevice.CreateGraphicsPipeline({
            .Vertex = {.Code = code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .Blend = blend,
            .Cull = Rhi::CullMode::None, // ImGui doesn't keep a consistent winding
            .DebugName = "ImGui",
        });
        if (pipeline)
            mPipelines[format] = pipeline;
        return pipeline;
    }

    void ImGuiBackend::install_platform_callbacks()
    {
        ImGuiPlatformIO &io = ImGui::GetPlatformIO();
        io.DrawCallback_ResetRenderState = &reset_render_state;

        io.Platform_SetClipboardTextFn = [](ImGuiContext *, const char *text) { from_io().mPlatform.SetClipboardText(text); };
        io.Platform_GetClipboardTextFn = [](ImGuiContext *) -> const char *
        {
            ImGuiBackend &backend = from_io();
            backend.mClipboard = backend.mPlatform.GetClipboardText();
            return backend.mClipboard.c_str();
        };

        ImGuiViewport *main = ImGui::GetMainViewport();
        main->PlatformHandle = mMainWindow;
        main->PlatformUserData = &mMainViewport;
        if (!mViewports)
            return;

        io.Platform_CreateWindow = [](ImGuiViewport *viewport)
        {
            ImGuiBackend &backend = from_io();
            auto data = std::make_unique<ViewportData>();
            data->owned_window = backend.mPlatform.OpenWindow({
                .Title = "ImGui",
                .Width = to_size(viewport->Size).x,
                .Height = to_size(viewport->Size).y,
                .Visible = false, // ImGui shows it after positioning it
                .Decorated = !(viewport->Flags & ImGuiViewportFlags_NoDecoration),
                .Floating = (viewport->Flags & ImGuiViewportFlags_TopMost) != 0,
                .Focused = false,
                .FocusOnShow = !(viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing),
                .HasPosition = true,
                .Position = IVec2(static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y)),
            });
            data->window = data->owned_window.get();
            viewport->PlatformHandle = data->window;
            viewport->PlatformUserData = data.release();
        };
        io.Platform_DestroyWindow = [](ImGuiViewport *viewport)
        {
            if (viewport->PlatformUserData && viewport != ImGui::GetMainViewport())
            {
                ImGuiBackend &backend = from_io();
                ViewportData *data = &viewport_data(viewport);
                if (backend.mLastHovered == data->window)
                    backend.mLastHovered = nullptr;
                backend.destroy_buffers(*data);
                delete data; // the swapchain (if still there) before the window
            }
            viewport->PlatformUserData = nullptr;
            viewport->PlatformHandle = nullptr;
        };
        io.Platform_ShowWindow = [](ImGuiViewport *viewport) { viewport_data(viewport).window->Show(); };
        io.Platform_SetWindowPos = [](ImGuiViewport *viewport, ImVec2 position)
        {
            ViewportData &data = viewport_data(viewport);
            data.ignore_move_frame = ImGui::GetFrameCount();
            data.window->SetPosition(IVec2(static_cast<int>(position.x), static_cast<int>(position.y)));
        };
        io.Platform_GetWindowPos = [](ImGuiViewport *viewport) { return to_imvec(viewport_data(viewport).window->GetPosition()); };
        io.Platform_SetWindowSize = [](ImGuiViewport *viewport, ImVec2 size)
        {
            ViewportData &data = viewport_data(viewport);
            data.ignore_resize_frame = ImGui::GetFrameCount();
            data.window->SetSize(to_size(size));
        };
        io.Platform_GetWindowSize = [](ImGuiViewport *viewport) { return to_imvec(viewport_data(viewport).window->GetSize()); };
        io.Platform_GetWindowFramebufferScale = [](ImGuiViewport *viewport)
        {
            const Window &window = *viewport_data(viewport).window;
            const UVec2 size = window.GetSize();
            const UVec2 framebuffer = window.GetFramebufferSize();
            return size.x && size.y ? ImVec2(static_cast<float>(framebuffer.x) / static_cast<float>(size.x),
                                             static_cast<float>(framebuffer.y) / static_cast<float>(size.y))
                                    : ImVec2(1.0f, 1.0f);
        };
        io.Platform_SetWindowFocus = [](ImGuiViewport *viewport) { viewport_data(viewport).window->Focus(); };
        io.Platform_GetWindowFocus = [](ImGuiViewport *viewport) { return viewport_data(viewport).window->IsFocused(); };
        io.Platform_GetWindowMinimized = [](ImGuiViewport *viewport) { return viewport_data(viewport).window->IsMinimized(); };
        io.Platform_SetWindowTitle = [](ImGuiViewport *viewport, const char *title) { viewport_data(viewport).window->SetTitle(title); };
        io.Platform_SetWindowAlpha = [](ImGuiViewport *viewport, float alpha) { viewport_data(viewport).window->SetOpacity(alpha); };
        io.Platform_GetWindowDpiScale = [](ImGuiViewport *viewport) { return viewport_data(viewport).window->GetContentScale().x; };

        io.Renderer_CreateWindow = [](ImGuiViewport *viewport)
        {
            ViewportData &data = viewport_data(viewport);
            const UVec2 framebuffer = data.window->GetFramebufferSize();
            // No vsync: the main swapchain already paces the frame; waiting on several would stall.
            data.swapchain = from_io().mDevice.CreateSwapchain({
                .Window = data.window->GetNativeHandle(),
                .Width = framebuffer.x,
                .Height = framebuffer.y,
                .VSync = false,
            });
        };
        io.Renderer_DestroyWindow = [](ImGuiViewport *viewport)
        {
            if (viewport->PlatformUserData && viewport != ImGui::GetMainViewport())
                viewport_data(viewport).swapchain.reset(); // waits for the GPU; outside the frame
        };
        io.Renderer_SetWindowSize = [](ImGuiViewport *viewport, ImVec2)
        {
            ViewportData &data = viewport_data(viewport);
            if (data.swapchain)
            {
                const UVec2 framebuffer = data.window->GetFramebufferSize();
                data.swapchain->Resize(framebuffer.x, framebuffer.y);
            }
        };
    }

    // --- Input -----------------------------------------------------------------------------

    bool ImGuiBackend::WantsMouse() const
    {
        ImGui::SetCurrentContext(mContext);
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool ImGuiBackend::WantsKeyboard() const
    {
        ImGui::SetCurrentContext(mContext);
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void ImGuiBackend::ProcessEvent(const Event &event)
    {
        ImGui::SetCurrentContext(mContext);
        ImGuiIO &io = ImGui::GetIO();
        Window *source = event.Source;
        const bool captured = source == mMainWindow && mMainWindow->GetCursorMode() == CursorMode::Captured;
        // With viewports, mouse positions are absolute (screen coordinates); otherwise relative to the main window.
        const auto to_imgui_position = [&](Vec2 local)
        {
            const IVec2 origin = mViewports && source ? source->GetPosition() : IVec2(0, 0);
            return ImVec2(local.x + static_cast<float>(origin.x), local.y + static_cast<float>(origin.y));
        };
        ImGuiViewport *viewport = source && source != mMainWindow ? ImGui::FindViewportByPlatformHandle(source) : nullptr;

        if (const auto *key = std::get_if<KeyEvent>(&event.Data))
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, HasAny(key->Mods, Modifiers::Control));
            io.AddKeyEvent(ImGuiMod_Shift, HasAny(key->Mods, Modifiers::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, HasAny(key->Mods, Modifiers::Alt));
            io.AddKeyEvent(ImGuiMod_Super, HasAny(key->Mods, Modifiers::Super));
            if (const ImGuiKey imgui_key = to_imgui_key(key->Code); imgui_key != ImGuiKey_None)
                io.AddKeyEvent(imgui_key, key->Action != KeyAction::Release);
        }
        else if (const auto *text = std::get_if<TextEvent>(&event.Data))
        {
            io.AddInputCharacter(static_cast<unsigned int>(text->Codepoint));
        }
        else if (const auto *move = std::get_if<MouseMoveEvent>(&event.Data))
        {
            if (!captured)
            {
                // With viewports: the desktop position recorded when the event happened.
                const ImVec2 position = mViewports ? ImVec2(move->ScreenPosition.x, move->ScreenPosition.y)
                                                   : to_imgui_position(move->Position);
                io.AddMousePosEvent(position.x, position.y);
            }
        }
        else if (const auto *button = std::get_if<MouseButtonEvent>(&event.Data))
        {
            if (!captured || !button->Pressed) // always deliver releases, so no button sticks
                io.AddMouseButtonEvent(static_cast<int>(button->Button), button->Pressed);
        }
        else if (const auto *scroll = std::get_if<MouseScrollEvent>(&event.Data))
        {
            if (!captured)
                io.AddMouseWheelEvent(scroll->Offset.x, scroll->Offset.y);
        }
        else if (const auto *focus = std::get_if<WindowFocusEvent>(&event.Data))
        {
            io.AddFocusEvent(focus->Focused);
        }
        else if (const auto *enter = std::get_if<CursorEnterEvent>(&event.Data))
        {
            if (enter->Entered)
            {
                mLastHovered = source;
                if (mViewports)
                    if (ImGuiViewport *hovered = ImGui::FindViewportByPlatformHandle(source))
                        io.AddMouseViewportEvent(hovered->ID);
            }
            else if (mLastHovered == source)
            {
                // Left this window without entering another of ours.
                mLastHovered = nullptr;
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                if (mViewports)
                    io.AddMouseViewportEvent(0);
            }
        }
        else if (viewport)
        {
            // OS-driven changes to a secondary viewport window: ImGui takes them from the window.
            if (std::holds_alternative<WindowCloseEvent>(event.Data))
                viewport->PlatformRequestClose = true;
            else if (std::holds_alternative<WindowMoveEvent>(event.Data))
            {
                // Echoes of our own SetPosition arrive in the next frame or two (like ImGui's GLFW backend).
                if (!viewport->PlatformUserData || ImGui::GetFrameCount() > viewport_data(viewport).ignore_move_frame + 1)
                    viewport->PlatformRequestMove = true;
            }
            else if (std::holds_alternative<WindowResizeEvent>(event.Data))
            {
                if (!viewport->PlatformUserData || ImGui::GetFrameCount() > viewport_data(viewport).ignore_resize_frame + 1)
                    viewport->PlatformRequestResize = true;
            }
            else if (const auto *resize = std::get_if<FramebufferResizeEvent>(&event.Data))
            {
                if (viewport->PlatformUserData && viewport_data(viewport).swapchain)
                    viewport_data(viewport).swapchain->Resize(resize->Width, resize->Height);
            }
        }
    }

    void ImGuiBackend::update_monitors()
    {
        ImGuiPlatformIO &io = ImGui::GetPlatformIO();
        io.Monitors.resize(0);
        for (const MonitorInfo &monitor : mPlatform.GetMonitors())
        {
            ImGuiPlatformMonitor entry;
            entry.MainPos = to_imvec(monitor.Position);
            entry.MainSize = to_imvec(monitor.Size);
            entry.WorkPos = to_imvec(monitor.WorkPosition);
            entry.WorkSize = to_imvec(monitor.WorkSize);
            entry.DpiScale = monitor.ContentScale;
            io.Monitors.push_back(entry);
        }
    }

    void ImGuiBackend::poll_mouse_position()
    {
        // Mouse events stop while the cursor is outside all our windows, e.g. when a window is
        // dragged out of the main one. While one of our windows has focus, ask it where the
        // cursor is (like ImGui's own GLFW backend), so drags keep following the mouse.
        if (mLastHovered)
            return; // events already deliver the position
        ImGuiIO &io = ImGui::GetIO();
        for (ImGuiViewport *viewport : ImGui::GetPlatformIO().Viewports)
        {
            if (!viewport->PlatformUserData)
                continue;
            const Window &window = *viewport_data(viewport).window;
            if (!window.IsFocused() || window.GetCursorMode() == CursorMode::Captured)
                continue;
            const Vec2 local = window.GetCursorPosition();
            const IVec2 origin = mViewports ? window.GetPosition() : IVec2(0, 0);
            io.AddMousePosEvent(local.x + static_cast<float>(origin.x), local.y + static_cast<float>(origin.y));
            return;
        }
    }

    void ImGuiBackend::update_passthrough()
    {
        // ImGui sets NoInputs on a viewport while its window is dragged, so docking can see the
        // window under it.
        for (ImGuiViewport *viewport : ImGui::GetPlatformIO().Viewports)
        {
            if (viewport == ImGui::GetMainViewport() || !viewport->PlatformUserData)
                continue;
            ViewportData &data = viewport_data(viewport);
            const bool passthrough = (viewport->Flags & ImGuiViewportFlags_NoInputs) != 0;
            if (passthrough != data.passthrough)
            {
                data.window->SetMousePassthrough(passthrough);
                data.passthrough = passthrough;
            }
        }
    }

    void ImGuiBackend::update_cursors()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
            return;
        const std::optional<StandardCursor> cursor = io.MouseDrawCursor ? std::nullopt : to_standard_cursor(ImGui::GetMouseCursor());
        for (ImGuiViewport *viewport : ImGui::GetPlatformIO().Viewports)
        {
            if (!viewport->PlatformUserData)
                continue;
            ViewportData &data = viewport_data(viewport);
            Window &window = *data.window;
            if (window.GetCursorMode() == CursorMode::Captured)
                continue; // the game owns it (e.g. mouse look)
            if (!cursor)
            {
                if (window.GetCursorMode() == CursorMode::Normal)
                {
                    window.SetCursorMode(CursorMode::Hidden);
                    data.hid_cursor = true;
                }
                continue;
            }
            if (data.hid_cursor)
            {
                window.SetCursorMode(CursorMode::Normal);
                data.hid_cursor = false;
            }
            window.SetCursor(*cursor);
        }
    }

    void ImGuiBackend::NewFrame(float delta_time)
    {
        ImGui::SetCurrentContext(mContext);
        ImGuiIO &io = ImGui::GetIO();
        const UVec2 size = mMainWindow->GetSize();
        const UVec2 framebuffer = mMainWindow->GetFramebufferSize();
        io.DisplaySize = to_imvec(size);
        if (size.x > 0 && size.y > 0)
            io.DisplayFramebufferScale = ImVec2(static_cast<float>(framebuffer.x) / static_cast<float>(size.x),
                                                static_cast<float>(framebuffer.y) / static_cast<float>(size.y));
        io.DeltaTime = delta_time > 0.0f ? delta_time : 1.0f / 60.0f;
        if (mViewports)
        {
            update_monitors();
            update_passthrough();
        }
        update_cursors(); // from the previous frame's request, like ImGui's own backends
        poll_mouse_position();
        ImGui::NewFrame();
    }

    void ImGuiBackend::EndFrame()
    {
        LMAO_PROFILE_ZONE();
        ImGui::SetCurrentContext(mContext);
        ImGui::Render();
        if (mViewports)
            ImGui::UpdatePlatformWindows(); // may create / destroy windows and swapchains
    }

    std::vector<Rhi::Swapchain *> ImGuiBackend::GetViewportSwapchains() const
    {
        ImGui::SetCurrentContext(mContext);
        std::vector<Rhi::Swapchain *> swapchains;
        for (ImGuiViewport *viewport : ImGui::GetPlatformIO().Viewports)
        {
            if (viewport == ImGui::GetMainViewport() || !viewport->PlatformUserData ||
                (viewport->Flags & ImGuiViewportFlags_IsMinimized))
                continue;
            if (Rhi::Swapchain *swapchain = viewport_data(viewport).swapchain.get())
                swapchains.push_back(swapchain);
        }
        return swapchains;
    }

    // --- Rendering -------------------------------------------------------------------------

    void ImGuiBackend::destroy_texture(ImTextureData &texture)
    {
        if (const auto found = mTextures.find(texture.UniqueID); found != mTextures.end())
        {
            mDevice.DestroyTexture(found->second); // deferred: frames in flight may still sample it
            mTextures.erase(found);
        }
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    void ImGuiBackend::update_textures(Rhi::CommandList &commands, ImDrawData &draw_data)
    {
        if (!draw_data.Textures)
            return;
        for (ImTextureData *texture : *draw_data.Textures)
        {
            const auto pixels = [&]
            {
                return std::as_bytes(std::span(static_cast<const unsigned char *>(texture->GetPixels()),
                                               static_cast<size_t>(texture->GetSizeInBytes())));
            };
            switch (texture->Status)
            {
            case ImTextureStatus_WantCreate:
            {
                LMAO_ASSERT(texture->Format == ImTextureFormat_RGBA32, "ImGui backend expects RGBA32 textures");
                const Rhi::TextureHandle handle = mDevice.CreateTexture({
                    .Width = static_cast<uint32_t>(texture->Width),
                    .Height = static_cast<uint32_t>(texture->Height),
                    .Format = Rhi::Format::RGBA8Unorm,
                    .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDest,
                    .DebugName = "ImGui texture",
                });
                if (!handle)
                    break; // stays WantCreate: retried next frame
                commands.UploadTexture(handle, 0, pixels());
                mTextures[texture->UniqueID] = handle;
                texture->SetTexID(TextureId(Rhi::DescriptorIndex(handle)));
                texture->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantUpdates:
                // New glyphs: re-upload the whole atlas. It's small and changes rarely; the copy
                // is ordered after earlier frames' sampling on the same queue.
                if (const auto found = mTextures.find(texture->UniqueID); found != mTextures.end())
                    commands.UploadTexture(found->second, 0, pixels());
                texture->SetStatus(ImTextureStatus_OK);
                break;
            case ImTextureStatus_WantDestroy:
                if (texture->UnusedFrames >= static_cast<int>(Rhi::Device::MaxFramesInFlight))
                    destroy_texture(*texture);
                break;
            default:
                break;
            }
        }
    }

    bool ImGuiBackend::ensure_capacity(FrameBuffers &buffers, uint64_t vertex_bytes, uint64_t index_bytes)
    {
        // Grow by doubling; the old buffers are destroyed after the GPU is done with them.
        if (vertex_bytes > buffers.vertex_capacity)
        {
            if (buffers.vertices)
                mDevice.DestroyBuffer(buffers.vertices);
            buffers.vertex_capacity = std::max<uint64_t>(vertex_bytes, std::max<uint64_t>(buffers.vertex_capacity * 2, 64 * 1024));
            buffers.vertices = mDevice.CreateBuffer({
                .Size = buffers.vertex_capacity,
                .Usage = Rhi::BufferUsage::Storage,
                .Memory = Rhi::MemoryUsage::Upload,
                .DebugName = "ImGui vertices",
            });
        }
        if (index_bytes > buffers.index_capacity)
        {
            if (buffers.indices)
                mDevice.DestroyBuffer(buffers.indices);
            buffers.index_capacity = std::max<uint64_t>(index_bytes, std::max<uint64_t>(buffers.index_capacity * 2, 32 * 1024));
            buffers.indices = mDevice.CreateBuffer({
                .Size = buffers.index_capacity,
                .Usage = Rhi::BufferUsage::Index,
                .Memory = Rhi::MemoryUsage::Upload,
                .DebugName = "ImGui indices",
            });
        }
        return buffers.vertices && buffers.indices;
    }

    void ImGuiBackend::destroy_buffers(ViewportData &data)
    {
        for (FrameBuffers &buffers : data.buffers)
        {
            if (buffers.vertices)
                mDevice.DestroyBuffer(buffers.vertices);
            if (buffers.indices)
                mDevice.DestroyBuffer(buffers.indices);
            buffers = {};
        }
    }

    void ImGuiBackend::Render(Rhi::CommandList &commands, Rhi::TextureHandle main_target)
    {
        LMAO_PROFILE_ZONE();
        ImGui::SetCurrentContext(mContext);
        ImDrawData *main_draw_data = ImGui::GetDrawData();
        if (!main_draw_data)
            return; // EndFrame wasn't called
        update_textures(commands, *main_draw_data); // the texture list is shared by every viewport

        if (main_target)
            draw(commands, *main_draw_data, main_target, mTargetFormat, mMainViewport, false);

        for (ImGuiViewport *viewport : ImGui::GetPlatformIO().Viewports)
        {
            if (viewport == ImGui::GetMainViewport() || !viewport->PlatformUserData || !viewport->DrawData ||
                (viewport->Flags & ImGuiViewportFlags_IsMinimized))
                continue;
            ViewportData &data = viewport_data(viewport);
            if (!data.swapchain || !data.swapchain->IsAcquired())
                continue;
            const bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
            draw(commands, *viewport->DrawData, data.swapchain->GetCurrentTexture(), data.swapchain->GetFormat(), data, clear);
        }
    }

    void ImGuiBackend::draw(Rhi::CommandList &commands, ImDrawData &draw_data, Rhi::TextureHandle target,
                            Rhi::Format format, ViewportData &data, bool clear)
    {
        const ImVec2 scale = draw_data.FramebufferScale;
        const uint32_t width = static_cast<uint32_t>(draw_data.DisplaySize.x * scale.x);
        const uint32_t height = static_cast<uint32_t>(draw_data.DisplaySize.y * scale.y);
        if (width == 0 || height == 0 || (draw_data.TotalVtxCount == 0 && !clear))
            return;
        const Rhi::PipelineHandle pipeline = get_pipeline(format);
        if (!pipeline)
            return;

        FrameBuffers &buffers = data.buffers[data.frame_index];
        data.frame_index = (data.frame_index + 1) % Rhi::Device::MaxFramesInFlight;
        const uint64_t vertex_bytes = std::max<uint64_t>(uint64_t(draw_data.TotalVtxCount) * sizeof(ImDrawVert), 1);
        const uint64_t index_bytes = std::max<uint64_t>(uint64_t(draw_data.TotalIdxCount) * sizeof(ImDrawIdx), 1);
        if (!ensure_capacity(buffers, vertex_bytes, index_bytes))
            return;

        // All draw lists into one vertex and one index buffer.
        auto *vertex_out = static_cast<ImDrawVert *>(mDevice.GetMappedData(buffers.vertices));
        auto *index_out = static_cast<ImDrawIdx *>(mDevice.GetMappedData(buffers.indices));
        for (const ImDrawList *list : draw_data.CmdLists)
        {
            std::memcpy(vertex_out, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(index_out, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vertex_out += list->VtxBuffer.Size;
            index_out += list->IdxBuffer.Size;
        }

        commands.BeginMarker("ImGui");
        const Rhi::ColorAttachment color{
            .Texture = target,
            .Load = clear ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load,
            .ClearColor = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        commands.BeginRendering(color);
        commands.SetViewport({.Width = static_cast<float>(width), .Height = static_cast<float>(height)});
        commands.BindPipeline(pipeline);
        commands.BindIndexBuffer(buffers.indices, Rhi::IndexType::UInt16);

        // Positions are in ImGui's coordinates from DisplayPos (screen coordinates with
        // viewports); clip space is Y-up.
        const ImVec2 position = draw_data.DisplayPos;
        const ImVec2 size = draw_data.DisplaySize;
        ImGuiPush push{
            .Scale = Vec2(2.0f / size.x, -2.0f / size.y),
            .Translate = Vec2(-1.0f - position.x * 2.0f / size.x, 1.0f + position.y * 2.0f / size.y),
            .Vertices = Rhi::DescriptorIndex(buffers.vertices),
            .Texture = {},
            .Sampler = Rhi::DescriptorIndex(mSampler),
            .VertexBase = 0,
            .LinearizeColor = is_srgb(format) ? 1u : 0u,
        };

        uint32_t list_vertex_offset = 0;
        uint32_t list_index_offset = 0;
        for (const ImDrawList *list : draw_data.CmdLists)
        {
            for (const ImDrawCmd &command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    if (command.UserCallback == &reset_render_state)
                    {
                        commands.BindPipeline(pipeline);
                        commands.BindIndexBuffer(buffers.indices, Rhi::IndexType::UInt16);
                    }
                    else
                    {
                        command.UserCallback(list, &command);
                    }
                    continue;
                }
                // Clip rectangle in framebuffer pixels, clamped to the target.
                const float left = std::max((command.ClipRect.x - position.x) * scale.x, 0.0f);
                const float top = std::max((command.ClipRect.y - position.y) * scale.y, 0.0f);
                const float right = std::min((command.ClipRect.z - position.x) * scale.x, static_cast<float>(width));
                const float bottom = std::min((command.ClipRect.w - position.y) * scale.y, static_cast<float>(height));
                if (right <= left || bottom <= top)
                    continue;
                commands.SetScissor({
                    .X = static_cast<int32_t>(left),
                    .Y = static_cast<int32_t>(top),
                    .Width = static_cast<uint32_t>(right - left),
                    .Height = static_cast<uint32_t>(bottom - top),
                });

                push.Texture = {static_cast<uint32_t>(command.GetTexID())};
                push.VertexBase = list_vertex_offset + command.VtxOffset;
                commands.PushConstants(push);
                commands.DrawIndexed(command.ElemCount, 1, list_index_offset + command.IdxOffset, 0);
            }
            list_vertex_offset += static_cast<uint32_t>(list->VtxBuffer.Size);
            list_index_offset += static_cast<uint32_t>(list->IdxBuffer.Size);
        }
        commands.EndRendering();
        commands.EndMarker();
    }

} // namespace Lmao::Render
