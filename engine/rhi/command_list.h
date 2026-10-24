#pragma once

// Records GPU commands for one frame. Obtained from Device::BeginFrame; valid until EndFrame.
// Texture layout transitions and the barriers around copies are inserted by the RHI (and
// later by the render graph), never by the caller.

#include "core/math/math.h"
#include "rhi/pipeline.h"
#include "rhi/types.h"

#include <cstddef>
#include <type_traits>

#include <span>
#include <string_view>

namespace Lmao::Rhi
{

    // Binding convention (docs/decisions/0010): push constants per draw, set 1 per frame.
    inline constexpr uint32_t MaxPushConstantsSize = 128;
    inline constexpr uint32_t MaxFrameConstantsSize = 4096;

    enum class LoadOp : uint8_t
    {
        Load,    // keep the previous contents
        Clear,   // fill with ClearColor
        DontCare // contents are undefined; cheapest when every pixel is overwritten
    };

    enum class StoreOp : uint8_t
    {
        Store,
        DontCare
    };

    struct ColorAttachment
    {
        TextureHandle Texture;
        LoadOp Load = LoadOp::Clear;
        StoreOp Store = StoreOp::Store;
        Vec4 ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
    };

    // Reverse-Z (decision 0005): near is 1, far is 0, so depth clears to 0 and the usual
    // compare is GREATER.
    struct DepthAttachment
    {
        TextureHandle Texture; // invalid: no depth attachment
        LoadOp Load = LoadOp::Clear;
        StoreOp Store = StoreOp::Store; // DontCare when no later pass reads the depth
        float ClearDepth = 0.0f;
    };

    enum class IndexType : uint8_t
    {
        UInt16,
        UInt32
    };

    // A pass declares what it writes (attachments) and what it samples. The RHI moves sampled
    // textures to their shader-read layout before the pass, so a render target can be sampled
    // by a later pass without any barrier in renderer code (decision 0008; the M5 render graph
    // builds on these declarations).
    struct RenderingDesc
    {
        std::span<const ColorAttachment> ColorAttachments; // may be empty for depth-only passes
        DepthAttachment Depth;
        std::span<const TextureHandle> SampledTextures; // needs TextureUsage::Sampled
    };

    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        CommandList(const CommandList &) = delete;
        CommandList &operator=(const CommandList &) = delete;

        // Starts rendering into the attachments (color and/or depth); the render area is their
        // common size.
        virtual void BeginRendering(const RenderingDesc &desc) = 0;
        void BeginRendering(const ColorAttachment &color_attachment)
        {
            BeginRendering(RenderingDesc{.ColorAttachments = {&color_attachment, 1}});
        }
        virtual void EndRendering() = 0;

        // Viewport and scissor are dynamic state: set them after BeginRendering, before drawing.
        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const Rect &scissor) = 0;

        virtual void BindPipeline(PipelineHandle pipeline) = 0;

        // Up to MaxPushConstantsSize bytes for the next draws, visible to every shader stage.
        // Offset and size must be multiples of 4.
        virtual void PushConstants(std::span<const std::byte> data, uint32_t offset = 0) = 0;
        template <typename T>
        void PushConstants(const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= MaxPushConstantsSize && sizeof(T) % 4 == 0);
            PushConstants(std::as_bytes(std::span(&value, 1)));
        }

        // This frame's set 1 data (FrameConstants in bindless.slang), up to MaxFrameConstantsSize
        // bytes. Copied right away; the last call before EndFrame wins for the whole frame.
        virtual void SetFrameConstants(std::span<const std::byte> data) = 0;
        template <typename T>
        void SetFrameConstants(const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= MaxFrameConstantsSize);
            SetFrameConstants(std::as_bytes(std::span(&value, 1)));
        }
        virtual void Draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
                          uint32_t first_instance = 0) = 0;

        // Needs BufferUsage::Index. Vertex data is still pulled from bindless buffers in the
        // shader; SV_VertexID is the index value (plus vertex_offset).
        virtual void BindIndexBuffer(BufferHandle buffer, IndexType type = IndexType::UInt32, uint64_t offset = 0) = 0;
        virtual void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0,
                                 int32_t vertex_offset = 0, uint32_t first_instance = 0) = 0;

        // --- Transfers (outside BeginRendering/EndRendering) ----------------------------------
        // Uploads copy `data` into a per-frame staging ring right away, so the caller's memory
        // can be reused as soon as the call returns. The GPU copy runs with this frame's commands,
        // before any later command in the frame reads the destination.

        // Needs BufferUsage::CopyDest.
        virtual void UploadBuffer(BufferHandle buffer, std::span<const std::byte> data, uint64_t offset = 0) = 0;
        // One mip of a texture, tightly packed rows of max(width >> mip, 1) x max(height >> mip, 1)
        // texels. Needs TextureUsage::CopyDest. Sampled textures are left ready to sample.
        virtual void UploadTexture(TextureHandle texture, uint32_t mip, std::span<const std::byte> data) = 0;
        // Fills mips 1..N-1 by repeatedly halving mip 0 with a linear filter.
        // Needs TextureUsage::CopySource | CopyDest and a format that supports linear blits.
        virtual void GenerateMips(TextureHandle texture) = 0;

        // GPU-to-GPU copy. Needs CopySource on `source` and CopyDest on `destination`.
        virtual void CopyBuffer(BufferHandle source, uint64_t source_offset, BufferHandle destination,
                                uint64_t destination_offset, uint64_t size) = 0;
        // One mip, tightly packed, into a buffer (e.g. a Readback buffer for screenshots or tests).
        // The data is on the CPU side once this frame has finished on the GPU (e.g. after WaitIdle).
        virtual void CopyTextureToBuffer(TextureHandle texture, uint32_t mip, BufferHandle buffer, uint64_t offset = 0) = 0;

        // Debug markers: show up as named regions in RenderDoc, Nsight and validation messages.
        virtual void BeginMarker(std::string_view name) = 0;
        virtual void EndMarker() = 0;

    protected:
        CommandList() = default;
    };

} // namespace Lmao::Rhi
