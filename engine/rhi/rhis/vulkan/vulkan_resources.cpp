// VulkanDevice: buffers, textures and samplers, allocated through VMA.

#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include "core/assert/assert.h"
#include "core/profile/profile.h"

#include <algorithm>
#include <string>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        VkBufferUsageFlags to_vk(BufferUsage usage)
        {
            VkBufferUsageFlags flags = 0;
            if (HasAny(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (HasAny(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (HasAny(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (HasAny(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (HasAny(usage, BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            if (HasAny(usage, BufferUsage::CopySource)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (HasAny(usage, BufferUsage::CopyDest)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            return flags;
        }

        VkImageUsageFlags to_vk(TextureUsage usage)
        {
            VkImageUsageFlags flags = 0;
            if (HasAny(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (HasAny(usage, TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (HasAny(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (HasAny(usage, TextureUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (HasAny(usage, TextureUsage::CopySource)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (HasAny(usage, TextureUsage::CopyDest)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            return flags;
        }

        // Format features the GPU must support (optimal tiling) for each usage.
        VkFormatFeatureFlags required_features(TextureUsage usage)
        {
            VkFormatFeatureFlags features = 0;
            if (HasAny(usage, TextureUsage::Sampled)) features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if (HasAny(usage, TextureUsage::Storage)) features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
            if (HasAny(usage, TextureUsage::ColorAttachment)) features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
            if (HasAny(usage, TextureUsage::DepthAttachment)) features |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (HasAny(usage, TextureUsage::CopySource)) features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
            if (HasAny(usage, TextureUsage::CopyDest)) features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            return features;
        }

        VkFilter to_vk(Filter filter) { return filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

        VkSamplerMipmapMode to_vk_mip(Filter filter)
        {
            return filter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        VkSamplerAddressMode to_vk(AddressMode mode)
        {
            switch (mode)
            {
            case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        const char *memory_name(MemoryUsage memory)
        {
            switch (memory)
            {
            case MemoryUsage::GpuOnly: return "GPU only";
            case MemoryUsage::Upload: return "upload";
            case MemoryUsage::Readback: return "readback";
            }
            return "?";
        }

        std::string name_or(std::string_view name, const char *fallback)
        {
            return name.empty() ? std::string(fallback) : std::string(name);
        }

    } // namespace

    // --- Buffers -------------------------------------------------------------------------

    BufferHandle VulkanDevice::CreateBuffer(const BufferDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        const std::string name = name_or(desc.DebugName, "Unnamed buffer");
        if (desc.Size == 0 || desc.Usage == BufferUsage::None)
        {
            LMAO_LOG_ERROR(LogChannel(), "Buffer '{}': size and usage must be non-zero", name);
            return {};
        }

        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.Size,
            // Every buffer can be addressed from shaders (bufferDeviceAddress is a required feature).
            .usage = to_vk(desc.Usage) | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        // Mappable memory is required to be coherent, so callers never flush or invalidate.
        // Every desktop GPU has coherent host-visible memory.
        VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        switch (desc.Memory)
        {
        case MemoryUsage::GpuOnly:
            break;
        case MemoryUsage::Upload:
            allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
            allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case MemoryUsage::Readback:
            allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
            allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        }

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo info{};
        if (!Check(vmaCreateBuffer(mAllocator, &buffer_info, &allocation_info, &buffer, &allocation, &info), "vmaCreateBuffer"))
        {
            LMAO_LOG_ERROR(LogChannel(), "Failed to create buffer '{}' ({} bytes, {})", name, desc.Size, memory_name(desc.Memory));
            return {};
        }

        vmaSetAllocationName(mAllocator, allocation, name.c_str());
        SetName(VK_OBJECT_TYPE_BUFFER, buffer, name.c_str());
        const BufferHandle handle = mBuffers.Insert({
            .Handle = buffer,
            .Allocation = allocation,
            .Size = desc.Size,
            .Usage = buffer_info.usage,
            .Mapped = info.pMappedData,
        });
        if (!write_buffer_descriptor(handle, mBuffers[handle], name))
        {
            vmaDestroyBuffer(mAllocator, buffer, allocation); // never used by the GPU
            mBuffers.Remove(handle);
            return {};
        }
        LMAO_LOG_DEBUG(LogChannel(), "Created buffer '{}' ({} bytes, {}, index {})", name, desc.Size,
                       memory_name(desc.Memory), handle.Index);
        return handle;
    }

    void VulkanDevice::DestroyBuffer(BufferHandle handle)
    {
        Buffer *buffer = mBuffers.Get(handle);
        LMAO_ASSERT(buffer && !buffer->Destroyed, "DestroyBuffer: stale, invalid or already destroyed handle");
        if (!buffer || buffer->Destroyed)
            return;
        buffer->Destroyed = true;
        drop_queued(handle, {});
        defer_destroy({.buffer = buffer->Handle, .allocation = buffer->Allocation, .buffer_slot = handle});
    }

    void *VulkanDevice::GetMappedData(BufferHandle handle)
    {
        const Buffer *buffer = mBuffers.Get(handle);
        LMAO_ASSERT(buffer && !buffer->Destroyed, "GetMappedData: stale, invalid or destroyed handle");
        return buffer && !buffer->Destroyed ? buffer->Mapped : nullptr;
    }

    // --- Textures ------------------------------------------------------------------------

    TextureHandle VulkanDevice::CreateTexture(const TextureDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        const std::string name = name_or(desc.DebugName, "Unnamed texture");

        const VkFormat format = ToVkFormat(desc.Format);
        if (format == VK_FORMAT_UNDEFINED || desc.Usage == TextureUsage::None)
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': needs a known format and a usage (format {})", name,
                           ToString(desc.Format));
            return {};
        }
        if (desc.Width == 0 || desc.Height == 0 || desc.Width > mLimits.maxImageDimension2D ||
            desc.Height > mLimits.maxImageDimension2D)
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': size {}x{} is outside 1..{}", name, desc.Width, desc.Height,
                           mLimits.maxImageDimension2D);
            return {};
        }

        const bool depth = IsDepthFormat(desc.Format);
        if (HasAny(desc.Usage, depth ? TextureUsage::ColorAttachment : TextureUsage::DepthAttachment))
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': {} can't be used as a {} attachment", name, ToString(desc.Format),
                           depth ? "color" : "depth");
            return {};
        }

        const uint32_t full_chain = FullMipCount(desc.Width, desc.Height);
        const uint32_t mip_levels = desc.MipLevels == 0 ? full_chain : desc.MipLevels;
        if (mip_levels > full_chain)
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': {} mips requested, {}x{} has at most {}", name, mip_levels,
                           desc.Width, desc.Height, full_chain);
            return {};
        }

        // E.g. storage images in an sRGB format are unsupported almost everywhere.
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &format_properties);
        const VkFormatFeatureFlags needed = required_features(desc.Usage);
        if ((format_properties.optimalTilingFeatures & needed) != needed)
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': this GPU doesn't support format {} for the requested usage", name,
                           ToString(desc.Format));
            return {};
        }

        const VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {desc.Width, desc.Height, 1},
            .mipLevels = mip_levels,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = to_vk(desc.Usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        // Render targets are large and get recreated on resize: give them their own memory block.
        if (HasAny(desc.Usage, TextureUsage::ColorAttachment | TextureUsage::DepthAttachment))
            allocation_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (!Check(vmaCreateImage(mAllocator, &image_info, &allocation_info, &image, &allocation, nullptr), "vmaCreateImage"))
        {
            LMAO_LOG_ERROR(LogChannel(), "Failed to create texture '{}' ({}x{}, {})", name, desc.Width, desc.Height,
                           ToString(desc.Format));
            return {};
        }

        const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        const VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {.aspectMask = aspect, .levelCount = mip_levels, .layerCount = 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        if (!Check(vkCreateImageView(mDevice, &view_info, nullptr, &view), "vkCreateImageView"))
        {
            vmaDestroyImage(mAllocator, image, allocation);
            return {};
        }

        vmaSetAllocationName(mAllocator, allocation, name.c_str());
        SetName(VK_OBJECT_TYPE_IMAGE, image, name.c_str());
        SetName(VK_OBJECT_TYPE_IMAGE_VIEW, view, (name + " view").c_str());
        const TextureHandle handle = mTextures.Insert({
            .Image = image,
            .View = view,
            .Format = format,
            .Extent = {desc.Width, desc.Height},
            .MipLevels = mip_levels,
            .Aspect = aspect,
            .Layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .Usage = image_info.usage,
            .Allocation = allocation,
        });
        if (!write_texture_descriptors(handle, mTextures[handle], name))
        {
            vkDestroyImageView(mDevice, view, nullptr); // never used by the GPU
            vmaDestroyImage(mAllocator, image, allocation);
            mTextures.Remove(handle);
            return {};
        }
        LMAO_LOG_DEBUG(LogChannel(), "Created texture '{}' ({}x{}, {} mips, {}, index {})", name, desc.Width, desc.Height,
                       mip_levels, ToString(desc.Format), handle.Index);
        return handle;
    }

    void VulkanDevice::DestroyTexture(TextureHandle handle)
    {
        Texture *texture = mTextures.Get(handle);
        LMAO_ASSERT(texture && !texture->Destroyed, "DestroyTexture: stale, invalid or already destroyed handle");
        if (!texture || texture->Destroyed)
            return;
        LMAO_ASSERT(texture->Allocation, "DestroyTexture: swapchain images are destroyed by their swapchain");
        if (!texture->Allocation)
            return;
        texture->Destroyed = true;
        drop_queued({}, handle);
        defer_destroy({.image = texture->Image, .view = texture->View, .allocation = texture->Allocation,
                       .texture_slot = handle});
    }

    // --- Staging -------------------------------------------------------------------------

    StagingSpace VulkanDevice::AllocateStaging(uint64_t size)
    {
        LMAO_ASSERT(mFrameSwapchain, "staging memory is only available while a frame is being recorded");
        Frame &frame = mFrames[mFrameIndex];

        // 16 covers the texel size of every format and the 4-byte rule for buffer copies.
        const uint64_t alignment = std::max<uint64_t>(16, mLimits.optimalBufferCopyOffsetAlignment);
        const uint64_t offset = (frame.upload_used + alignment - 1) / alignment * alignment;
        if (offset + size <= UploadRingSize)
        {
            frame.upload_used = offset + size;
            return {.Buffer = frame.upload_buffer, .Offset = offset, .Data = frame.upload_data + offset};
        }

        // Too big for what's left of the ring: a one-off buffer, freed once this frame is done.
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo allocation_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo info{};
        if (!Check(vmaCreateBuffer(mAllocator, &buffer_info, &allocation_info, &buffer, &allocation, &info),
                   "vmaCreateBuffer (staging)"))
            return {};
        vmaSetAllocationName(mAllocator, allocation, "One-off staging");
        SetName(VK_OBJECT_TYPE_BUFFER, buffer, "One-off staging");
        LMAO_LOG_DEBUG(LogChannel(), "Upload of {} bytes doesn't fit the upload ring ({} of {} bytes used): "
                                     "using a one-off staging buffer",
                       size, frame.upload_used, UploadRingSize);
        defer_destroy({.buffer = buffer, .allocation = allocation});
        return {.Buffer = buffer, .Offset = 0, .Data = static_cast<std::byte *>(info.pMappedData)};
    }

    // --- Samplers ------------------------------------------------------------------------

    SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc &desc)
    {
        const std::string name = name_or(desc.DebugName, "Unnamed sampler");
        const float anisotropy = std::min(desc.MaxAnisotropy, mLimits.maxSamplerAnisotropy);
        const VkSamplerCreateInfo sampler_info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = to_vk(desc.MagFilter),
            .minFilter = to_vk(desc.MinFilter),
            .mipmapMode = to_vk_mip(desc.MipFilter),
            .addressModeU = to_vk(desc.AddressU),
            .addressModeV = to_vk(desc.AddressV),
            .addressModeW = to_vk(desc.AddressW),
            .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
            .maxAnisotropy = std::max(anisotropy, 1.0f),
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VkSampler sampler = VK_NULL_HANDLE;
        if (!Check(vkCreateSampler(mDevice, &sampler_info, nullptr, &sampler), "vkCreateSampler"))
            return {};
        SetName(VK_OBJECT_TYPE_SAMPLER, sampler, name.c_str());
        const SamplerHandle handle = mSamplers.Insert({sampler});
        if (!write_sampler_descriptor(handle, sampler, name))
        {
            vkDestroySampler(mDevice, sampler, nullptr);
            mSamplers.Remove(handle);
            return {};
        }
        return handle;
    }

    void VulkanDevice::DestroySampler(SamplerHandle handle)
    {
        Sampler *sampler = mSamplers.Get(handle);
        LMAO_ASSERT(sampler && !sampler->Destroyed, "DestroySampler: stale, invalid or already destroyed handle");
        if (!sampler || sampler->Destroyed)
            return;
        sampler->Destroyed = true;
        defer_destroy({.sampler = sampler->Handle, .sampler_slot = handle});
    }

    // --- Statistics ----------------------------------------------------------------------

    MemoryStats VulkanDevice::GetMemoryStats() const
    {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mAllocator, budgets);
        const VkPhysicalDeviceMemoryProperties *memory = nullptr;
        vmaGetMemoryProperties(mAllocator, &memory);

        MemoryStats stats;
        for (uint32_t heap = 0; heap < memory->memoryHeapCount; ++heap)
        {
            const VmaBudget &budget = budgets[heap];
            stats.AllocatedBytes += budget.statistics.allocationBytes;
            stats.ReservedBytes += budget.statistics.blockBytes;
            stats.AllocationCount += budget.statistics.allocationCount;
            if (memory->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                stats.DeviceLocalUsage += budget.usage;
                stats.DeviceLocalBudget += budget.budget;
            }
        }
        return stats;
    }

} // namespace Lmao::Rhi::Vulkan
