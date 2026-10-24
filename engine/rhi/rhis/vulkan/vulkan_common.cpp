#include "rhi/rhis/vulkan/vulkan_common.h"

#include <cstring>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        // vk_enum_string_helper.h now lives in Vulkan-Utility-Libraries; these are the results
        // the engine is likely to see.
        const char *result_name(VkResult result)
        {
            switch (result)
            {
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
            case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
            case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
            default: return nullptr;
            }
        }

    } // namespace

    spdlog::logger *LogChannel()
    {
        static spdlog::logger *channel = Log::Channel("vulkan");
        return channel;
    }

    bool Check(VkResult result, const char *what)
    {
        if (result >= 0)
            return true;
        if (const char *name = result_name(result))
            LMAO_LOG_ERROR(LogChannel(), "{} failed: {}", what, name);
        else
            LMAO_LOG_ERROR(LogChannel(), "{} failed: VkResult {}", what, static_cast<int>(result));
        return false;
    }

    std::string VersionToString(uint32_t version)
    {
        return fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                           VK_API_VERSION_PATCH(version));
    }

    bool HasExtension(std::span<const VkExtensionProperties> extensions, const char *name)
    {
        for (const VkExtensionProperties &extension : extensions)
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        return false;
    }

    namespace
    {
        struct FormatPair
        {
            Format rhi;
            VkFormat vk;
        };

        constexpr FormatPair format_pairs[] = {
            {Format::RGBA8Unorm, VK_FORMAT_R8G8B8A8_UNORM},
            {Format::RGBA8Srgb, VK_FORMAT_R8G8B8A8_SRGB},
            {Format::BGRA8Unorm, VK_FORMAT_B8G8R8A8_UNORM},
            {Format::BGRA8Srgb, VK_FORMAT_B8G8R8A8_SRGB},
            {Format::RGB10A2Unorm, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
            {Format::RGBA16Float, VK_FORMAT_R16G16B16A16_SFLOAT},
            {Format::D32Float, VK_FORMAT_D32_SFLOAT},
        };
    } // namespace

    VkFormat ToVkFormat(Format format)
    {
        for (const FormatPair &pair : format_pairs)
            if (pair.rhi == format)
                return pair.vk;
        return VK_FORMAT_UNDEFINED;
    }

    Format FromVkFormat(VkFormat format)
    {
        for (const FormatPair &pair : format_pairs)
            if (pair.vk == format)
                return pair.rhi;
        return Format::Unknown;
    }

} // namespace Lmao::Rhi::Vulkan
