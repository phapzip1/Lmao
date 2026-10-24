// Sandbox: opens a window, creates the GPU device and a swapchain, and draws a glTF model
// (base-color textures, alpha cutout, simple lighting) with a fly camera. The model streams
// in through queued uploads: meshes appear as they arrive and textures show a white
// placeholder until theirs is ready. Everything goes through the
// binding convention (bindless buffers, textures and samplers; push constants; per-frame
// constants), depth-tested with reverse-Z. Without a model it draws a spinning textured cube
// with a translucent triangle on top. Logs every input event and marks frames for Tracy.
//
// Esc closes the window; V toggles vsync; M logs GPU memory usage; F1 toggles the debug panel
// (Dear ImGui through the RHI: frame time, GPU, swapchain, memory, uploads, camera).
// Camera: hold the right mouse button to look, WASD / Q / E to move, Shift faster, wheel for speed.
//
//   lmao_sandbox [--model <file.gltf|.glb>] [--gpu-validation] [--x11]
//
// The default model is DamagedHelmet from scripts/fetch_assets.sh (or .ps1).
// --gpu-validation: GPU-assisted validation (slow; checks bindless indices in shaders).
// --x11: on Linux, use X11 (XWayland on Wayland sessions), where ImGui windows can be dragged
//        out of the main window; Wayland doesn't let applications position windows.

#include "core/log/log.h"
#include "core/platform/crash_handler.h"
#include "core/platform/paths.h"
#include "core/platform/platform.h"
#include "core/platform/time.h"
#include "core/profile/profile.h"
#include "fly_camera.h"
#include "render/imgui/imgui_backend.h"
#include "render/pipeline_library.h"
#include "resource/gltf_loader.h"
#include "rhi/device.h"
#include "shaders/cube.spv.h"
#include "shaders/model.spv.h"
#include "shaders/triangle.spv.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace Lmao;

namespace
{

    // Mirrors of the shader structs (shaders/common/bindless.slang, shaders/triangle.slang).

    struct FrameConstants // set 1, std140
    {
        Mat4 ViewProjection;
        Mat4 View;
        Mat4 Projection;
        Vec4 CameraPosition;
        float Time;
        float DeltaTime;
        uint32_t Resolution[2];
    };
    static_assert(sizeof(FrameConstants) == 224);
    static_assert(offsetof(FrameConstants, CameraPosition) == 192 && offsetof(FrameConstants, Time) == 208 &&
                  offsetof(FrameConstants, Resolution) == 216);

    struct Vertex // storage buffer, scalar layout
    {
        Vec2 Position;
        Vec2 Uv;
        Vec4 Color;
    };
    static_assert(sizeof(Vertex) == 32 && offsetof(Vertex, Uv) == 8 && offsetof(Vertex, Color) == 16);

    struct TrianglePush
    {
        Rhi::BufferIndex Vertices;
        Rhi::TextureIndex Texture;
        Rhi::SamplerIndex Sampler;
        float Spin;
    };
    static_assert(sizeof(TrianglePush) == 16);

    // Counter-clockwise (front-facing); translucent (alpha-blended over the cube); UVs repeat
    // the checkerboard 4 times so mips show.
    constexpr Vertex triangle_vertices[3] = {
        {{0.0f, 0.3f}, {2.0f, 0.0f}, {1.0f, 0.3f, 0.3f, 0.6f}},
        {{-0.3f, -0.25f}, {0.0f, 4.0f}, {0.3f, 0.4f, 1.0f, 0.6f}},
        {{0.3f, -0.25f}, {4.0f, 4.0f}, {0.3f, 1.0f, 0.3f, 0.6f}},
    };

    struct CubeVertex // storage buffer, scalar layout (no vec3)
    {
        Vec4 Position;
        Vec4 Color;
        Vec2 Uv;
        Vec2 Pad;
    };
    static_assert(sizeof(CubeVertex) == 48 && offsetof(CubeVertex, Color) == 16 && offsetof(CubeVertex, Uv) == 32);

    struct CubePush
    {
        Mat4 Model;
        Rhi::BufferIndex Vertices;
        Rhi::TextureIndex Texture;
        Rhi::SamplerIndex Sampler;
        uint32_t Pad;
    };
    static_assert(sizeof(CubePush) == 80 && offsetof(CubePush, Vertices) == 64);

