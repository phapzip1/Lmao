#include "render/imgui/imgui_backend.h"

#include "core/assert/assert.h"
#include "core/log/log.h"
#include "core/profile/profile.h"
#include "shaders/imgui.spv.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <span>
#include <vector>

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

    } // namespace

    std::unique_ptr<ImGuiBackend> ImGuiBackend::Create(Rhi::Device &device, Rhi::Format target_format, float content_scale)
    {
        IMGUI_CHECKVERSION();
        auto backend = std::unique_ptr<ImGuiBackend>(new ImGuiBackend(device, target_format));
        if (!backend->create_pipeline())
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        io.BackendRendererName = "lmao_rhi";
        io.BackendPlatformName = "lmao_platform";
        // Textures: created / updated / destroyed on request. Vertex offsets: 64K+ vertex meshes with 16-bit indices.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr; // no imgui.ini next to the executable; window layout persistence comes with the editor
        ImGui::GetPlatformIO().DrawCallback_ResetRenderState = &reset_render_state;

        ImGuiStyle &style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        if (content_scale > 0.0f && content_scale != 1.0f)
        {
            style.ScaleAllSizes(content_scale);
            style.FontScaleDpi = content_scale; // fonts are rasterized at this size (dynamic atlas)
        }
        return backend;
    }

    ImGuiBackend::ImGuiBackend(Rhi::Device &device, Rhi::Format target_format)
        : mDevice(device), mTargetFormat(target_format)
    {
        mContext = ImGui::CreateContext();
    }

    ImGuiBackend::~ImGuiBackend()
    {
        ImGui::SetCurrentContext(mContext);
        // ImGui owns the texture data; we own the GPU copies.
        for (ImTextureData *texture : ImGui::GetPlatformIO().Textures)
            if (texture->RefCount == 1)
                destroy_texture(*texture);
        for (FrameBuffers &buffers : mFrames)
        {
            if (buffers.vertices)
                mDevice.DestroyBuffer(buffers.vertices);
            if (buffers.indices)
                mDevice.DestroyBuffer(buffers.indices);
        }
        if (mSampler)
            mDevice.DestroySampler(mSampler);
        if (mPipeline)
            mDevice.DestroyPipeline(mPipeline);
        ImGui::DestroyContext(mContext);
    }

    bool ImGuiBackend::create_pipeline()
    {
        const auto code = std::as_bytes(std::span(Shaders::Imgui));
        const Rhi::Format color_formats[] = {mTargetFormat};
        const Rhi::BlendState blend[] = {Rhi::BlendState::AlphaBlend()};
        mPipeline = mDevice.CreateGraphicsPipeline({
            .Vertex = {.Code = code, .EntryPoint = "vertexMain"},
            .Fragment = {.Code = code, .EntryPoint = "fragmentMain"},
            .ColorFormats = color_formats,
            .Blend = blend,
            .Cull = Rhi::CullMode::None, // ImGui doesn't keep a consistent winding
            .DebugName = "ImGui",
        });
        mSampler = mDevice.CreateSampler({
            .AddressU = Rhi::AddressMode::ClampToEdge,
            .AddressV = Rhi::AddressMode::ClampToEdge,
            .AddressW = Rhi::AddressMode::ClampToEdge,
            .DebugName = "ImGui sampler",
        });
        return mPipeline && mSampler;
    }

    void ImGuiBackend::ProcessEvent(const Event &event, const Window &window)
    {
        ImGui::SetCurrentContext(mContext);
        ImGuiIO &io = ImGui::GetIO();
        const bool captured = window.GetCursorMode() == CursorMode::Captured;

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
                io.AddMousePosEvent(move->Position.x, move->Position.y);
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
            if (!enter->Entered)
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
    }

    void ImGuiBackend::NewFrame(const Window &window, float delta_time)
    {
        ImGui::SetCurrentContext(mContext);
        ImGuiIO &io = ImGui::GetIO();
        const UVec2 size = window.GetSize();
        const UVec2 framebuffer = window.GetFramebufferSize();
        io.DisplaySize = ImVec2(static_cast<float>(size.x), static_cast<float>(size.y));
        if (size.x > 0 && size.y > 0)
            io.DisplayFramebufferScale = ImVec2(static_cast<float>(framebuffer.x) / static_cast<float>(size.x),
                                                static_cast<float>(framebuffer.y) / static_cast<float>(size.y));
        io.DeltaTime = delta_time > 0.0f ? delta_time : 1.0f / 60.0f;
        ImGui::NewFrame();
    }

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
                commands.UploadTexture(handle, 0, std::as_bytes(std::span(static_cast<const unsigned char *>(texture->GetPixels()),
                                                                          static_cast<size_t>(texture->GetSizeInBytes()))));
                mTextures[texture->UniqueID] = handle;
                texture->SetTexID(TextureId(Rhi::DescriptorIndex(handle)));
                texture->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantUpdates:
            {
                // New glyphs: re-upload the whole atlas. It's small and changes rarely; the copy
                // is ordered after earlier frames' sampling on the same queue.
                if (const auto found = mTextures.find(texture->UniqueID); found != mTextures.end())
                    commands.UploadTexture(found->second, 0,
                                           std::as_bytes(std::span(static_cast<const unsigned char *>(texture->GetPixels()),
                                                                   static_cast<size_t>(texture->GetSizeInBytes()))));
                texture->SetStatus(ImTextureStatus_OK);
                break;
            }
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

    void ImGuiBackend::Render(Rhi::CommandList &commands, Rhi::TextureHandle target)
    {
        LMAO_PROFILE_ZONE();
        ImGui::SetCurrentContext(mContext);
        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();
        update_textures(commands, *draw_data);

        const ImVec2 scale = draw_data->FramebufferScale;
        const uint32_t width = static_cast<uint32_t>(draw_data->DisplaySize.x * scale.x);
        const uint32_t height = static_cast<uint32_t>(draw_data->DisplaySize.y * scale.y);
        if (width == 0 || height == 0 || draw_data->TotalVtxCount == 0)
            return;

        FrameBuffers &buffers = mFrames[mFrameIndex];
        mFrameIndex = (mFrameIndex + 1) % Rhi::Device::MaxFramesInFlight;
        const uint64_t vertex_bytes = uint64_t(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
        const uint64_t index_bytes = uint64_t(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);
        if (!ensure_capacity(buffers, vertex_bytes, index_bytes))
            return;

        // All draw lists into one vertex and one index buffer.
        auto *vertex_out = static_cast<ImDrawVert *>(mDevice.GetMappedData(buffers.vertices));
        auto *index_out = static_cast<ImDrawIdx *>(mDevice.GetMappedData(buffers.indices));
        for (const ImDrawList *list : draw_data->CmdLists)
        {
            std::memcpy(vertex_out, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(index_out, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vertex_out += list->VtxBuffer.Size;
            index_out += list->IdxBuffer.Size;
        }

        commands.BeginMarker("ImGui");
        const Rhi::ColorAttachment color{.Texture = target, .Load = Rhi::LoadOp::Load};
        commands.BeginRendering(color);
        commands.SetViewport({.Width = static_cast<float>(width), .Height = static_cast<float>(height)});
        commands.BindPipeline(mPipeline);
        commands.BindIndexBuffer(buffers.indices, Rhi::IndexType::UInt16);

        // ImGui positions are in screen coordinates from DisplayPos; clip space is Y-up.
        const ImVec2 position = draw_data->DisplayPos;
        const ImVec2 size = draw_data->DisplaySize;
        ImGuiPush push{
            .Scale = Vec2(2.0f / size.x, -2.0f / size.y),
            .Translate = Vec2(-1.0f - position.x * 2.0f / size.x, 1.0f + position.y * 2.0f / size.y),
            .Vertices = Rhi::DescriptorIndex(buffers.vertices),
            .Texture = {},
            .Sampler = Rhi::DescriptorIndex(mSampler),
            .VertexBase = 0,
            .LinearizeColor = is_srgb(mTargetFormat) ? 1u : 0u,
        };

        uint32_t list_vertex_offset = 0;
        uint32_t list_index_offset = 0;
        for (const ImDrawList *list : draw_data->CmdLists)
        {
            for (const ImDrawCmd &command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    if (command.UserCallback == &reset_render_state)
                    {
                        commands.BindPipeline(mPipeline);
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
