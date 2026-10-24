#pragma once

// Graphics pipeline description. Pipelines render with dynamic rendering (no render pass
// objects) and dynamic viewport/scissor, so one pipeline works for any target size.

#include "core/containers/enum_flags.h"
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

    enum class CompareOp : uint8_t
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    // Used when GraphicsPipelineDesc::DepthFormat is set. Defaults suit reverse-Z (decision 0005).
    struct DepthState
    {
        bool TestEnable = true;
        bool WriteEnable = true;
        CompareOp Compare = CompareOp::Greater;
    };

    enum class BlendFactor : uint8_t
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationColor,
        OneMinusDestinationColor,
        DestinationAlpha,
        OneMinusDestinationAlpha
    };

    enum class BlendOp : uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class ColorMask : uint8_t
    {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = R | G | B | A,
    };
    LMAO_ENUM_FLAGS(ColorMask)

    // result = source * SourceFactor (Op) destination * DestinationFactor, per color and alpha.
    struct BlendState
    {
        bool Enable = false;
        BlendFactor SourceColor = BlendFactor::One;
        BlendFactor DestinationColor = BlendFactor::Zero;
        BlendOp ColorOp = BlendOp::Add;
        BlendFactor SourceAlpha = BlendFactor::One;
        BlendFactor DestinationAlpha = BlendFactor::Zero;
        BlendOp AlphaOp = BlendOp::Add;
        ColorMask WriteMask = ColorMask::All;

        static constexpr BlendState Opaque() { return {}; }
        // Straight (non-premultiplied) alpha, e.g. ImGui.
        static constexpr BlendState AlphaBlend()
        {
            return {true, BlendFactor::SourceAlpha, BlendFactor::OneMinusSourceAlpha, BlendOp::Add, BlendFactor::One,
                    BlendFactor::OneMinusSourceAlpha, BlendOp::Add, ColorMask::All};
        }
        static constexpr BlendState PremultipliedAlpha()
        {
            return {true, BlendFactor::One, BlendFactor::OneMinusSourceAlpha, BlendOp::Add, BlendFactor::One,
                    BlendFactor::OneMinusSourceAlpha, BlendOp::Add, ColorMask::All};
        }
        static constexpr BlendState Additive()
        {
            return {true, BlendFactor::One, BlendFactor::One, BlendOp::Add, BlendFactor::One, BlendFactor::One,
                    BlendOp::Add, ColorMask::All};
        }
    };

    struct GraphicsPipelineDesc
    {
        ShaderStage Vertex;
        ShaderStage Fragment; // may be empty for depth-only pipelines (no color attachments)
        std::span<const Format> ColorFormats; // formats of the attachments passed to BeginRendering
        // One per color attachment, or empty for opaque everywhere.
        std::span<const BlendState> Blend;
        Format DepthFormat = Format::Unknown; // Unknown: the pass has no depth attachment
        DepthState Depth;
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