    // Unit cube, 4 vertices per face so each face has its own UVs and color. Each face's
    // corners go counter-clockwise seen from outside (u x v = the face normal).
    struct CubeMesh
    {
        std::array<CubeVertex, 24> Vertices;
        std::array<uint16_t, 36> Indices;
    };

    CubeMesh make_cube()
    {
        struct Face
        {
            Vec3 normal, u, v;
            Vec4 color;
        };
        const Face faces[6] = {
            {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {1.0f, 0.45f, 0.45f, 1}},  // +X
            {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0.45f, 1.0f, 1.0f, 1}},   // -X
            {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {0.45f, 1.0f, 0.45f, 1}},  // +Y
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {1.0f, 0.45f, 1.0f, 1}},   // -Y
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0.45f, 0.55f, 1.0f, 1}},   // +Z
            {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {1.0f, 1.0f, 0.45f, 1}},  // -Z
        };
        const float corner_signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        CubeMesh mesh{};
        for (uint16_t f = 0; f < 6; ++f)
        {
            for (uint16_t c = 0; c < 4; ++c)
            {
                const float su = corner_signs[c][0], sv = corner_signs[c][1];
                const Vec3 p = 0.5f * (faces[f].normal + su * faces[f].u + sv * faces[f].v);
                mesh.Vertices[f * 4 + c] = {Vec4(p, 1.0f), faces[f].color, {(su + 1) * 0.5f, (1 - sv) * 0.5f}, {}};
            }
            const uint16_t base = static_cast<uint16_t>(f * 4);
            const uint16_t face_indices[6] = {base, uint16_t(base + 1), uint16_t(base + 2), base, uint16_t(base + 2),
                                              uint16_t(base + 3)};
            std::copy(std::begin(face_indices), std::end(face_indices), mesh.Indices.begin() + f * 6);
        }
        return mesh;
    }

    struct ModelPush // shaders/model.slang
    {
        Mat4 Model;
        Vec4 BaseColorFactor;
        Rhi::BufferIndex Vertices;
        Rhi::TextureIndex BaseColor;
        Rhi::SamplerIndex Sampler;
        float AlphaCutoff;
    };
    static_assert(sizeof(ModelPush) == 96 && offsetof(ModelPush, Vertices) == 80);

    // A Resource::Model on the GPU: one vertex + index buffer per mesh, one texture per image.
    struct GpuMesh
    {
        Rhi::BufferHandle Vertices;
        Rhi::BufferHandle Indices;
        uint32_t IndexCount = 0;
        uint32_t Material = 0;
    };

    struct GpuMaterial
    {
        Vec4 BaseColorFactor{1.0f};
        Rhi::TextureHandle BaseColor; // the model's texture, or the shared white one
        float AlphaCutoff = 0.0f;     // 0: opaque
        bool DoubleSided = false;
    };

    struct GpuModel
    {
        std::vector<GpuMesh> Meshes;
        std::vector<GpuMaterial> Materials;
        std::vector<Rhi::TextureHandle> Textures;
    };

    GpuModel create_gpu_model(Rhi::Device &device, const Resource::Model &model, Rhi::TextureHandle white)
    {
        GpuModel gpu;
        for (const Resource::Image &image : model.Images)
            gpu.Textures.push_back(device.CreateTexture({
                .Width = image.Width,
                .Height = image.Height,
                .MipLevels = 0,
                .Format = Rhi::Format::RGBA8Srgb, // base color is sRGB-encoded
                .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopySource | Rhi::TextureUsage::CopyDest,
                .DebugName = image.Name,
            }));
        for (const Resource::Material &material : model.Materials)
        {
            const bool textured = material.BaseColorImage >= 0 && gpu.Textures[material.BaseColorImage].IsValid();
            gpu.Materials.push_back({
                .BaseColorFactor = material.BaseColorFactor,
                .BaseColor = textured ? gpu.Textures[material.BaseColorImage] : white,
                // Blend materials are cut out like masks until the renderer sorts translucent draws.
                .AlphaCutoff = material.Alpha == Resource::AlphaMode::Opaque ? 0.0f : material.AlphaCutoff,
                .DoubleSided = material.DoubleSided,
            });
        }
        for (size_t i = 0; i < model.Meshes.size(); ++i)
        {
            const Resource::Mesh &mesh = model.Meshes[i];
            gpu.Meshes.push_back({
                .Vertices = device.CreateBuffer({
                    .Size = mesh.Vertices.size() * sizeof(Resource::MeshVertex),
                    .Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::CopyDest,
                    .DebugName = fmt::format("{} mesh {} vertices", model.Name, i),
                }),
                .Indices = device.CreateBuffer({
                    .Size = mesh.Indices.size() * sizeof(uint32_t),
                    .Usage = Rhi::BufferUsage::Index | Rhi::BufferUsage::CopyDest,
                    .DebugName = fmt::format("{} mesh {} indices", model.Name, i),
                }),
                .IndexCount = static_cast<uint32_t>(mesh.Indices.size()),
                .Material = mesh.Material,
            });
        }
        return gpu;
    }

    template <typename T>
    std::vector<std::byte> to_bytes(const std::vector<T> &values)
    {
        const auto bytes = std::as_bytes(std::span(values));
        return {bytes.begin(), bytes.end()};
    }

    // Queued, not uploaded: the device spreads it over frames within its upload budget. Image
    // pixels are moved into the queue; the CPU-side model keeps only materials and bounds.
    void queue_gpu_model(Rhi::Device &device, const GpuModel &gpu, Resource::Model &model)
    {
        for (size_t i = 0; i < model.Meshes.size(); ++i)
        {
            device.QueueUpload(gpu.Meshes[i].Vertices, to_bytes(model.Meshes[i].Vertices));
            device.QueueUpload(gpu.Meshes[i].Indices, to_bytes(model.Meshes[i].Indices));
        }
        for (size_t i = 0; i < model.Images.size(); ++i)
        {
            if (!gpu.Textures[i])
                continue;
            device.QueueUpload(gpu.Textures[i], 0, std::move(model.Images[i].Pixels));
            device.QueueGenerateMips(gpu.Textures[i]);
        }
        model.Meshes.clear();
        model.Images.clear();
    }

    void destroy_gpu_model(Rhi::Device &device, GpuModel &gpu)
    {
        for (const GpuMesh &mesh : gpu.Meshes)
        {
            device.DestroyBuffer(mesh.Vertices);
            device.DestroyBuffer(mesh.Indices);
        }
        for (Rhi::TextureHandle texture : gpu.Textures)
            if (texture)
                device.DestroyTexture(texture);
        gpu = {};
    }

    // Recent frame times for the panel's graph.
    struct FrameHistory
    {
        std::array<float, 240> Milliseconds{};
        size_t Next = 0;
        void Add(float ms) { Milliseconds[Next++ % Milliseconds.size()] = ms; }
    };

    struct PanelState
    {
        bool Visible = true;
        bool VSync = true;
        bool ShowDemo = false;
    };

    void draw_debug_panel(PanelState &state, const FrameHistory &history, const Platform &platform,
                          const Render::ImGuiBackend &imgui, const Render::PipelineLibrary &pipelines,
                          Rhi::Device &device, Rhi::Swapchain &swapchain, const Sandbox::FlyCamera &camera, const std::optional<Resource::Model> &model, size_t mesh_count)
    {
        if (!state.Visible)
            return;
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Lmao debug", &state.Visible, ImGuiWindowFlags_AlwaysAutoResize))
        {
            // Frame time: average over the history, with a graph.
            float sum = 0.0f, worst = 0.0f;
            for (float ms : history.Milliseconds)
            {
                sum += ms;
                worst = std::max(worst, ms);
            }
            const float average = sum / static_cast<float>(history.Milliseconds.size());
            ImGui::Text("Frame %.2f ms (%.0f FPS), worst %.2f ms", average, average > 0.0f ? 1000.0f / average : 0.0f, worst);
            ImGui::PlotLines("##frame times", history.Milliseconds.data(), static_cast<int>(history.Milliseconds.size()),
                             static_cast<int>(history.Next % history.Milliseconds.size()), nullptr, 0.0f,
                             std::max(worst, 1.0f), ImVec2(320, 60));

            const Rhi::AdapterInfo &gpu = device.GetAdapterInfo();
            if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("%s (%s)", gpu.Name.c_str(), std::string(Rhi::ToString(gpu.Type)).c_str());
                ImGui::Text("%s %s, driver %s", std::string(Rhi::ToString(device.GetBackend())).c_str(),
                            gpu.ApiVersion.c_str(), gpu.Driver.c_str());
            }
            if (ImGui::CollapsingHeader("Swapchain", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const UVec2 size = swapchain.GetSize();
                ImGui::Text("%u x %u, %s, %u images", size.x, size.y, std::string(Rhi::ToString(swapchain.GetFormat())).c_str(),
                            swapchain.GetImageCount());
                if (ImGui::Checkbox("VSync (V)", &state.VSync))
                    swapchain.SetVSync(state.VSync);
            }
            if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const Rhi::MemoryStats stats = device.GetMemoryStats();
                constexpr double mib = 1024.0 * 1024.0;
                ImGui::Text("%u allocations, %.1f MiB used, %.1f MiB reserved", stats.AllocationCount,
                            stats.AllocatedBytes / mib, stats.ReservedBytes / mib);
                ImGui::Text("Device-local: %.1f of %.1f MiB budget", stats.DeviceLocalUsage / mib, stats.DeviceLocalBudget / mib);
                const double budget = static_cast<double>(stats.DeviceLocalBudget);
                ImGui::ProgressBar(budget > 0.0 ? static_cast<float>(stats.DeviceLocalUsage / budget) : 0.0f, ImVec2(320, 0));
                ImGui::Text("Queued uploads: %.1f MiB", device.GetPendingUploadBytes() / mib);
                if (pipelines.IsHotReloadEnabled())
                    ImGui::Text("Shader hot reload: on (edit shaders/*.slang), %u reload(s)", pipelines.GetReloadCount());
                else
                    ImGui::TextDisabled("Shader hot reload: off (Release build or LMAO_SHADER_HOT_RELOAD=OFF)");
            }
            if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (model)
                    ImGui::Text("%s: %zu meshes, %zu materials", model->Name.c_str(), mesh_count, model->Materials.size());
                else
                    ImGui::TextUnformatted("No model (cube)");
                const Vec3 &p = camera.GetPosition();
                ImGui::Text("Camera (%.2f, %.2f, %.2f), speed %.2f/s", p.x, p.y, p.z, camera.GetSpeed());
                ImGui::TextDisabled("Hold RMB to look, WASD/QE to move, Shift faster, wheel for speed");
            }
            if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Platform: %s", std::string(platform.GetName()).c_str());
                if (imgui.AreViewportsEnabled())
                {
                    ImGui::TextUnformatted("Viewports: on (drag windows out of the main window)");
                    // Off (ImGui's default): a window moved back inside is merged into the main
                    // window, destroying its OS window. On: it stays a separate OS window.
                    ImGui::Checkbox("Keep detached windows separate", &ImGui::GetIO().ConfigViewportsNoAutoMerge);
                }
                else
                    ImGui::TextDisabled("Viewports: off - this display can't position windows (try --x11)");
                ImGui::Checkbox("ImGui demo window", &state.ShowDemo);
            }
        }
        ImGui::End();
        if (state.ShowDemo)
            ImGui::ShowDemoWindow(&state.ShowDemo);
    }

    // 256x256 RGBA8, 32-texel checks.
    std::vector<uint32_t> make_checkerboard()
    {
        std::vector<uint32_t> texels(256 * 256);
        for (uint32_t y = 0; y < 256; ++y)
            for (uint32_t x = 0; x < 256; ++x)
                texels[y * 256 + x] = ((x / 32 + y / 32) % 2) ? 0xFFFFFFFFu : 0xFF404040u;
        return texels;
    }

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
                       [](const WindowMoveEvent &e) { LMAO_DEBUG("Window moved to ({}, {})", e.Position.x, e.Position.y); },
                   },
                   event.Data);
    }

} // namespace

