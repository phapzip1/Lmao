#pragma once

// Basic RHI types shared by every interface header.

#include "core/containers/handle.h"

#include <cstdint>
#include <string_view>

namespace Lmao::Rhi
{

    using BufferHandle = Handle<struct BufferTag>;
    using TextureHandle = Handle<struct TextureTag>;
    using SamplerHandle = Handle<struct SamplerTag>;

    // A resource's index in the bindless arrays (shaders/common/bindless.slang), typed so a
    // buffer index can't be passed where a texture index is expected. 4 bytes, the same layout
    // as the TextureIndex / BufferIndex / SamplerIndex structs on the shader side, so it can go
    // straight into push constants and GPU buffers.
    template <typename Tag>
    struct ShaderIndex
    {
        uint32_t Value = 0;
    };
    using TextureIndex = ShaderIndex<struct TextureTag>;
    using BufferIndex = ShaderIndex<struct BufferTag>;
    using SamplerIndex = ShaderIndex<struct SamplerTag>;
    static_assert(sizeof(TextureIndex) == 4);

    // A handle's index is its descriptor index. An invalid handle maps to index 0, the device's
    // fallback resource (magenta texture, zeroed buffer, linear sampler), so a forgotten or failed
    // resource shows up on screen instead of reading an arbitrary slot.
    template <typename Tag>
    constexpr ShaderIndex<Tag> DescriptorIndex(Handle<Tag> handle)
    {
        return {handle.IsValid() ? handle.Index : 0};
    }

    enum class Format : uint8_t
    {
        Unknown,
        RGBA8Unorm,
        RGBA8Srgb,
        BGRA8Unorm,
        BGRA8Srgb,
        RGB10A2Unorm,
        RGBA16Float,
        D32Float,
    };

    // Size of one texel. All formats are uncompressed for now; block-compressed formats (BCn)
    // will need a block size instead.
    constexpr uint32_t BytesPerTexel(Format format)
    {
        switch (format)
        {
        case Format::RGBA8Unorm:
        case Format::RGBA8Srgb:
        case Format::BGRA8Unorm:
        case Format::BGRA8Srgb:
        case Format::RGB10A2Unorm:
        case Format::D32Float: return 4;
        case Format::RGBA16Float: return 8;
        case Format::Unknown: break;
        }
        return 0;
    }

    constexpr std::string_view ToString(Format format)
    {
        switch (format)
        {
        case Format::RGBA8Unorm: return "RGBA8Unorm";
        case Format::RGBA8Srgb: return "RGBA8Srgb";
        case Format::BGRA8Unorm: return "BGRA8Unorm";
        case Format::BGRA8Srgb: return "BGRA8Srgb";
        case Format::RGB10A2Unorm: return "RGB10A2Unorm";
        case Format::RGBA16Float: return "RGBA16Float";
        case Format::D32Float: return "D32Float";
        case Format::Unknown: break;
        }
        return "Unknown";
    }

} // namespace Lmao::Rhi
