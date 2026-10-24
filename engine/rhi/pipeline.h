#pragma once

// Graphics pipeline description. Pipelines render with dynamic rendering (no render pass
// objects) and dynamic viewport/scissor, so one pipeline works for any target size.

#include "rhi/types.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace Lmao::Rhi
{

    using PipelineHandle = Handle<struct PipelineTag>;

    // Compiled shader code for the device's backend (SPIR-V for Vulkan) plus the entry point.
    // The bytes are copied during pipeline creation and don't need to outlive the call.
    struct ShaderStage
    {
        std::span<const std::byte> Code;
        std::string_view EntryPoint;
    };

    enum class PrimitiveTopology : uint8_t
    {
        TriangleList,
        TriangleStrip,
        LineList,
        PointList
    };

    enum class CullMode : uint8_t
    {
        None,
        Front,
        Back
    };

    struct GraphicsPipelineDesc
    {
        ShaderStage Vertex;
        ShaderStage Fragment;
        std::span<const Format> ColorFormats; // formats of the attachments passed to BeginRendering
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
        CullMode Cull = CullMode::Back;   // front faces are counter-clockwise (right-handed)
        std::string_view DebugName;       // shown in RenderDoc and validation messages
    };

    // Top-left origin, in pixels.
    struct Viewport
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    struct Rect
    {
        int32_t X = 0;
        int32_t Y = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

} // namespace Lmao::Rhi
