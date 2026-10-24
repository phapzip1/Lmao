// The one translation unit that compiles the single-header libraries used by lmao_resource.

#include <cstdint> // cgltf uses uint8_t etc. without including it everywhere

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// The engine reads files itself (UTF-8 paths on every OS); stb_image only decodes memory.
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
