#pragma once

// Dear ImGui on the engine's own platform events and RHI: no GLFW or Vulkan ImGui backend.
//
//     auto imgui = Render::ImGuiBackend::Create(*device, swapchain->GetFormat(), window->GetContentScale().x);
//     // per event:   imgui->ProcessEvent(event);
//     // per frame:   imgui->NewFrame(*window, delta_time);  ImGui::Begin(...) ...
//     //              imgui->Render(*commands, swapchain->GetCurrentTexture());  // its own pass, after the scene
//
// - Fonts and other ImGui-owned textures are RHI textures (ImGuiBackendFlags_RendererHasTextures);
//   ImTextureID is a bindless index, so ImGui::Image(ImGuiBackend::TextureId(index), ...) shows
//   any engine texture.
// - Vertices and indices go in per-frame host-visible buffers that grow as needed.
// - Destroy it before the Device.

#include "core/math/math.h"
#include "core/platform/event.h"
#include "core/platform/window.h"
#include "rhi/device.h"

#include <imgui.h>

#include <array>
#include <memory>
#include <unordered_map>

namespace Lmao::Render
{

    class ImGuiBackend
    {
    public:
        // Creates the ImGui context (docking enabled). `content_scale` is the window's DPI scale.
        // nullptr on failure (the reason is logged).
        static std::unique_ptr<ImGuiBackend> Create(Rhi::Device &device, Rhi::Format target_format, float content_scale);

        ~ImGuiBackend();

        ImGuiBackend(const ImGuiBackend &) = delete;
        ImGuiBackend &operator=(const ImGuiBackend &) = delete;

        // Feed every platform event. Mouse events are ignored while the cursor is captured.
        void ProcessEvent(const Event &event, const Window &window);

        // Starts the ImGui frame; build the UI after this.
        void NewFrame(const Window &window, float delta_time);

        // Ends the ImGui frame and draws it over `target` (loaded, not cleared) in its own pass.
        // Call once per frame, outside BeginRendering/EndRendering.
        void Render(Rhi::CommandList &commands, Rhi::TextureHandle target);

        // Whether ImGui wants the input (the cursor is over a window, a text field is active...):
        // the game should ignore it then.
        bool WantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }
        bool WantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

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

        ImGuiBackend(Rhi::Device &device, Rhi::Format target_format);

        bool create_pipeline();
        void update_textures(Rhi::CommandList &commands, ImDrawData &draw_data);
        void destroy_texture(ImTextureData &texture);
        bool ensure_capacity(FrameBuffers &buffers, uint64_t vertex_bytes, uint64_t index_bytes);

        Rhi::Device &mDevice;
        Rhi::Format mTargetFormat;
        ImGuiContext *mContext = nullptr;
        Rhi::PipelineHandle mPipeline;
        Rhi::SamplerHandle mSampler;
        std::array<FrameBuffers, Rhi::Device::MaxFramesInFlight> mFrames{};
        uint32_t mFrameIndex = 0;
        std::unordered_map<int, Rhi::TextureHandle> mTextures; // ImTextureData::UniqueID -> texture
    };

} // namespace Lmao::Render
