#pragma once

// glTF 2.0 loading (.gltf with external or embedded buffers and images, and .glb).
//
// What is read today: the default scene's node hierarchy (transforms baked into vertices),
// triangle primitives with POSITION / NORMAL / TEXCOORD_0 and indices, and each material's
// base color (factor + texture), alpha mode and double-sidedness. Everything else (other PBR
// textures, skins, animations, cameras, lights) is ignored until the systems that need it.
//
// Engine and glTF conventions match (right-handed, Y-up, meters, counter-clockwise front faces).

#include "resource/model.h"

#include <optional>
#include <string_view>

namespace Lmao::Resource
{

    // `path` is UTF-8. nullopt on failure (the reason is logged).
    std::optional<Model> LoadGltf(std::string_view path);

} // namespace Lmao::Resource
