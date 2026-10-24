// Sandbox: opens a window, creates the GPU device and a swapchain, and draws a spinning,
// textured cube (indexed, depth-tested with reverse-Z, perspective camera) with a translucent
// triangle blended on top, all through the binding convention (bindless buffers, textures and
// samplers; push constants; per-frame constants). Logs every input event and marks frames for Tracy.
// Esc closes the window; V toggles vsync; M logs GPU memory usage.
// --gpu-validation: GPU-assisted validation (slow; checks bindless indices in shaders).

#include "core/log/log.h"
#include "core/platform/crash_handler.h"
#include "core/platform/platform.h"
#include "core/platform/time.h"
#include "core/profile/profile.h"
#include "rhi/device.h"
#include "shaders/cube.spv.h"
#include "shaders/triangle.spv.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
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
                   },
                   event.Data);
    }

} // namespace

int main(int argc, char **argv)
{
    bool gpu_validation = false;
    for (int i = 1; i < argc; ++i)
        gpu_validation |= std::string_view(argv[i]) == "--gpu-validation";

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

        auto device = Rhi::Device::Create({.ApplicationName = "Lmao Sandbox", .GpuValidation = gpu_validation});
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
        const auto cube_code = std::as_bytes(std::span(Shaders::Cube));
        const Rhi::PipelineHandle cube = device->CreateGraphicsPipeline({
            .Vertex = {.Code = cube_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = cube_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .DepthFormat = depth_format, // default depth state: test + write, GREATER (reverse-Z)
            .DebugName = "Cube",
        });
        const Rhi::BlendState overlay_blend[] = {Rhi::BlendState::AlphaBlend()};
        const auto triangle_code = std::as_bytes(std::span(Shaders::Triangle));
        const Rhi::PipelineHandle triangle = device->CreateGraphicsPipeline({
            .Vertex = {.Code = triangle_code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = triangle_code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .Blend = overlay_blend,
            .DepthFormat = depth_format, // same pass as the cube, but drawn on top of everything
            .Depth = {.TestEnable = false, .WriteEnable = false},
            .DebugName = "Triangle overlay",
        });
        if (!cube || !triangle)
            return 1;

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
                if (const auto *key = std::get_if<KeyEvent>(&event.Data);
                    key && key->Code == Key::M && key->Action == KeyAction::Press)
                {
                    const Rhi::MemoryStats stats = device->GetMemoryStats();
                    LMAO_INFO("GPU memory: {} allocations, {:.1f} MiB allocated, {:.1f} MiB reserved; "
                              "device-local {:.1f} of {:.1f} MiB budget",
                              stats.AllocationCount, stats.AllocatedBytes / 1048576.0, stats.ReservedBytes / 1048576.0,
                              stats.DeviceLocalUsage / 1048576.0, stats.DeviceLocalBudget / 1048576.0);
                }
            }

            const double delta_time = frame_timer.Lap();
            if (Rhi::CommandList *commands = device->BeginFrame(*swapchain))
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
                const Vec3 eye(0.0f, 1.2f, 2.6f);
                FrameConstants frame{};
                frame.View = Math::LookAt(eye, Vec3(0.0f));
                frame.Projection = Math::PerspectiveInfiniteReverseZ(Math::Radians(60.0f), aspect, 0.1f);
                frame.ViewProjection = frame.Projection * frame.View;
                frame.CameraPosition = Vec4(eye, 1.0f);
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
                commands->EndRendering();
                commands->EndMarker();
                device->EndFrame();
            }

            LMAO_PROFILE_PLOT("Frame time (ms)", delta_time * 1000.0);
            LMAO_PROFILE_FRAME();
        }

        if (depth)
            device->DestroyTexture(depth);
        device->DestroyBuffer(cube_indices);
        device->DestroyBuffer(cube_vertices);
        device->DestroyPipeline(cube);
        device->DestroySampler(sampler);
        device->DestroyTexture(checkerboard);
        device->DestroyBuffer(vertex_buffer);
        device->DestroyPipeline(triangle);
    } // swapchain, device, window, then platform destroyed here (reverse order of creation)

    LMAO_INFO("Bye");
    Log::Shutdown();
    return 0;
}
