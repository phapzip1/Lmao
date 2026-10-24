#pragma once

// Dear ImGui on the engine's own platform layer and RHI: no GLFW or Vulkan ImGui backend.
//
//     auto imgui = Render::ImGuiBackend::Create(*device, *platform, *window, swapchain->GetFormat());
//     // per event:   imgui->ProcessEvent(event);
//     // per frame:   imgui->NewFrame(delta_time);  ...ImGui:: calls...  imgui->EndFrame();
//     //              swapchains = {main swapchain} + imgui->GetViewportSwapchains()
//     //              commands = device->BeginFrame(swapchains); ...scene...
//     //              imgui->Render(*commands, main_swapchain->GetCurrentTexture());  // after the scene
//
// - Multi-viewport: ImGui windows dragged outside the main window become OS windows, each with
//   its own swapchain, presented in the same frame. Needs window positioning, so it's off on
//   Wayland (Platform::SupportsWindowPositioning); ImGui windows then stay inside the main one.
// - Mouse cursor shapes and the system clipboard go through the platform layer.
// - Fonts and other ImGui-owned textures are RHI textures (ImGuiBackendFlags_RendererHasTextures);
//   ImTextureID is a bindless index, so ImGui::Image(ImGuiBackend::TextureId(index), ...) shows
//   any engine texture.
// - Vertices and indices go in per-frame host-visible buffers (per viewport) that grow as needed.
// - Destroy it before the Device and the Platform.

#include "core/math/math.h"
#include "core/platform/event.h"
#include "core/platform/platform.h"
#include "core/platform/window.h"
#include "rhi/device.h"

#include <imgui.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lmao::Render
{

    class ImGuiBackend
    {
    public:
        // Creates the ImGui context (docking, and viewports where the platform allows them).
        // nullptr on failure (the reason is logged).
        static std::unique_ptr<ImGuiBackend> Create(Rhi::Device &device, Platform &platform, Window &main_window,
                                                    Rhi::Format target_format, bool enable_viewports = true);

        ~ImGuiBackend();

        ImGuiBackend(const ImGuiBackend &) = delete;
        ImGuiBackend &operator=(const ImGuiBackend &) = delete;

        // Feed every platform event (from every window). Mouse events of the main window are
        // ignored while its cursor is captured.
        void ProcessEvent(const Event &event);

        // Starts the ImGui frame; build the UI after this.
        void NewFrame(float delta_time);
        // Ends the ImGui frame and creates / moves / destroys viewport windows and their
        // swapchains. Call before Device::BeginFrame.
        void EndFrame();
        // Swapchains of the secondary viewports, to pass to Device::BeginFrame with the main one.
        std::vector<Rhi::Swapchain *> GetViewportSwapchains() const;

        // Draws the main viewport over `main_target` (loaded, not cleared; skipped if invalid) and
        // each secondary viewport into its swapchain, each in its own pass. Once per frame,
        // outside BeginRendering/EndRendering.
        void Render(Rhi::CommandList &commands, Rhi::TextureHandle main_target);

        // Whether ImGui wants the input (the cursor is over a window, a text field is active...):
        // the game should ignore it then.
        bool WantsMouse() const;
        bool WantsKeyboard() const;
        bool AreViewportsEnabled() const { return mViewports; }
        // The event came from a secondary viewport window (not for the game at all).
        bool IsViewportWindow(const Window *window) const { return window && window != mMainWindow; }

        static ImTextureID TextureId(Rhi::TextureIndex index) { return static_cast<ImTextureID>(index.Value); }

    private:
        // Host-visible, mapped; one set per frame in flight (Render is called once per frame).
        struct FrameBuffers
        {
            Rhi::BufferHandle vertices;
            Rhi::BufferHandle indices;
            uint64_t vertex_capacity = 0; // bytes
            uint64_t index_capacity = 0;
        };

        // ImGuiViewport::PlatformUserData. The main viewport's window is the application's.
        struct ViewportData
        {
            Window *window = nullptr;
            std::unique_ptr<Window> owned_window; // secondary viewports
            std::unique_ptr<Rhi::Swapchain> swapchain;
            std::array<FrameBuffers, Rhi::Device::MaxFramesInFlight> buffers{};
            uint32_t frame_index = 0;
            bool hid_cursor = false; // we set CursorMode::Hidden for ImGuiMouseCursor_None
            bool passthrough = false; // mouse passthrough is on (ImGuiViewportFlags_NoInputs)
            // Frame of our last SetPosition / SetSize: the OS echoes them back as move / resize
            // events, which must not be mistaken for the user moving the window.
            int ignore_move_frame = -1;
            int ignore_resize_frame = -1;
        };

        ImGuiBackend(Rhi::Device &device, Platform &platform, Window &main_window, Rhi::Format target_format);

        void install_platform_callbacks();
        void update_monitors();
        void update_cursors();
        void update_passthrough();
        void poll_mouse_position();
        Rhi::PipelineHandle get_pipeline(Rhi::Format format);
        void update_textures(Rhi::CommandList &commands, ImDrawData &draw_data);
        void destroy_texture(ImTextureData &texture);
        bool ensure_capacity(FrameBuffers &buffers, uint64_t vertex_bytes, uint64_t index_bytes);
        void destroy_buffers(ViewportData &data);
        void draw(Rhi::CommandList &commands, ImDrawData &draw_data, Rhi::TextureHandle target, Rhi::Format format,
                  ViewportData &data, bool clear);

        // ImGuiPlatformIO / ImGuiIO callbacks reach the backend through these.
        static ImGuiBackend &from_io();
        static ViewportData &viewport_data(ImGuiViewport *viewport);

        Rhi::Device &mDevice;
        Platform &mPlatform;
        Window *mMainWindow;
        Rhi::Format mTargetFormat;
        ImGuiContext *mContext = nullptr;
        bool mViewports = false;
        ViewportData mMainViewport;
        std::unordered_map<Rhi::Format, Rhi::PipelineHandle> mPipelines; // per target format
        Rhi::SamplerHandle mSampler;
        std::unordered_map<int, Rhi::TextureHandle> mTextures; // ImTextureData::UniqueID -> texture
        std::string mClipboard;                                // keeps GetClipboardText's result alive
        Window *mLastHovered = nullptr;                        // window the cursor is in, for leave events
    };

} // namespace Lmao::Render
