// VulkanDevice: the binding convention (docs/decisions/0010-bindless-descriptors.md).
//
//   set 0  bindless arrays, one descriptor per resource at its handle index:
//            binding 0  sampled images   (Texture2D[])
//            binding 1  storage images   (RWTexture2D<float4>[])
//            binding 2  storage buffers  (RWByteAddressBuffer[])
//            binding 3  samplers         (SamplerState[])
//   set 1  per-frame uniform buffer, one set per frame in flight
//   push constants: 128 bytes, all stages

#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include "core/assert/assert.h"

#include <algorithm>
#include <array>
#include <vector>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        constexpr uint32_t sampled_image_binding = 0;
        constexpr uint32_t storage_image_binding = 1;
        constexpr uint32_t storage_buffer_binding = 2;
        constexpr uint32_t sampler_binding = 3;

        // What a stale or invalid buffer index reads. Debug: quiet NaNs, which spread through any
        // math and stand out in RenderDoc and shader debuggers (read as an integer index it is
        // huge, which GPU-assisted validation reports). Release: zeros, the safest garbage.
#if defined(NDEBUG)
        constexpr uint32_t fallback_buffer_word = 0;
        constexpr const char *fallback_buffer_name = "Fallback buffer (zeros)";
#else
        constexpr uint32_t fallback_buffer_word = 0x7FC00000u; // quiet NaN as float
        constexpr const char *fallback_buffer_name = "Fallback buffer (NaN)";
