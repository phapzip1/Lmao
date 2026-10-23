// Vulkan implementation of Rhi::Device: GPU selection and the logical device.

#include "rhi/rhis/vulkan/vulkan_device.h"

#include "core/profile/profile.h"
#include "rhi/rhis/vulkan/vulkan_instance.h"

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
            VkPhysicalDeviceVulkan12Features Vulkan12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceVulkan13Features Vulkan13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

            FeatureChain()
            {
                Features2.pNext = &Vulkan12;
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
            fn("timelineSemaphore", chain.Vulkan12.timelineSemaphore);
            fn("bufferDeviceAddress", chain.Vulkan12.bufferDeviceAddress);
            // Bindless descriptors
            fn("descriptorIndexing", chain.Vulkan12.descriptorIndexing);
            fn("runtimeDescriptorArray", chain.Vulkan12.runtimeDescriptorArray);
            fn("descriptorBindingPartiallyBound", chain.Vulkan12.descriptorBindingPartiallyBound);
            fn("descriptorBindingSampledImageUpdateAfterBind", chain.Vulkan12.descriptorBindingSampledImageUpdateAfterBind);
            fn("descriptorBindingStorageImageUpdateAfterBind", chain.Vulkan12.descriptorBindingStorageImageUpdateAfterBind);
            fn("descriptorBindingStorageBufferUpdateAfterBind", chain.Vulkan12.descriptorBindingStorageBufferUpdateAfterBind);
            fn("shaderSampledImageArrayNonUniformIndexing", chain.Vulkan12.shaderSampledImageArrayNonUniformIndexing);
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

        class VulkanDevice final : public Device
        {
        public:
            VulkanDevice(std::unique_ptr<Instance> instance, VkPhysicalDevice physical, VkDevice device,
                         uint32_t queue_family, AdapterInfo info);
            ~VulkanDevice() override;

            Backend GetBackend() const override { return Backend::Vulkan; }
            const AdapterInfo &GetAdapterInfo() const override { return mAdapterInfo; }
            void WaitIdle() override;

        private:
            // Shows up in validation messages, RenderDoc and Nsight instead of a raw handle.
            template <typename T>
            void set_name(VkObjectType type, T handle, const char *name)
            {
                if (!mInstance->HasDebugUtils())
                    return;
                const VkDebugUtilsObjectNameInfoEXT info{
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    .objectType = type,
                    .objectHandle = reinterpret_cast<uint64_t>(handle),
                    .pObjectName = name,
                };
                vkSetDebugUtilsObjectNameEXT(mDevice, &info);
            }

            std::unique_ptr<Instance> mInstance; // declared first: destroyed after the device
            VkPhysicalDevice mPhysicalDevice;
            VkDevice mDevice;
            uint32_t mGraphicsQueueFamily;
            VkQueue mGraphicsQueue = VK_NULL_HANDLE;
            AdapterInfo mAdapterInfo;
        };

        VulkanDevice::VulkanDevice(std::unique_ptr<Instance> instance, VkPhysicalDevice physical, VkDevice device,
                                   uint32_t queue_family, AdapterInfo info)
            : mInstance(std::move(instance)), mPhysicalDevice(physical), mDevice(device),
              mGraphicsQueueFamily(queue_family), mAdapterInfo(std::move(info))
        {
            vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
            set_name(VK_OBJECT_TYPE_DEVICE, mDevice, "Lmao device");
            set_name(VK_OBJECT_TYPE_QUEUE, mGraphicsQueue, "Graphics queue");
        }

        VulkanDevice::~VulkanDevice()
        {
            vkDeviceWaitIdle(mDevice);
            vkDestroyDevice(mDevice, nullptr);
            device_exists = false;
        }

        void VulkanDevice::WaitIdle()
        {
            LMAO_PROFILE_ZONE();
            Check(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle");
        }

        std::unique_ptr<Device> create_device(const DeviceDesc &desc)
        {
            std::unique_ptr<Instance> instance = Instance::Create({
                .ApplicationName = desc.ApplicationName,
                .EnableValidation = desc.EnableValidation,
                .BreakOnValidationError = desc.BreakOnValidationError,
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

            const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            const VkDeviceCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext = &enabled.Features2,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &queue_info,
                .enabledExtensionCount = static_cast<uint32_t>(std::size(extensions)),
                .ppEnabledExtensionNames = extensions,
            };
            VkDevice device = VK_NULL_HANDLE;
            if (!Check(vkCreateDevice(best.Physical, &create_info, nullptr, &device), "vkCreateDevice"))
                return nullptr;
            volkLoadDevice(device);

            AdapterInfo info = make_adapter_info(best);
            LMAO_LOG_INFO(LogChannel(), "Using GPU: {} ({}, driver {})", info.Name, ToString(info.Type), info.Driver);
            return std::make_unique<VulkanDevice>(std::move(instance), best.Physical, device, best.QueueFamily,
                                                  std::move(info));
        }

    } // namespace

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
