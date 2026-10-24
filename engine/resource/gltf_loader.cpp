#include "resource/gltf_loader.h"

#include "core/io/path.h"
#include "core/log/log.h"
#include "core/platform/time.h"
#include "core/profile/profile.h"

#include <cgltf.h>
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>

namespace Lmao::Resource
{
    namespace
    {

        spdlog::logger *log_channel()
        {
            static spdlog::logger *channel = Log::Channel("resource");
            return channel;
        }

        const char *result_name(cgltf_result result)
        {
            switch (result)
            {
            case cgltf_result_success: return "success";
            case cgltf_result_data_too_short: return "data too short";
            case cgltf_result_unknown_format: return "unknown format";
            case cgltf_result_invalid_json: return "invalid JSON";
            case cgltf_result_invalid_gltf: return "invalid glTF";
            case cgltf_result_invalid_options: return "invalid options";
            case cgltf_result_file_not_found: return "file not found";
            case cgltf_result_io_error: return "IO error";
            case cgltf_result_out_of_memory: return "out of memory";
            case cgltf_result_legacy_gltf: return "legacy glTF 1.0";
            default: return "unknown error";
            }
        }

        // Whole file into memory. UTF-8 path, opened through std::filesystem so it works on Windows.
        bool read_file(std::string_view path, std::vector<uint8_t> &bytes)
        {
            std::ifstream file(PathFromUtf8(path), std::ios::binary | std::ios::ate);
            if (!file)
                return false;
            const std::streamsize size = file.tellg();
            file.seekg(0);
            bytes.resize(static_cast<size_t>(size));
            return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char *>(bytes.data()), size));
        }

        // cgltf's file callbacks, so it reads .gltf/.bin files through read_file (not fopen).
        cgltf_result cgltf_read(const cgltf_memory_options *, const cgltf_file_options *, const char *path, cgltf_size *size,
                                void **data)
        {
            std::vector<uint8_t> bytes;
            if (!read_file(path, bytes))
                return cgltf_result_file_not_found;
            void *memory = std::malloc(bytes.empty() ? 1 : bytes.size());
            if (!memory)
                return cgltf_result_out_of_memory;
            std::memcpy(memory, bytes.data(), bytes.size());
            *size = bytes.size();
            *data = memory;
            return cgltf_result_success;
        }

        void cgltf_release(const cgltf_memory_options *, const cgltf_file_options *, void *data) { std::free(data); }

        std::string directory_of(std::string_view path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash + 1));
        }

        // Encoded image bytes (PNG/JPEG) from a buffer view or a file next to the .gltf.
        bool image_bytes(const cgltf_image &image, const std::string &directory, std::vector<uint8_t> &bytes)
        {
            if (image.buffer_view)
            {
                const uint8_t *data = cgltf_buffer_view_data(image.buffer_view);
                if (!data)
                    return false;
                bytes.assign(data, data + image.buffer_view->size);
                return true;
            }
            if (!image.uri)
                return false;
            if (std::strncmp(image.uri, "data:", 5) == 0)
            {
                LMAO_LOG_WARN(log_channel(), "Image '{}': data URIs aren't supported yet", image.name ? image.name : "");
                return false;
            }
            std::string uri = image.uri;
            uri.resize(cgltf_decode_uri(uri.data())); // %20 etc.
            return read_file(directory + uri, bytes);
        }

        AlphaMode to_alpha_mode(cgltf_alpha_mode mode)
        {
            switch (mode)
            {
            case cgltf_alpha_mode_mask: return AlphaMode::Mask;
            case cgltf_alpha_mode_blend: return AlphaMode::Blend;
            default: return AlphaMode::Opaque;
            }
        }

        class Loader
        {
        public:
            Loader(const cgltf_data &data, std::string directory) : mData(data), mDirectory(std::move(directory)) {}

            Model Run()
            {
                load_materials();
                if (const cgltf_scene *scene = mData.scene ? mData.scene : (mData.scenes_count ? &mData.scenes[0] : nullptr))
                {
                    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
                        load_node(*scene->nodes[i]);
                }
                else
                {
                    // No scene: take every root node.
                    for (cgltf_size i = 0; i < mData.nodes_count; ++i)
                        if (!mData.nodes[i].parent)
                            load_node(mData.nodes[i]);
                }
                if (mModel.Meshes.empty())
                    mModel.BoundsMin = mModel.BoundsMax = Vec3(0.0f);
                return std::move(mModel);
            }

        private:
            void load_materials()
            {
                for (cgltf_size i = 0; i < mData.materials_count; ++i)
                {
                    const cgltf_material &source = mData.materials[i];
                    Material material;
                    material.Name = source.name ? source.name : "";
                    material.Alpha = to_alpha_mode(source.alpha_mode);
                    material.AlphaCutoff = source.alpha_cutoff;
                    material.DoubleSided = source.double_sided;
                    if (source.has_pbr_metallic_roughness)
                    {
                        const cgltf_pbr_metallic_roughness &pbr = source.pbr_metallic_roughness;
                        material.BaseColorFactor = Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                                        pbr.base_color_factor[2], pbr.base_color_factor[3]);
                        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                            material.BaseColorImage = load_image(*pbr.base_color_texture.texture->image);
                    }
                    mModel.Materials.push_back(std::move(material));
                }
                mDefaultMaterial = static_cast<uint32_t>(mModel.Materials.size());
                mModel.Materials.push_back({.Name = "Default"}); // for primitives without one
            }

            // Index into mModel.Images, decoding each source image once; -1 if it can't be loaded.
            int32_t load_image(const cgltf_image &image)
            {
                const auto known = mImages.find(&image);
                if (known != mImages.end())
                    return known->second;

                int32_t index = -1;
                std::vector<uint8_t> encoded;
                int width = 0, height = 0, channels = 0;
                if (image_bytes(image, mDirectory, encoded))
                {
                    stbi_uc *pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width,
                                                            &height, &channels, 4);
                    if (pixels)
                    {
                        Image decoded{.Name = image.name ? image.name : (image.uri ? image.uri : ""),
                                      .Width = static_cast<uint32_t>(width),
                                      .Height = static_cast<uint32_t>(height)};
                        const auto *bytes = reinterpret_cast<const std::byte *>(pixels);
                        decoded.Pixels.assign(bytes, bytes + size_t(width) * height * 4);
                        stbi_image_free(pixels);
                        index = static_cast<int32_t>(mModel.Images.size());
                        mModel.Images.push_back(std::move(decoded));
                    }
                    else
                    {
                        LMAO_LOG_WARN(log_channel(), "Image '{}': can't decode ({})", image.uri ? image.uri : "embedded",
                                      stbi_failure_reason());
                    }
                }
                else
                {
                    LMAO_LOG_WARN(log_channel(), "Image '{}': can't read", image.uri ? image.uri : "embedded");
                }
                mImages[&image] = index;
                return index;
            }

            void load_node(const cgltf_node &node)
            {
                if (node.mesh)
                {
                    float world[16];
                    cgltf_node_transform_world(&node, world);
                    const Mat4 transform = glm::make_mat4(world); // both column-major
                    for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i)
                        load_primitive(node.mesh->primitives[i], transform, node.mesh->name);
                }
                for (cgltf_size i = 0; i < node.children_count; ++i)
                    load_node(*node.children[i]);
            }

            void load_primitive(const cgltf_primitive &primitive, const Mat4 &transform, const char *mesh_name)
            {
                if (primitive.type != cgltf_primitive_type_triangles)
                {
                    LMAO_LOG_WARN(log_channel(), "Mesh '{}': skipping a non-triangle primitive", mesh_name ? mesh_name : "");
                    return;
                }
                const cgltf_accessor *positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
                if (!positions)
                    return;
                const cgltf_accessor *normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
                const cgltf_accessor *uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);

                // Normals use the inverse transpose; a mirroring transform flips the winding.
                const Mat3 normal_transform = glm::transpose(glm::inverse(Mat3(transform)));
                const bool mirrored = glm::determinant(Mat3(transform)) < 0.0f;

                Mesh mesh;
                mesh.Material = primitive.material
                                    ? static_cast<uint32_t>(primitive.material - mData.materials)
                                    : mDefaultMaterial;
                mesh.Vertices.resize(positions->count);
                for (cgltf_size v = 0; v < positions->count; ++v)
                {
                    float p[3] = {}, n[3] = {}, uv[2] = {};
                    cgltf_accessor_read_float(positions, v, p, 3);
                    if (normals)
                        cgltf_accessor_read_float(normals, v, n, 3);
                    if (uvs)
                        cgltf_accessor_read_float(uvs, v, uv, 2);

                    const Vec3 position = Vec3(transform * Vec4(p[0], p[1], p[2], 1.0f));
                    Vec3 normal = normal_transform * Vec3(n[0], n[1], n[2]);
                    const float length = glm::length(normal);
                    normal = length > 0.0f ? normal / length : Vec3(0.0f);

                    mesh.Vertices[v] = {Vec4(position, uv[0]), Vec4(normal, uv[1])};
                    grow_bounds(position);
                }

                if (primitive.indices)
                {
                    mesh.Indices.resize(primitive.indices->count);
                    for (cgltf_size i = 0; i < primitive.indices->count; ++i)
                        mesh.Indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
                }
                else
                {
                    mesh.Indices.resize(positions->count);
                    for (cgltf_size i = 0; i < positions->count; ++i)
                        mesh.Indices[i] = static_cast<uint32_t>(i);
                }
                mesh.Indices.resize(mesh.Indices.size() / 3 * 3); // whole triangles only
                if (mirrored)
                    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
                        std::swap(mesh.Indices[i + 1], mesh.Indices[i + 2]);

                mModel.Meshes.push_back(std::move(mesh));
            }

            void grow_bounds(const Vec3 &position)
            {
                if (mFirstVertex)
                {
                    mModel.BoundsMin = mModel.BoundsMax = position;
                    mFirstVertex = false;
                    return;
                }
                mModel.BoundsMin = glm::min(mModel.BoundsMin, position);
                mModel.BoundsMax = glm::max(mModel.BoundsMax, position);
            }

            const cgltf_data &mData;
            std::string mDirectory;
            Model mModel;
            std::unordered_map<const cgltf_image *, int32_t> mImages;
            uint32_t mDefaultMaterial = 0;
            bool mFirstVertex = true;
        };

    } // namespace

    std::optional<Model> LoadGltf(std::string_view path)
    {
        LMAO_PROFILE_ZONE();
        Stopwatch timer;
        const std::string path_string(path);

        cgltf_options options{};
        options.file.read = &cgltf_read;
        options.file.release = &cgltf_release;

        cgltf_data *data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, path_string.c_str(), &data);
        if (result == cgltf_result_success)
            result = cgltf_load_buffers(&options, data, path_string.c_str());
        if (result == cgltf_result_success)
            result = cgltf_validate(data);
        if (result != cgltf_result_success)
        {
            LMAO_LOG_ERROR(log_channel(), "Can't load '{}': {}", path, result_name(result));
            cgltf_free(data);
            return std::nullopt;
        }

        Model model = Loader(*data, directory_of(path)).Run();
        cgltf_free(data);

        const size_t slash = path.find_last_of("/\\");
        model.Name = std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
        size_t triangles = 0;
        for (const Mesh &mesh : model.Meshes)
            triangles += mesh.Indices.size() / 3;
        LMAO_LOG_INFO(log_channel(), "Loaded '{}': {} meshes, {} triangles, {} materials, {} images in {:.0f} ms",
                      model.Name, model.Meshes.size(), triangles, model.Materials.size(), model.Images.size(),
                      timer.ElapsedMilliseconds());
        return model;
    }

} // namespace Lmao::Resource