int main(int argc, char **argv)
{
    bool gpu_validation = false;
    uint64_t frame_limit = 0; // --frames N: quit cleanly after N frames (smoke tests); 0 = run until closed
    DisplayBackend display = DisplayBackend::Auto;
    std::string model_path = LMAO_ASSET_DIR "/external/glTF-Sample-Assets/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument(argv[i]);
        if (argument == "--gpu-validation")
            gpu_validation = true;
        else if (argument == "--x11")
            display = DisplayBackend::X11;
        else if (argument == "--model" && i + 1 < argc)
            model_path = argv[++i];
        else if (argument == "--frames" && i + 1 < argc)
            frame_limit = std::strtoull(argv[++i], nullptr, 10);
    }

    Log::Init({.MinLevel = Log::Level::Debug, .FilePath = "logs/sandbox.log"});
    CrashHandler::Install();
    LMAO_PROFILE_THREAD_NAME("Main");

    {
        auto platform = Platform::Create({.Display = display});
        if (!platform)
            return 1;

        auto window = platform->OpenWindow({.Title = "Lmao Sandbox"});
        if (!window)
            return 1;

        const std::string cache_directory = GetUserCacheDirectory("Lmao");
        auto device = Rhi::Device::Create({
            .ApplicationName = "Lmao Sandbox",
            .GpuValidation = gpu_validation,
            .PipelineCacheDirectory = cache_directory,
        });
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

        constexpr Rhi::Format depth_format = Rhi::Format::D32Float;
        const Rhi::Format color_formats[] = {swapchain->GetFormat()};
        // Pipelines are rebuilt in place when their .slang file changes (development builds).
        Render::PipelineLibrary pipelines(*device);
        const auto cube_code = std::as_bytes(std::span(Shaders::Cube));
        const Rhi::PipelineHandle cube = pipelines.CreateGraphicsPipeline({
            .Vertex = {.Code = cube_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = cube_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .DepthFormat = depth_format, // default depth state: test + write, GREATER (reverse-Z)
            .DebugName = "Cube",
        }, Shaders::CubeSource);
        const Rhi::BlendState overlay_blend[] = {Rhi::BlendState::AlphaBlend()};
        const auto triangle_code = std::as_bytes(std::span(Shaders::Triangle));
        const Rhi::PipelineHandle triangle = pipelines.CreateGraphicsPipeline({
            .Vertex = {.Code = triangle_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = triangle_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .Blend = overlay_blend,
            .DepthFormat = depth_format, // same pass as the cube, but drawn on top of everything
            .Depth = {.TestEnable = false, .WriteEnable = false},
            .DebugName = "Triangle overlay",
        }, Shaders::TriangleSource);
        const auto model_code = std::as_bytes(std::span(Shaders::Model));
        Rhi::GraphicsPipelineDesc model_desc{
            .Vertex = {.Code = model_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = model_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .DepthFormat = depth_format,
            .DebugName = "Model",
        };
        const Rhi::PipelineHandle model_pipeline = pipelines.CreateGraphicsPipeline(model_desc, Shaders::ModelSource);
        model_desc.Cull = Rhi::CullMode::None;
        model_desc.DebugName = "Model (double-sided)";
        const Rhi::PipelineHandle model_double_sided = pipelines.CreateGraphicsPipeline(model_desc, Shaders::ModelSource);
        if (!cube || !triangle || !model_pipeline || !model_double_sided)
            return 1;

        auto imgui = Render::ImGuiBackend::Create(*device, *platform, *window, swapchain->GetFormat());
        if (!imgui)
            return 1;
        PanelState panel{.VSync = vsync};
        FrameHistory frame_history;

        // Recreated whenever the swapchain changes size (deferred destruction keeps the old one
        // alive until frames using it are done).
        Rhi::TextureHandle depth;
        UVec2 depth_size{0, 0};

        const CubeMesh cube_mesh = make_cube();
        const Rhi::BufferHandle cube_vertices = device->CreateBuffer({
            .Size = sizeof(cube_mesh.Vertices),
            .Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::CopyDest,
            .DebugName = "Cube vertices",
        });
        const Rhi::BufferHandle cube_indices = device->CreateBuffer({
            .Size = sizeof(cube_mesh.Indices),
            .Usage = Rhi::BufferUsage::Index | Rhi::BufferUsage::CopyDest,
            .DebugName = "Cube indices",
        });
        if (!cube_vertices || !cube_indices)
            return 1;

        const Rhi::BufferHandle vertex_buffer = device->CreateBuffer({
            .Size = sizeof(triangle_vertices),
            .Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::CopyDest,
            .DebugName = "Triangle vertices",
        });
        const Rhi::TextureHandle checkerboard = device->CreateTexture({
            .Width = 256,
            .Height = 256,
            .MipLevels = 0, // full chain, generated on the GPU
            .Format = Rhi::Format::RGBA8Srgb,
            .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopySource | Rhi::TextureUsage::CopyDest,
            .DebugName = "Checkerboard",
        });
        const Rhi::SamplerHandle sampler = device->CreateSampler({.MaxAnisotropy = 8.0f, .DebugName = "Linear repeat"});
        if (!vertex_buffer || !checkerboard || !sampler)
            return 1;
        bool uploaded = false; // done in the first frame: uploads are recorded into a frame

        // The model: decoded on the CPU now, then streamed to the GPU over the next frames.
        const Rhi::TextureHandle white = device->CreateTexture({
            .Width = 1,
            .Height = 1,
            .Format = Rhi::Format::RGBA8Srgb,
            .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDest,
            .DebugName = "White",
        });
        std::optional<Resource::Model> model = Resource::LoadGltf(model_path);
        if (!model)
            LMAO_WARN("No model loaded (run scripts/fetch_assets.sh, or pass --model <file>): showing the cube");
        GpuModel gpu_model = model ? create_gpu_model(*device, *model, white) : GpuModel{};
        device->QueueUpload(white, 0, std::vector<std::byte>(4, std::byte{0xFF}));
        if (model)
            queue_gpu_model(*device, gpu_model, *model);

        Sandbox::FlyCamera camera;
        float near_plane = 0.1f;
        if (model)
        {
            // Frame the model: from the front, a little above, far enough to see all of it.
            const Vec3 center = 0.5f * (model->BoundsMin + model->BoundsMax);
            const float radius = std::max(0.5f * glm::length(model->BoundsMax - model->BoundsMin), 0.5f);
            camera.LookAt(center + Vec3(0.0f, 0.3f * radius, 1.8f * radius), center);
            camera.SetSpeed(0.5f * radius);
            near_plane = std::clamp(0.005f * radius, 0.01f, 0.1f);
        }
        else
        {
            camera.LookAt(Vec3(0.0f, 1.2f, 2.6f), Vec3(0.0f));
        }

        Stopwatch clock;
        Stopwatch frame_timer;
        uint64_t frame_count = 0;
        while (!window->ShouldClose())
        {
            if (frame_limit != 0 && frame_count++ == frame_limit)
                break;
            // Sleep only when nothing is on screen: ImGui windows outside the main one keep
            // drawing while it is minimized.
            if (window->IsMinimized() && imgui->GetViewportSwapchains().empty())
                platform->WaitEvents();
            else
                platform->PollEvents();
            pipelines.Update();

            for (const Event &event : platform->GetEvents())
            {
                log_event(event, *window);
                imgui->ProcessEvent(event);
                if (imgui->IsViewportWindow(event.Source))
                    continue; // an ImGui window dragged out of the main one: only for the UI

                // The UI gets the mouse while the cursor is over it and key presses while it has
                // focus. Releases always reach the camera, so nothing stays held.
                const bool captured = window->GetCursorMode() == CursorMode::Captured;
                const bool ui_mouse = imgui->WantsMouse() && !captured;
                bool to_camera = true;
                if (std::holds_alternative<MouseMoveEvent>(event.Data) || std::holds_alternative<MouseScrollEvent>(event.Data))
                    to_camera = !ui_mouse;
                else if (const auto *button = std::get_if<MouseButtonEvent>(&event.Data))
                    to_camera = !button->Pressed || !ui_mouse;
                else if (const auto *key = std::get_if<KeyEvent>(&event.Data))
                    to_camera = key->Action == KeyAction::Release || !imgui->WantsKeyboard();
                if (to_camera)
                    camera.OnEvent(event, *window);
                // Sandbox shortcuts, unless the UI is taking the keyboard (e.g. typing in a field).
                const bool shortcuts = !imgui->WantsKeyboard();
                if (const auto *key = std::get_if<KeyEvent>(&event.Data);
                    shortcuts && key && key->Code == Key::F1 && key->Action == KeyAction::Press)
                    panel.Visible = !panel.Visible;
                if (const auto *resize = std::get_if<FramebufferResizeEvent>(&event.Data))
                    swapchain->Resize(resize->Width, resize->Height);
                if (const auto *key = std::get_if<KeyEvent>(&event.Data);
                    shortcuts && key && key->Code == Key::V && key->Action == KeyAction::Press)
                {
                    vsync = !vsync;
                    panel.VSync = vsync;
                    swapchain->SetVSync(vsync);
                    LMAO_INFO("VSync {}", vsync ? "on" : "off");
                }
                if (const auto *key = std::get_if<KeyEvent>(&event.Data);
                    shortcuts && key && key->Code == Key::M && key->Action == KeyAction::Press)
                {
                    const Rhi::MemoryStats stats = device->GetMemoryStats();
                    LMAO_INFO("GPU memory: {} allocations, {:.1f} MiB allocated, {:.1f} MiB reserved; "
                              "device-local {:.1f} of {:.1f} MiB budget",
                              stats.AllocationCount, stats.AllocatedBytes / 1048576.0, stats.ReservedBytes / 1048576.0,
                              stats.DeviceLocalUsage / 1048576.0, stats.DeviceLocalBudget / 1048576.0);
                }
            }

            const double delta_time = frame_timer.Lap();
            frame_history.Add(static_cast<float>(delta_time * 1000.0));
            camera.Update(static_cast<float>(delta_time));

            // UI first: ending the ImGui frame may open or close viewport windows (and their swapchains).
            imgui->NewFrame(static_cast<float>(delta_time));
            draw_debug_panel(panel, frame_history, *platform, *imgui, pipelines, *device, *swapchain, camera, model, gpu_model.Meshes.size());
            vsync = panel.VSync; // the checkbox may have changed it
            imgui->EndFrame();

            std::vector<Rhi::Swapchain *> swapchains = imgui->GetViewportSwapchains();
            swapchains.insert(swapchains.begin(), swapchain.get());
            if (Rhi::CommandList *commands = device->BeginFrame(swapchains))
            {
                if (!uploaded)
                {
                    commands->BeginMarker("Uploads");
                    commands->UploadBuffer(vertex_buffer, std::as_bytes(std::span(triangle_vertices)));
                    commands->UploadBuffer(cube_vertices, std::as_bytes(std::span(cube_mesh.Vertices)));
                    commands->UploadBuffer(cube_indices, std::as_bytes(std::span(cube_mesh.Indices)));
                    const std::vector<uint32_t> texels = make_checkerboard();
                    commands->UploadTexture(checkerboard, 0, std::as_bytes(std::span(texels)));
                    commands->GenerateMips(checkerboard);
                    commands->EndMarker();
                    uploaded = true;
                }

                // The main window may be minimized while ImGui windows outside it still draw.
                if (swapchain->IsAcquired())
                {
                    const float t = static_cast<float>(clock.ElapsedSeconds());
                    // Dim, so the triangle stands out.
                    const Vec4 background(0.1f + 0.05f * std::sin(t), 0.1f + 0.05f * std::sin(t + 2.1f),
                                          0.1f + 0.05f * std::sin(t + 4.2f), 1.0f);
                    const UVec2 size = swapchain->GetSize();
                    if (size != depth_size)
                    {
                        if (depth)
                            device->DestroyTexture(depth);
                        depth = device->CreateTexture({
                            .Width = size.x,
                            .Height = size.y,
                            .Format = depth_format,
                            .Usage = Rhi::TextureUsage::DepthAttachment,
                            .DebugName = "Depth buffer",
                        });
                        depth_size = size;
                    }

                    // Perspective camera with an infinite far plane: reverse-Z keeps precision at any distance.
                    const float aspect = static_cast<float>(size.x) / static_cast<float>(size.y);
                    FrameConstants frame{};
                    frame.View = camera.GetView();
                    frame.Projection = Math::PerspectiveInfiniteReverseZ(Math::Radians(60.0f), aspect, near_plane);
                    frame.ViewProjection = frame.Projection * frame.View;
                    frame.CameraPosition = Vec4(camera.GetPosition(), 1.0f);
                    frame.Time = t;
                    frame.DeltaTime = static_cast<float>(delta_time);
                    frame.Resolution[0] = size.x;
                    frame.Resolution[1] = size.y;
                    commands->SetFrameConstants(frame);

                    commands->BeginMarker("Scene pass");
                    const Rhi::ColorAttachment color{.Texture = swapchain->GetCurrentTexture(), .ClearColor = background};
                    commands->BeginRendering({
                        .ColorAttachments = {&color, 1},
                        .Depth = {.Texture = depth, .Store = Rhi::StoreOp::DontCare}, // clears to 0 (reverse-Z)
                    });
                    commands->SetViewport({.Width = static_cast<float>(size.x), .Height = static_cast<float>(size.y)});
                    commands->SetScissor({.Width = size.x, .Height = size.y});

                    if (model)
                    {
                        Rhi::PipelineHandle bound;
                        for (const GpuMesh &mesh : gpu_model.Meshes)
                        {
                            // Still streaming in: skip it this frame.
                            if (device->IsUploadPending(mesh.Vertices) || device->IsUploadPending(mesh.Indices))
                                continue;
                            const GpuMaterial &material = gpu_model.Materials[mesh.Material];
                            const Rhi::PipelineHandle pipeline = material.DoubleSided ? model_double_sided : model_pipeline;
                            if (pipeline != bound)
                            {
                                commands->BindPipeline(pipeline);
                                bound = pipeline;
                            }
                            commands->BindIndexBuffer(mesh.Indices, Rhi::IndexType::UInt32);
                            commands->PushConstants(ModelPush{
                                .Model = Mat4(1.0f),
                                .BaseColorFactor = material.BaseColorFactor,
                                .Vertices = Rhi::DescriptorIndex(mesh.Vertices),
                                // White until the texture's upload and mips are recorded.
                                .BaseColor = device->GetTextureIndex(material.BaseColor, white),
                                .Sampler = Rhi::DescriptorIndex(sampler),
                                .AlphaCutoff = material.AlphaCutoff,
                            });
                            commands->DrawIndexed(mesh.IndexCount);
                        }
                    }
                    else
                    {
                        commands->BindPipeline(cube);
                        commands->BindIndexBuffer(cube_indices, Rhi::IndexType::UInt16);
                        commands->PushConstants(CubePush{
                            .Model = glm::rotate(glm::rotate(Mat4(1.0f), t * 0.7f, Math::Up), t * 0.4f, Math::Right),
                            .Vertices = Rhi::DescriptorIndex(cube_vertices),
                            .Texture = Rhi::DescriptorIndex(checkerboard),
                            .Sampler = Rhi::DescriptorIndex(sampler),
                            .Pad = 0,
                        });
                        commands->DrawIndexed(static_cast<uint32_t>(cube_mesh.Indices.size()));

                        commands->BindPipeline(triangle);
                        commands->PushConstants(TrianglePush{
                            .Vertices = Rhi::DescriptorIndex(vertex_buffer),
                            .Texture = Rhi::DescriptorIndex(checkerboard),
                            .Sampler = Rhi::DescriptorIndex(sampler),
                            .Spin = 0.5f,
                        });
                        commands->Draw(3);
                    }
                    commands->EndRendering();
                    commands->EndMarker();
                }

                imgui->Render(*commands, swapchain->IsAcquired() ? swapchain->GetCurrentTexture() : Rhi::TextureHandle{});
                device->EndFrame();
            }

            LMAO_PROFILE_PLOT("Frame time (ms)", delta_time * 1000.0);
            LMAO_PROFILE_FRAME();
        }

        destroy_gpu_model(*device, gpu_model);
        device->DestroyTexture(white);
        if (depth)
            device->DestroyTexture(depth);
        device->DestroyBuffer(cube_indices);
        device->DestroyBuffer(cube_vertices);
        device->DestroySampler(sampler);
        device->DestroyTexture(checkerboard);
        device->DestroyBuffer(vertex_buffer);
    } // pipelines (the library destroys its own), swapchain, device, window, then platform destroyed here (reverse order of creation)

    LMAO_INFO("Bye");
    Log::Shutdown();
    return 0;
}
