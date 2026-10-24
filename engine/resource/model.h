#pragma once

// CPU-side model data, ready to upload: meshes with vertices already in model space (node
// transforms baked in), materials, and decoded images. Produced by LoadGltf; independent of
// the RHI (resource sits above core, beside the RHI, in the layering).

#include "core/math/math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Lmao::Resource
{

    // 32 bytes, scalar layout for vertex pulling from a bindless buffer. No vec3 (engine
    // convention): the UV is packed into the two w components.
    struct MeshVertex
    {
        Vec4 PositionU; // xyz = position, w = texcoord u
        Vec4 NormalV;   // xyz = normal (unit length, may be zero if the source had none), w = texcoord v
    };
    static_assert(sizeof(MeshVertex) == 32 && alignof(MeshVertex) <= 16);

    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask, // cut out where alpha < AlphaCutoff
        Blend // translucent (drawn like Mask for now; sorting comes with the renderer)
    };

    struct Material
    {
        std::string Name;
        Vec4 BaseColorFactor{1.0f};
        int32_t BaseColorImage = -1; // index into Model::Images, -1: none (use the factor alone)
        AlphaMode Alpha = AlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
    };

    // Decoded to RGBA8, tightly packed rows. Base-color images hold sRGB-encoded values.
    // std::byte so the pixels can be moved straight into Rhi::Device::QueueUpload.
    struct Image
    {
        std::string Name;
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<std::byte> Pixels;
    };

    // One glTF primitive (triangles) under one node: one draw.
    struct Mesh
    {
        std::vector<MeshVertex> Vertices;
        std::vector<uint32_t> Indices; // counter-clockwise front faces
        uint32_t Material = 0;         // index into Model::Materials (always valid)
    };

    struct Model
    {
        std::string Name;
        std::vector<Mesh> Meshes;
        std::vector<Material> Materials;
        std::vector<Image> Images; // only images some material uses (base color for now)
        Vec3 BoundsMin{0.0f};      // axis-aligned bounds of every vertex, in model space
        Vec3 BoundsMax{0.0f};
    };

} // namespace Lmao::Resource
