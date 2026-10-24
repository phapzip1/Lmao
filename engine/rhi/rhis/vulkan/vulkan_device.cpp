// Vulkan implementation of Rhi::Device: GPU selection, the logical device and the frame loop.

#include "rhi/rhis/vulkan/vulkan_device.h"

#include "core/assert/assert.h"
#include "core/profile/profile.h"
#include "rhi/rhis/vulkan/vulkan_device_internal.h"
#include "rhi/rhis/vulkan/vulkan_instance.h"
#include "rhi/rhis/vulkan/vulkan_swapchain.h"

#include <atomic>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        // volk loads device functions into globals, so only one device may exist at a time.
        std::atomic<bool> device_exists{false};

        // Feature structs queried from (and enabled on) the device. pNext points into the
        // object itself, so it can't be copied.
        struct FeatureChain
        {
            VkPhysicalDeviceFeatures2 Features2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            VkPhysicalDeviceVulkan11Features Vulkan11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            VkPhysicalDeviceVulkan12Features Vulkan12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceVulkan13Features Vulkan13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

            FeatureChain()
            {
                Features2.pNext = &Vulkan11;
                Vulkan11.pNext = &Vulkan12;
                Vulkan12.pNext = &Vulkan13;
            }

            FeatureChain(const FeatureChain &) = delete;
            FeatureChain &operator=(const FeatureChain &) = delete;
        };

        // Every feature the engine requires (docs/decisions/0003). Used both to check a GPU and
        // to enable the features on the device, so the two lists can't drift apart.
        template <typename Fn>
        void for_each_required_feature(FeatureChain &chain, Fn &&fn)
        {
            fn("dynamicRendering", chain.Vulkan13.dynamicRendering);
            fn("synchronization2", chain.Vulkan13.synchronization2);
            // Slang lowers `discard` to SPIR-V 1.6 demote (alpha-tested materials); mandatory in 1.3
            fn("shaderDemoteToHelperInvocation", chain.Vulkan13.shaderDemoteToHelperInvocation);
            fn("timelineSemaphore", chain.Vulkan12.timelineSemaphore);
            fn("bufferDeviceAddress", chain.Vulkan12.bufferDeviceAddress);
            fn("samplerAnisotropy", chain.Features2.features.samplerAnisotropy);
            // The bindless buffer array is read-write (RWByteAddressBuffer) in every stage
            fn("vertexPipelineStoresAndAtomics", chain.Features2.features.vertexPipelineStoresAndAtomics);
            fn("fragmentStoresAndAtomics", chain.Features2.features.fragmentStoresAndAtomics);
            // Slang lowers SV_VertexID to gl_VertexIndex - gl_BaseVertex; also SV_DrawIndex later
            fn("shaderDrawParameters", chain.Vulkan11.shaderDrawParameters);
            // Bindless descriptors
            fn("descriptorIndexing", chain.Vulkan12.descriptorIndexing);
            fn("runtimeDescriptorArray", chain.Vulkan12.runtimeDescriptorArray);
            fn("descriptorBindingPartiallyBound", chain.Vulkan12.descriptorBindingPartiallyBound);
            fn("descriptorBindingSampledImageUpdateAfterBind", chain.Vulkan12.descriptorBindingSampledImageUpdateAfterBind);
            fn("descriptorBindingStorageImageUpdateAfterBind", chain.Vulkan12.descriptorBindingStorageImageUpdateAfterBind);
            fn("descriptorBindingStorageBufferUpdateAfterBind", chain.Vulkan12.descriptorBindingStorageBufferUpdateAfterBind);
            fn("descriptorBindingUpdateUnusedWhilePending", chain.Vulkan12.descriptorBindingUpdateUnusedWhilePending);
            fn("shaderSampledImageArrayNonUniformIndexing", chain.Vulkan12.shaderSampledImageArrayNonUniformIndexing);
            fn("shaderStorageImageArrayNonUniformIndexing", chain.Vulkan12.shaderStorageImageArrayNonUniformIndexing);
            fn("shaderStorageBufferArrayNonUniformIndexing", chain.Vulkan12.shaderStorageBufferArrayNonUniformIndexing);
        }

        struct Adapter
        {
            VkPhysicalDevice Physical = VK_NULL_HANDLE;
            VkPhysicalDeviceProperties Properties{};
            uint32_t QueueFamily = 0;
            uint64_t LocalMemory = 0;
        };

        AdapterType to_adapter_type(VkPhysicalDeviceType type)
        {
            switch (type)
            {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return AdapterType::Discrete;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return AdapterType::Integrated;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return AdapterType::Virtual;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: return AdapterType::Cpu;
            default: return AdapterType::Other;
            }
        }

        uint64_t device_local_memory(VkPhysicalDevice physical)
        {
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(physical, &memory);
            uint64_t total = 0;
            for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
                if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    total += memory.memoryHeaps[i].size;
            return total;
        }

        double to_gib(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); }

        // Empty if the engine can run on this GPU, otherwise the reason it can't.
        // On success, queue_family is a family with graphics and compute.
        std::string check_adapter(VkPhysicalDevice physical, const VkPhysicalDeviceProperties &properties,
                                  uint32_t &queue_family)
        {
            // Checked first: querying 1.3 feature structs on an older GPU is invalid.
            if (properties.apiVersion < VK_API_VERSION_1_3)
                return fmt::format("supports Vulkan {}, 1.3 is required", VersionToString(properties.apiVersion));

            FeatureChain supported;
            vkGetPhysicalDeviceFeatures2(physical, &supported.Features2);
            std::string missing;
            for_each_required_feature(supported, [&missing](const char *name, VkBool32 &value)
                                      {
                                          if (value)
                                              return;
                                          if (!missing.empty())
                                              missing += ", ";
                                          missing += name;
                                      });
            if (!missing.empty())
                return "missing features: " + missing;

            uint32_t extension_count = 0;
            vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr);
            std::vector<VkExtensionProperties> extensions(extension_count);
            vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, extensions.data());
            if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                return "missing extension " VK_KHR_SWAPCHAIN_EXTENSION_NAME;

            uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
            constexpr VkQueueFlags graphics_compute = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            for (uint32_t i = 0; i < family_count; ++i)
            {
                if ((families[i].queueFlags & graphics_compute) == graphics_compute)
                {
                    queue_family = i;
                    return {};
                }
            }
            return "no queue family with graphics and compute";
        }

        // Higher is better: discrete before integrated before the rest, then more GPU memory.
        uint64_t score(const Adapter &adapter)
        {
            uint64_t rank = 0;
            switch (to_adapter_type(adapter.Properties.deviceType))
            {
            case AdapterType::Discrete: rank = 3; break;
            case AdapterType::Integrated: rank = 2; break;
            case AdapterType::Virtual: rank = 1; break;
            default: break;
            }
            return (rank << 48) | (adapter.LocalMemory >> 20); // memory in MiB fits below bit 48
        }

        AdapterInfo make_adapter_info(const Adapter &adapter)
        {
            // Driver name and version string: core since Vulkan 1.2, which every selected GPU has.
            VkPhysicalDeviceVulkan12Properties vulkan12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
            VkPhysicalDeviceProperties2 properties2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vulkan12};
            vkGetPhysicalDeviceProperties2(adapter.Physical, &properties2);

            return {
                .Name = adapter.Properties.deviceName,
                .Type = to_adapter_type(adapter.Properties.deviceType),
                .VendorId = adapter.Properties.vendorID,
                .DeviceId = adapter.Properties.deviceID,
                .ApiVersion = VersionToString(adapter.Properties.apiVersion),
                .Driver = fmt::format("{} {}", vulkan12.driverName, vulkan12.driverInfo),
                .DedicatedMemory = adapter.LocalMemory,
            };
        }

        std::unique_ptr<Device> create_device(const DeviceDesc &desc)
        {
            std::unique_ptr<Instance> instance = Instance::Create({
                .ApplicationName = desc.ApplicationName,
                .EnableValidation = desc.EnableValidation,
                .BreakOnValidationError = desc.BreakOnValidationError,
                .GpuValidation = desc.GpuValidation,
            });
            if (!instance)
                return nullptr;

            uint32_t count = 0;
            vkEnumeratePhysicalDevices(instance->GetHandle(), &count, nullptr);
            std::vector<VkPhysicalDevice> physicals(count);
            vkEnumeratePhysicalDevices(instance->GetHandle(), &count, physicals.data());

            // Log every GPU and why it was rejected: "why did it pick the wrong GPU?" is a common question.
            Adapter best;
            bool found = false;
            for (uint32_t i = 0; i < count; ++i)
            {
                Adapter adapter{.Physical = physicals[i]};
                vkGetPhysicalDeviceProperties(adapter.Physical, &adapter.Properties);
                adapter.LocalMemory = device_local_memory(adapter.Physical);

                const std::string problem = check_adapter(adapter.Physical, adapter.Properties, adapter.QueueFamily);
                LMAO_LOG_INFO(LogChannel(), "GPU {}: {} ({}, Vulkan {}, {:.1f} GiB)", i, adapter.Properties.deviceName,
                              ToString(to_adapter_type(adapter.Properties.deviceType)),
                              VersionToString(adapter.Properties.apiVersion), to_gib(adapter.LocalMemory));
                if (!problem.empty())
                {
                    LMAO_LOG_INFO(LogChannel(), "  not usable: {}", problem);
                    continue;
                }
                if (!found || score(adapter) > score(best))
                {
                    best = adapter;
                    found = true;
                }
            }
            if (!found)
            {
                LMAO_LOG_ERROR(LogChannel(), "No usable GPU: the engine needs Vulkan 1.3 with dynamic rendering, "
                                             "synchronization2 and bindless descriptors");
                return nullptr;
            }

            // One graphics + compute queue for now; transfer and async compute queues come later.
            const float priority = 1.0f;
            const VkDeviceQueueCreateInfo queue_info{
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = best.QueueFamily,
                .queueCount = 1,
                .pQueuePriorities = &priority,
            };

            FeatureChain enabled;
            for_each_required_feature(enabled, [](const char *, VkBool32 &value) { value = VK_TRUE; });

            // Optional: VK_EXT_memory_budget makes the allocator's budget numbers reflect what the
            // OS reports (other processes included) instead of a fraction of heap sizes.
            uint32_t extension_count = 0;
            vkEnumerateDeviceExtensionProperties(best.Physical, nullptr, &extension_count, nullptr);
            std::vector<VkExtensionProperties> available(extension_count);
            vkEnumerateDeviceExtensionProperties(best.Physical, nullptr, &extension_count, available.data());
            const bool memory_budget = HasExtension(available, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

            std::vector<const char *> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            if (memory_budget)
                extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            const VkDeviceCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext = &enabled.Features2,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &queue_info,
                .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                .ppEnabledExtensionNames = extensions.data(),
            };
            VkDevice device = VK_NULL_HANDLE;
            if (!Check(vkCreateDevice(best.Physical, &create_info, nullptr, &device), "vkCreateDevice"))
                return nullptr;
            volkLoadDevice(device);

            AdapterInfo info = make_adapter_info(best);
            LMAO_LOG_INFO(LogChannel(), "Using GPU: {} ({}, driver {})", info.Name, ToString(info.Type), info.Driver);
            auto vulkan_device = std::make_unique<VulkanDevice>(std::move(instance), best.Physical, device,
                                                                best.QueueFamily, std::move(info));
            if (!vulkan_device->Initialize(memory_budget, desc.UploadBudgetPerFrame, desc.PipelineCacheDirectory))
                return nullptr;
            return vulkan_device;
        }

    } // namespace

    VulkanDevice::VulkanDevice(std::unique_ptr<Instance> instance, VkPhysicalDevice physical, VkDevice device,
                               uint32_t queue_family, AdapterInfo info)
        : mInstance(std::move(instance)), mPhysicalDevice(physical), mDevice(device),
          mGraphicsQueueFamily(queue_family), mAdapterInfo(std::move(info)), mCommandList(*this)
    {
        vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
        SetName(VK_OBJECT_TYPE_DEVICE, mDevice, "Lmao device");
        SetName(VK_OBJECT_TYPE_QUEUE, mGraphicsQueue, "Graphics queue");
    }

    VulkanDevice::~VulkanDevice()
    {
        vkDeviceWaitIdle(mDevice);
        mShuttingDown = true;
        if (mFallbackTexture)
            DestroyTexture(mFallbackTexture);
        if (mFallbackBuffer)
            DestroyBuffer(mFallbackBuffer);
        if (mFallbackSampler)
            DestroySampler(mFallbackSampler);
        destroy_pending(true); // also releases the slots of destroyed resources
        LMAO_ASSERT(mSwapchainCount == 0, "{} swapchain(s) still alive when the device was destroyed", mSwapchainCount);
        LMAO_ASSERT(mTextures.IsEmpty(), "{} texture(s) still alive when the device was destroyed", mTextures.Size());
        LMAO_ASSERT(mPipelines.IsEmpty(), "{} pipeline(s) still alive when the device was destroyed", mPipelines.Size());
        LMAO_ASSERT(mBuffers.IsEmpty(), "{} buffer(s) still alive when the device was destroyed", mBuffers.Size());
        LMAO_ASSERT(mSamplers.IsEmpty(), "{} sampler(s) still alive when the device was destroyed", mSamplers.Size());
        if (mPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
        save_pipeline_cache();
        if (mPipelineCache != VK_NULL_HANDLE)
            vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
        destroy_frame_resources();
        if (mFramePool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(mDevice, mFramePool, nullptr); // frees the per-frame sets
        if (mBindlessPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(mDevice, mBindlessPool, nullptr);
        if (mFrameLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice, mFrameLayout, nullptr);
        if (mBindlessLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice, mBindlessLayout, nullptr);
        if (mAllocator != VK_NULL_HANDLE)
            vmaDestroyAllocator(mAllocator); // asserts (in VMA) if any allocation leaked
        vkDestroyDevice(mDevice, nullptr);
        device_exists = false;
    }

    void VulkanDevice::WaitIdle()
    {
        LMAO_PROFILE_ZONE();
        Check(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle");
    }

    bool VulkanDevice::Initialize(bool memory_budget, uint64_t upload_budget_per_frame,
                                  std::string_view pipeline_cache_directory)
    {
        // The budget must fit the upload ring with room left for in-frame CommandList::Upload*.
        mUploadBudget = std::clamp<uint64_t>(upload_budget_per_frame, 64 * 1024, UploadRingSize / 2);
        VkPhysicalDeviceVulkan12Properties vulkan12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vulkan12};
        vkGetPhysicalDeviceProperties2(mPhysicalDevice, &properties);
        mLimits = properties.properties.limits;

        if (!create_allocator(memory_budget) || !create_descriptors(vulkan12))
            return false;

        const VkDescriptorSetLayout set_layouts[] = {mBindlessLayout, mFrameLayout};
        const VkPushConstantRange push_constants{.stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = MaxPushConstantsSize};
        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(std::size(set_layouts)),
            .pSetLayouts = set_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constants,
        };
        if (!Check(vkCreatePipelineLayout(mDevice, &layout_info, nullptr, &mPipelineLayout), "vkCreatePipelineLayout"))
            return false;
        SetName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, mPipelineLayout, "Global pipeline layout");
        if (!create_pipeline_cache(pipeline_cache_directory, properties.properties))
            return false;

        VkSemaphoreTypeCreateInfo timeline_type{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };
        const VkSemaphoreCreateInfo timeline_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timeline_type};
        if (!Check(vkCreateSemaphore(mDevice, &timeline_info, nullptr, &mFrameTimeline), "vkCreateSemaphore (timeline)"))
            return false;
        SetName(VK_OBJECT_TYPE_SEMAPHORE, mFrameTimeline, "Frame timeline");

        for (uint32_t i = 0; i < MaxFramesInFlight; ++i)
        {
            Frame &frame = mFrames[i];
            const VkCommandPoolCreateInfo pool_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, // reset as a whole every frame
                .queueFamilyIndex = mGraphicsQueueFamily,
            };
            if (!Check(vkCreateCommandPool(mDevice, &pool_info, nullptr, &frame.command_pool), "vkCreateCommandPool"))
                return false;

            const VkCommandBufferAllocateInfo buffer_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = frame.command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (!Check(vkAllocateCommandBuffers(mDevice, &buffer_info, &frame.command_buffer), "vkAllocateCommandBuffers"))
                return false;


            const VkBufferCreateInfo upload_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = UploadRingSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };
            const VmaAllocationCreateInfo upload_allocation_info{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            };
            VmaAllocationInfo upload_mapping{};
            if (!Check(vmaCreateBuffer(mAllocator, &upload_info, &upload_allocation_info, &frame.upload_buffer,
                                       &frame.upload_allocation, &upload_mapping),
                       "vmaCreateBuffer (upload ring)"))
                return false;
            frame.upload_data = static_cast<std::byte *>(upload_mapping.pMappedData);
            if (!create_frame_constants(frame, i))
                return false;

            const std::string prefix = fmt::format("Frame {}", i);
            SetName(VK_OBJECT_TYPE_BUFFER, frame.upload_buffer, (prefix + " upload ring").c_str());
            vmaSetAllocationName(mAllocator, frame.upload_allocation, (prefix + " upload ring").c_str());
            SetName(VK_OBJECT_TYPE_COMMAND_POOL, frame.command_pool, (prefix + " command pool").c_str());
            SetName(VK_OBJECT_TYPE_COMMAND_BUFFER, frame.command_buffer, (prefix + " command buffer").c_str());
        }
        return create_fallbacks();
    }

    void VulkanDevice::destroy_frame_resources()
    {
        for (Frame &frame : mFrames)
        {
            if (frame.command_pool != VK_NULL_HANDLE)
                vkDestroyCommandPool(mDevice, frame.command_pool, nullptr); // frees its command buffer
            if (frame.upload_buffer != VK_NULL_HANDLE)
                vmaDestroyBuffer(mAllocator, frame.upload_buffer, frame.upload_allocation);
            if (frame.constants_buffer != VK_NULL_HANDLE)
                vmaDestroyBuffer(mAllocator, frame.constants_buffer, frame.constants_allocation);
            frame = {};
        }
        if (mFrameTimeline != VK_NULL_HANDLE)
            vkDestroySemaphore(mDevice, mFrameTimeline, nullptr);
        mFrameTimeline = VK_NULL_HANDLE;
    }

    bool VulkanDevice::create_allocator(bool memory_budget)
    {
        // VMA loads the rest of the functions it needs through these two (from volk).
        const VmaVulkanFunctions functions{
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        };
        VmaAllocatorCreateFlags flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (memory_budget)
            flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        const VmaAllocatorCreateInfo create_info{
            .flags = flags,
            .physicalDevice = mPhysicalDevice,
            .device = mDevice,
            .pVulkanFunctions = &functions,
            .instance = mInstance->GetHandle(),
            .vulkanApiVersion = VK_API_VERSION_1_3,
        };
        if (!Check(vmaCreateAllocator(&create_info, &mAllocator), "vmaCreateAllocator"))
            return false;
        LMAO_LOG_DEBUG(LogChannel(), "Memory allocator created (memory budget {})", memory_budget ? "on" : "off");
        return true;
    }

    void VulkanDevice::defer_destroy(PendingDestroy pending)
    {
        // Submitted frames may still use the objects, and so may the frame being recorded right now.
        pending.frame = mRecording ? mSubmittedFrames + 1 : mSubmittedFrames;
        mPendingDestroys.push_back(pending);
    }

    void VulkanDevice::destroy_pending(bool everything)
    {
        uint64_t completed = 0;
        if (!everything)
            Check(vkGetSemaphoreCounterValue(mDevice, mFrameTimeline, &completed), "vkGetSemaphoreCounterValue");
        std::erase_if(mPendingDestroys, [&](const PendingDestroy &pending)
                      {
                          if (!everything && pending.frame > completed)
                              return false;
                          // Views before their images; the allocation after the object bound to it.
                          if (pending.pipeline != VK_NULL_HANDLE)
                              vkDestroyPipeline(mDevice, pending.pipeline, nullptr);
                          if (pending.sampler != VK_NULL_HANDLE)
                              vkDestroySampler(mDevice, pending.sampler, nullptr);
                          if (pending.view != VK_NULL_HANDLE)
                              vkDestroyImageView(mDevice, pending.view, nullptr);
                          if (pending.image != VK_NULL_HANDLE)
                              vmaDestroyImage(mAllocator, pending.image, pending.allocation);
                          else if (pending.buffer != VK_NULL_HANDLE)
                              vmaDestroyBuffer(mAllocator, pending.buffer, pending.allocation);
                          // Now no frame can read the old descriptors: point the slots back at the
                          // fallbacks (a stale index reads magenta / zeros) and let them be reused.
                          if (pending.texture_slot)
                          {
                              point_slots_at_fallback(0, pending.texture_slot.Index, 1);
                              point_slots_at_fallback(1, pending.texture_slot.Index, 1);
                              mTextures.Remove(pending.texture_slot);
                          }
                          if (pending.buffer_slot)
                          {
                              point_slots_at_fallback(2, pending.buffer_slot.Index, 1);
                              mBuffers.Remove(pending.buffer_slot);
                          }
                          if (pending.sampler_slot)
                          {
                              point_slots_at_fallback(3, pending.sampler_slot.Index, 1);
                              mSamplers.Remove(pending.sampler_slot);
                          }
                          return true;
                      });
    }

    std::unique_ptr<Swapchain> VulkanDevice::CreateSwapchain(const SwapchainDesc &desc)
    {
        return VulkanSwapchain::Create(*this, desc);
    }

    CommandList *VulkanDevice::BeginFrame(std::span<Swapchain *const> swapchains)
    {
        LMAO_PROFILE_ZONE();
        LMAO_ASSERT(!mRecording, "BeginFrame called twice without EndFrame");
        Frame &frame = mFrames[mFrameIndex];

        {
            // The GPU must be done with this slot's command buffer and acquire semaphores.
            LMAO_PROFILE_ZONE_NAMED("Wait for frame slot");
            const VkSemaphoreWaitInfo wait_info{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &mFrameTimeline,
                .pValues = &frame.timeline_value,
            };
            if (!Check(vkWaitSemaphores(mDevice, &wait_info, UINT64_MAX), "vkWaitSemaphores"))
                return nullptr;
        }

        destroy_pending(false);

        mFrameSwapchains.clear();
        for (Swapchain *swapchain : swapchains)
        {
            auto *vulkan_swapchain = static_cast<VulkanSwapchain *>(swapchain);
            if (vulkan_swapchain && vulkan_swapchain->Acquire(mFrameIndex))
                mFrameSwapchains.push_back(vulkan_swapchain);
        }
        if (mFrameSwapchains.empty())
            return nullptr; // nothing to present (e.g. all minimized): the slot isn't used

        Check(vkResetCommandPool(mDevice, frame.command_pool, 0), "vkResetCommandPool");
        frame.upload_used = 0; // the GPU has finished the copies that read it
        mRecording = true;     // before recording: staging allocation needs it
        mCommandList.Begin(frame.command_buffer);
        drain_upload_queue();
        return &mCommandList;
    }

    void VulkanDevice::EndFrame()
    {
        LMAO_PROFILE_ZONE();
        LMAO_ASSERT(mRecording, "EndFrame without a successful BeginFrame");
        Frame &frame = mFrames[mFrameIndex];

        LMAO_PROFILE_PLOT("Upload ring (KiB)", static_cast<double>(frame.upload_used) / 1024.0);
        for (VulkanSwapchain *swapchain : mFrameSwapchains)
            mCommandList.TransitionForPresent(swapchain->GetCurrentTexture());
        mCommandList.End();

        // Wait for every acquired image; signal each image's render-finished semaphore and the timeline.
        std::vector<VkSemaphoreSubmitInfo> waits;
        std::vector<VkSemaphoreSubmitInfo> signals;
        for (VulkanSwapchain *swapchain : mFrameSwapchains)
        {
            waits.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = swapchain->GetAcquireSemaphore(),
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // first use of the image
            });
            signals.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = swapchain->GetRenderFinishedSemaphore(),
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            });
        }
        const uint64_t frame_value = mSubmittedFrames + 1;
        signals.push_back({
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = mFrameTimeline,
            .value = frame_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
        const VkCommandBufferSubmitInfo command_buffer{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.command_buffer,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size()),
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command_buffer,
            .signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size()),
            .pSignalSemaphoreInfos = signals.data(),
        };
        if (Check(vkQueueSubmit2(mGraphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2"))
        {
            mSubmittedFrames = frame_value;
            frame.timeline_value = frame_value;
            for (VulkanSwapchain *swapchain : mFrameSwapchains)
                swapchain->Present(mGraphicsQueue);
        }
        // On failure (device lost) nothing was submitted; the slot's old value stays valid.

        mFrameSwapchains.clear();
        mRecording = false;
        mFrameIndex = (mFrameIndex + 1) % MaxFramesInFlight;
    }

    std::unique_ptr<Device> CreateDevice(const DeviceDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        if (device_exists.exchange(true))
        {
            LMAO_LOG_ERROR(LogChannel(), "Only one Vulkan device may exist at a time");
            return nullptr;
        }
        std::unique_ptr<Device> device = create_device(desc);
        if (!device)
            device_exists = false;
        return device;
    }

} // namespace Lmao::Rhi::Vulkan
