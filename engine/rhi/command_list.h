#pragma once

// Records GPU commands for one frame. Obtained from Device::BeginFrame; valid until EndFrame.
// Texture layout transitions are inserted by the RHI (and later by the render graph), never
// by the caller.

#include "core/math/math.h"
#include "rhi/types.h"

#include <span>
#include <string_view>

namespace Lmao::Rhi
{

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

    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        CommandList(const CommandList &) = delete;
        CommandList &operator=(const CommandList &) = delete;

        // Starts rendering into the attachments; the render area is their common size.
        virtual void BeginRendering(std::span<const ColorAttachment> color_attachments) = 0;
        void BeginRendering(const ColorAttachment &color_attachment) { BeginRendering({&color_attachment, 1}); }
        virtual void EndRendering() = 0;

        // Debug markers: show up as named regions in RenderDoc, Nsight and validation messages.
        virtual void BeginMarker(std::string_view name) = 0;
        virtual void EndMarker() = 0;

    protected:
        CommandList() = default;
    };

} // namespace Lmao::Rhi
