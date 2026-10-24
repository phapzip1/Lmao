// VkPipelineCache persisted to disk: loaded in Initialize, saved when the device is destroyed.

#include "core/io/path.h"
#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        double to_kib(size_t bytes) { return static_cast<double>(bytes) / 1024.0; }

        std::vector<std::byte> read_file(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return {};
            const std::streamoff size = file.tellg();
            if (size <= 0)
                return {};
            std::vector<std::byte> data(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char *>(data.data()), size))
                return {};
            return data;
        }

        // Why the data can't be used with this device, or "" if the header matches. Drivers are
        // supposed to reject foreign data themselves, but some crash on it instead, and a check
        // here lets us say why the cache was dropped.
        std::string validate_header(const std::vector<std::byte> &data, const VkPhysicalDeviceProperties &properties)
        {
            VkPipelineCacheHeaderVersionOne header{};
            if (data.size() < sizeof(header))
                return fmt::format("{} bytes is smaller than the header", data.size());
            std::memcpy(&header, data.data(), sizeof(header));
            if (header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
                return fmt::format("unknown header version {}", static_cast<int>(header.headerVersion));
            if (header.headerSize < sizeof(header) || header.headerSize > data.size())
                return fmt::format("bad header size {}", header.headerSize);
            if (header.vendorID != properties.vendorID || header.deviceID != properties.deviceID)
                return fmt::format("made for GPU {:04x}:{:04x}, this is {:04x}:{:04x}", header.vendorID, header.deviceID,
                                   properties.vendorID, properties.deviceID);
            if (std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
                return "pipeline cache UUID changed (new driver?)";
            return {};
        }

    } // namespace

    bool VulkanDevice::create_pipeline_cache(std::string_view directory, const VkPhysicalDeviceProperties &properties)
    {
        std::vector<std::byte> initial;
        if (directory.empty())
            LMAO_LOG_INFO(LogChannel(), "Pipeline cache: no directory, not persisted");
        else
        {
            // One file per GPU so switching GPUs (laptop iGPU/dGPU) doesn't throw the other cache away.
            mPipelineCachePath = PathFromUtf8(directory) / fmt::format("pipeline_cache_{:04x}_{:04x}.bin",
                                                                       properties.vendorID, properties.deviceID);
            initial = read_file(mPipelineCachePath);
            if (initial.empty())
                LMAO_LOG_INFO(LogChannel(), "Pipeline cache: none at '{}', starting empty", PathToUtf8(mPipelineCachePath));
            else if (const std::string problem = validate_header(initial, properties); !problem.empty())
            {
                LMAO_LOG_WARN(LogChannel(), "Pipeline cache: discarding '{}' ({:.1f} KiB): {}", PathToUtf8(mPipelineCachePath),
                              to_kib(initial.size()), problem);
                initial.clear();
            }
        }

        VkPipelineCacheCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .initialDataSize = initial.size(),
            .pInitialData = initial.empty() ? nullptr : initial.data(),
        };
        VkResult result = vkCreatePipelineCache(mDevice, &info, nullptr, &mPipelineCache);
        if (result != VK_SUCCESS && !initial.empty())
        {
            LMAO_LOG_WARN(LogChannel(), "Pipeline cache: the driver rejected the data (VkResult {}), starting empty",
                          static_cast<int>(result));
            initial.clear();
            info.initialDataSize = 0;
            info.pInitialData = nullptr;
            result = vkCreatePipelineCache(mDevice, &info, nullptr, &mPipelineCache);
        }
        if (!Check(result, "vkCreatePipelineCache"))
            return false;
        SetName(VK_OBJECT_TYPE_PIPELINE_CACHE, mPipelineCache, "Pipeline cache");
        if (!initial.empty())
            LMAO_LOG_INFO(LogChannel(), "Pipeline cache: loaded {:.1f} KiB from '{}'", to_kib(initial.size()),
                          PathToUtf8(mPipelineCachePath));
        return true;
    }

    void VulkanDevice::save_pipeline_cache()
    {
        if (mPipelineCache == VK_NULL_HANDLE)
            return;
        const uint32_t hits = mPipelineCacheHits.load();
        const uint32_t misses = mPipelineCacheMisses.load();
        LMAO_LOG_INFO(LogChannel(), "Pipeline cache: {} of {} pipeline(s) came from the cache", hits, hits + misses);
        if (mPipelineCachePath.empty())
            return;

        size_t size = 0;
        if (!Check(vkGetPipelineCacheData(mDevice, mPipelineCache, &size, nullptr), "vkGetPipelineCacheData (size)") ||
            size == 0)
            return;
        std::vector<std::byte> data(size);
        // VK_INCOMPLETE can't happen with the size just queried, but treat it as a failure anyway.
        if (vkGetPipelineCacheData(mDevice, mPipelineCache, &size, data.data()) != VK_SUCCESS)
        {
            LMAO_LOG_WARN(LogChannel(), "Pipeline cache: vkGetPipelineCacheData failed, not saved");
            return;
        }

        // Write a temporary file and rename it over the old one, so a crash mid-write never
        // leaves a truncated cache behind.
        std::error_code error;
        std::filesystem::create_directories(mPipelineCachePath.parent_path(), error);
        std::filesystem::path temporary = mPipelineCachePath;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(size)))
            {
                LMAO_LOG_WARN(LogChannel(), "Pipeline cache: can't write '{}'", PathToUtf8(temporary));
                return;
            }
        }
        std::filesystem::rename(temporary, mPipelineCachePath, error);
        if (error)
        {
            LMAO_LOG_WARN(LogChannel(), "Pipeline cache: can't replace '{}': {}", PathToUtf8(mPipelineCachePath),
                          error.message());
            std::filesystem::remove(temporary, error);
            return;
        }
        LMAO_LOG_INFO(LogChannel(), "Pipeline cache: saved {:.1f} KiB to '{}'", to_kib(size), PathToUtf8(mPipelineCachePath));
    }

} // namespace Lmao::Rhi::Vulkan