#endif

        uint32_t clamp_capacity(uint32_t wanted, uint32_t set_limit, uint32_t stage_limit)
        {
            return std::min({wanted, set_limit, stage_limit});
        }

    } // namespace

    Texture &VulkanDevice::GetTexture(TextureHandle handle)
    {
        Texture &texture = mTextures[handle];
        LMAO_ASSERT(!texture.Destroyed, "texture (index {}) used after DestroyTexture", handle.Index);
        return texture;
    }

    Buffer &VulkanDevice::GetBuffer(BufferHandle handle)
    {
        Buffer &buffer = mBuffers[handle];
        LMAO_ASSERT(!buffer.Destroyed, "buffer (index {}) used after DestroyBuffer", handle.Index);
        return buffer;
    }

    bool VulkanDevice::create_descriptors(const VkPhysicalDeviceVulkan12Properties &limits)
    {
        // --- Capacities ---
        BindlessCapacity &capacity = mBindlessCapacity;
        capacity.SampledImages = clamp_capacity(capacity.SampledImages, limits.maxDescriptorSetUpdateAfterBindSampledImages,
                                                limits.maxPerStageDescriptorUpdateAfterBindSampledImages);
        capacity.StorageImages = clamp_capacity(capacity.StorageImages, limits.maxDescriptorSetUpdateAfterBindStorageImages,
                                                limits.maxPerStageDescriptorUpdateAfterBindStorageImages);
        capacity.StorageBuffers = clamp_capacity(capacity.StorageBuffers, limits.maxDescriptorSetUpdateAfterBindStorageBuffers,
                                                 limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
        capacity.Samplers = clamp_capacity(capacity.Samplers, limits.maxDescriptorSetUpdateAfterBindSamplers,
                                           limits.maxPerStageDescriptorUpdateAfterBindSamplers);
        const uint64_t total = uint64_t(capacity.SampledImages) + capacity.StorageImages + capacity.StorageBuffers +
                               capacity.Samplers;
        if (total > limits.maxPerStageUpdateAfterBindResources)
        {
            LMAO_LOG_ERROR(LogChannel(), "Bindless set needs {} descriptors per stage; the GPU allows {}", total,
                           limits.maxPerStageUpdateAfterBindResources);
            return false;
        }
        LMAO_LOG_INFO(LogChannel(), "Bindless capacity: {} textures, {} storage images, {} storage buffers, {} samplers",
                      capacity.SampledImages, capacity.StorageImages, capacity.StorageBuffers, capacity.Samplers);

        // --- Set 0: bindless ---
        const std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
            {sampled_image_binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, capacity.SampledImages, VK_SHADER_STAGE_ALL, nullptr},
            {storage_image_binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, capacity.StorageImages, VK_SHADER_STAGE_ALL, nullptr},
            {storage_buffer_binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, capacity.StorageBuffers, VK_SHADER_STAGE_ALL, nullptr},
            {sampler_binding, VK_DESCRIPTOR_TYPE_SAMPLER, capacity.Samplers, VK_SHADER_STAGE_ALL, nullptr},
        }};
        // Partially bound: unused slots may hold nothing (or a destroyed resource).
        // Update after bind + unused while pending: new resources get descriptors while earlier
        // frames using other slots of the same set are still on the GPU.
        constexpr VkDescriptorBindingFlags binding_flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                                          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        const std::array<VkDescriptorBindingFlags, 4> binding_flags{binding_flag, binding_flag, binding_flag, binding_flag};
        const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(binding_flags.size()),
            .pBindingFlags = binding_flags.data(),
        };
        const VkDescriptorSetLayoutCreateInfo bindless_layout_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &flags_info,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        if (!Check(vkCreateDescriptorSetLayout(mDevice, &bindless_layout_info, nullptr, &mBindlessLayout),
                   "vkCreateDescriptorSetLayout (bindless)"))
            return false;
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mBindlessLayout, "Set 0 layout (bindless)");

        const std::array<VkDescriptorPoolSize, 4> bindless_sizes{{
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, capacity.SampledImages},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, capacity.StorageImages},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, capacity.StorageBuffers},
            {VK_DESCRIPTOR_TYPE_SAMPLER, capacity.Samplers},
        }};
        const VkDescriptorPoolCreateInfo bindless_pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(bindless_sizes.size()),
            .pPoolSizes = bindless_sizes.data(),
        };
        if (!Check(vkCreateDescriptorPool(mDevice, &bindless_pool_info, nullptr, &mBindlessPool),
                   "vkCreateDescriptorPool (bindless)"))
            return false;
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mBindlessPool, "Bindless descriptor pool");

        const VkDescriptorSetAllocateInfo bindless_alloc{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = mBindlessPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mBindlessLayout,
        };
        if (!Check(vkAllocateDescriptorSets(mDevice, &bindless_alloc, &mBindlessSet), "vkAllocateDescriptorSets (bindless)"))
            return false;
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_SET, mBindlessSet, "Set 0 (bindless)");

        // --- Set 1: per-frame constants ---
        const VkDescriptorSetLayoutBinding frame_binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
        const VkDescriptorSetLayoutCreateInfo frame_layout_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &frame_binding,
        };
        if (!Check(vkCreateDescriptorSetLayout(mDevice, &frame_layout_info, nullptr, &mFrameLayout),
                   "vkCreateDescriptorSetLayout (frame)"))
            return false;
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mFrameLayout, "Set 1 layout (per frame)");

        const VkDescriptorPoolSize frame_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight};
        const VkDescriptorPoolCreateInfo frame_pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MaxFramesInFlight,
            .poolSizeCount = 1,
            .pPoolSizes = &frame_size,
        };
        if (!Check(vkCreateDescriptorPool(mDevice, &frame_pool_info, nullptr, &mFramePool), "vkCreateDescriptorPool (frame)"))
            return false;
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mFramePool, "Per-frame descriptor pool");
        return true;
    }

    bool VulkanDevice::submit_immediately(const std::function<void(VkCommandBuffer)> &record)
    {
        const VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = mGraphicsQueueFamily,
        };
        VkCommandPool pool = VK_NULL_HANDLE;
        if (!Check(vkCreateCommandPool(mDevice, &pool_info, nullptr, &pool), "vkCreateCommandPool (immediate)"))
            return false;

        const VkCommandBufferAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        bool ok = Check(vkAllocateCommandBuffers(mDevice, &allocate_info, &command_buffer), "vkAllocateCommandBuffers");
        if (ok)
        {
            const VkCommandBufferBeginInfo begin_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            vkBeginCommandBuffer(command_buffer, &begin_info);
            record(command_buffer);
            vkEndCommandBuffer(command_buffer);

            const VkCommandBufferSubmitInfo command_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = command_buffer,
            };
            const VkSubmitInfo2 submit{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &command_info,
            };
            ok = Check(vkQueueSubmit2(mGraphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2 (immediate)") &&
                 Check(vkQueueWaitIdle(mGraphicsQueue), "vkQueueWaitIdle");
        }
        vkDestroyCommandPool(mDevice, pool, nullptr);
        return ok;
    }

    bool VulkanDevice::create_fallbacks()
    {
        // Created first, so each gets index 0 of its array.
        mFallbackTexture = CreateTexture({
            .Width = 1,
            .Height = 1,
            .Format = Format::RGBA8Unorm, // storage support for RGBA8 is mandatory in Vulkan
            .Usage = TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::CopyDest,
            .DebugName = "Fallback texture (magenta)",
        });
        mFallbackBuffer = CreateBuffer({
            .Size = 64 * 1024,
            .Usage = BufferUsage::Storage | BufferUsage::CopyDest,
            .DebugName = fallback_buffer_name,
        });
        mFallbackSampler = CreateSampler({.DebugName = "Fallback sampler"});
        if (!mFallbackTexture || !mFallbackBuffer || !mFallbackSampler)
            return false;
        LMAO_ASSERT(mFallbackTexture.Index == 0 && mFallbackBuffer.Index == 0 && mFallbackSampler.Index == 0,
                    "fallback resources must be the first of their kind");

        // Magenta texture in GENERAL (valid for both sampling and storage), zeroed buffer.
        Texture &texture = mTextures[mFallbackTexture];
        const Buffer &buffer = mBuffers[mFallbackBuffer];
        const bool ok = submit_immediately([&](VkCommandBuffer command_buffer)
                                           {
            const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
            VkImageMemoryBarrier2 image_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = texture.Image,
                .subresourceRange = range,
            };
            VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &image_barrier,
            };
            vkCmdPipelineBarrier2(command_buffer, &dependency);

            const VkClearColorValue magenta{.float32 = {1.0f, 0.0f, 1.0f, 1.0f}};
            vkCmdClearColorImage(command_buffer, texture.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &magenta, 1, &range);
            vkCmdFillBuffer(command_buffer, buffer.Handle, 0, VK_WHOLE_SIZE, fallback_buffer_word);

            image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            const VkMemoryBarrier2 buffer_barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            };
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &buffer_barrier;
            vkCmdPipelineBarrier2(command_buffer, &dependency); });
        if (!ok)
            return false;
        texture.Layout = VK_IMAGE_LAYOUT_GENERAL;

        point_slots_at_fallback(sampled_image_binding, 0, mBindlessCapacity.SampledImages);
        point_slots_at_fallback(storage_image_binding, 0, mBindlessCapacity.StorageImages);
        point_slots_at_fallback(storage_buffer_binding, 0, mBindlessCapacity.StorageBuffers);
        point_slots_at_fallback(sampler_binding, 0, mBindlessCapacity.Samplers);
        return true;
    }

    void VulkanDevice::point_slots_at_fallback(uint32_t binding, uint32_t first, uint32_t count)
    {
        if (count == 0 || mShuttingDown)
            return;
        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mBindlessSet,
            .dstBinding = binding,
            .dstArrayElement = first,
            .descriptorCount = count,
        };
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkDescriptorBufferInfo> buffers;
        switch (binding)
        {
        case sampled_image_binding:
        case storage_image_binding:
            images.assign(count, {VK_NULL_HANDLE, mTextures[mFallbackTexture].View, VK_IMAGE_LAYOUT_GENERAL});
            write.descriptorType = binding == sampled_image_binding ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                                    : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo = images.data();
            break;
        case storage_buffer_binding:
            buffers.assign(count, {mBuffers[mFallbackBuffer].Handle, 0, VK_WHOLE_SIZE});
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = buffers.data();
            break;
        case sampler_binding:
            images.assign(count, {mSamplers[mFallbackSampler].Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.pImageInfo = images.data();
            break;
        default:
            return;
        }
        vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
    }

    bool VulkanDevice::create_frame_constants(Frame &frame, uint32_t index)
    {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = MaxFrameConstantsSize,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        // Written by the CPU every frame and read by the GPU: device-local host-visible if the
        // GPU has it (ReBAR / integrated), system memory otherwise.
        const VmaAllocationCreateInfo allocation_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        VmaAllocationInfo mapping{};
        if (!Check(vmaCreateBuffer(mAllocator, &buffer_info, &allocation_info, &frame.constants_buffer,
                                   &frame.constants_allocation, &mapping),
                   "vmaCreateBuffer (frame constants)"))
            return false;
        frame.constants_data = static_cast<std::byte *>(mapping.pMappedData);
        std::fill_n(frame.constants_data, MaxFrameConstantsSize, std::byte{0});

        const VkDescriptorSetAllocateInfo set_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = mFramePool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mFrameLayout,
        };
        if (!Check(vkAllocateDescriptorSets(mDevice, &set_info, &frame.frame_set), "vkAllocateDescriptorSets (frame)"))
            return false;

        // Written once: the set always points at this frame slot's buffer.
        const VkDescriptorBufferInfo descriptor{frame.constants_buffer, 0, MaxFrameConstantsSize};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frame.frame_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &descriptor,
        };
        vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);

        const std::string prefix = fmt::format("Frame {}", index);
        SetName(VK_OBJECT_TYPE_BUFFER, frame.constants_buffer, (prefix + " constants").c_str());
        vmaSetAllocationName(mAllocator, frame.constants_allocation, (prefix + " constants").c_str());
        SetName(VK_OBJECT_TYPE_DESCRIPTOR_SET, frame.frame_set, (prefix + " set 1").c_str());
        return true;
    }

    bool VulkanDevice::write_texture_descriptors(TextureHandle handle, const Texture &texture, const std::string &name)
    {
        const bool sampled = texture.Usage & VK_IMAGE_USAGE_SAMPLED_BIT;
        const bool storage = texture.Usage & VK_IMAGE_USAGE_STORAGE_BIT;
        if ((sampled && handle.Index >= mBindlessCapacity.SampledImages) ||
            (storage && handle.Index >= mBindlessCapacity.StorageImages))
        {
            LMAO_LOG_ERROR(LogChannel(), "Texture '{}': descriptor index {} is past the bindless capacity", name, handle.Index);
            return false;
        }

        // Sampled images are read in SHADER_READ_ONLY; storage images live in GENERAL.
        const VkDescriptorImageInfo sampled_info{VK_NULL_HANDLE, texture.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo storage_info{VK_NULL_HANDLE, texture.View, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2]{};
        uint32_t count = 0;
        if (sampled)
            writes[count++] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = mBindlessSet,
                .dstBinding = sampled_image_binding,
                .dstArrayElement = handle.Index,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &sampled_info,
            };
        if (storage)
            writes[count++] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = mBindlessSet,
                .dstBinding = storage_image_binding,
                .dstArrayElement = handle.Index,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &storage_info,
            };
        if (count != 0)
            vkUpdateDescriptorSets(mDevice, count, writes, 0, nullptr);
        return true;
    }

    bool VulkanDevice::write_buffer_descriptor(BufferHandle handle, const Buffer &buffer, const std::string &name)
    {
        if (!(buffer.Usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return true; // only storage buffers are in the bindless set
        if (handle.Index >= mBindlessCapacity.StorageBuffers)
        {
            LMAO_LOG_ERROR(LogChannel(), "Buffer '{}': descriptor index {} is past the bindless capacity ({})", name,
                           handle.Index, mBindlessCapacity.StorageBuffers);
            return false;
        }
        const VkDescriptorBufferInfo info{buffer.Handle, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mBindlessSet,
            .dstBinding = storage_buffer_binding,
            .dstArrayElement = handle.Index,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &info,
        };
        vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
        return true;
    }

    bool VulkanDevice::write_sampler_descriptor(SamplerHandle handle, VkSampler sampler, const std::string &name)
    {
        if (handle.Index >= mBindlessCapacity.Samplers)
        {
            LMAO_LOG_ERROR(LogChannel(), "Sampler '{}': descriptor index {} is past the bindless capacity ({})", name,
                           handle.Index, mBindlessCapacity.Samplers);
            return false;
        }
        const VkDescriptorImageInfo info{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mBindlessSet,
            .dstBinding = sampler_binding,
            .dstArrayElement = handle.Index,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = &info,
        };
        vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
        return true;
    }

} // namespace Lmao::Rhi::Vulkan
