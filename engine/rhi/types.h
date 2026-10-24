#pragma once

// Basic RHI types shared by every interface header.

#include "core/containers/handle.h"

#include <cstdint>
#include <string_view>

namespace Lmao::Rhi
{

    using TextureHandle = Handle<struct TextureTag>;

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
