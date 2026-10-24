#pragma once

// GPU resources: buffers, textures and samplers, created from plain descriptions by
// Rhi::Device and referred to by handle. Destruction is deferred until the GPU is done with
// every frame that might use the resource, so Destroy* is safe at any time.

#include "core/containers/enum_flags.h"
#include "rhi/types.h"

#include <cstdint>
#include <string_view>

namespace Lmao::Rhi
{

    // --- Buffers -------------------------------------------------------------------------

    enum class BufferUsage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
        Storage = 1 << 3,
        Indirect = 1 << 4,
        CopySource = 1 << 5,
        CopyDest = 1 << 6,
    };
    LMAO_ENUM_FLAGS(BufferUsage)

    enum class MemoryUsage : uint8_t
    {
        GpuOnly,  // fastest for the GPU; not mappable. Fill it with an upload (staging) copy.
        Upload,   // the CPU writes, the GPU reads: persistently mapped, write-combined. Write only.
        Readback, // the GPU writes, the CPU reads: persistently mapped and cached.
    };

    struct BufferDesc
    {
        uint64_t Size = 0; // bytes
        BufferUsage Usage = BufferUsage::None;
        MemoryUsage Memory = MemoryUsage::GpuOnly;
        std::string_view DebugName;
    };

    // --- Textures ------------------------------------------------------------------------

    enum class TextureUsage : uint32_t
    {
        None = 0,
        Sampled = 1 << 0,         // read in shaders through a sampler
        Storage = 1 << 1,         // read/written in shaders without a sampler
        ColorAttachment = 1 << 2, // rendered to (BeginRendering)
        DepthAttachment = 1 << 3,
        CopySource = 1 << 4,
        CopyDest = 1 << 5,
    };
    LMAO_ENUM_FLAGS(TextureUsage)

    struct TextureDesc
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t MipLevels = 1; // 0: the full chain down to 1x1
        Rhi::Format Format = Rhi::Format::Unknown;
        TextureUsage Usage = TextureUsage::None;
        std::string_view DebugName;
    };

    // Number of mips in a full chain for this size (1x1 has one).
    constexpr uint32_t FullMipCount(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        for (uint32_t size = width > height ? width : height; size > 1; size /= 2)
            ++levels;
        return levels;
    }

    constexpr bool IsDepthFormat(Format format) { return format == Format::D32Float; }

    // --- Samplers ------------------------------------------------------------------------

    enum class Filter : uint8_t
    {
        Nearest,
        Linear
    };

    enum class AddressMode : uint8_t
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge
    };

    struct SamplerDesc
    {
        Filter MinFilter = Filter::Linear;
        Filter MagFilter = Filter::Linear;
        Filter MipFilter = Filter::Linear;
        AddressMode AddressU = AddressMode::Repeat;
        AddressMode AddressV = AddressMode::Repeat;
        AddressMode AddressW = AddressMode::Repeat;
        float MaxAnisotropy = 1.0f; // > 1 enables anisotropic filtering; clamped to the GPU's limit
        std::string_view DebugName;
    };

    // --- Statistics ----------------------------------------------------------------------

    struct MemoryStats
    {
        uint64_t AllocatedBytes = 0;  // in use by live resources
        uint64_t ReservedBytes = 0;   // device memory blocks held by the allocator (>= AllocatedBytes)
        uint32_t AllocationCount = 0;
        uint64_t DeviceLocalUsage = 0;  // this process's use of GPU-local memory, as the OS reports it
        uint64_t DeviceLocalBudget = 0; // how much GPU-local memory the process can use without trouble
    };

} // namespace Lmao::Rhi
