#pragma once

// Vulkan instance, function loading (volk) and the debug messenger that reports validation
// messages to the log. Internal to rhi/rhis/vulkan/.

#include "rhi/rhis/vulkan/vulkan_common.h"

#include <memory>
#include <string_view>

namespace Lmao::Rhi::Vulkan
{

    struct InstanceDesc
    {
        std::string_view ApplicationName;
        bool EnableValidation = false;
        bool BreakOnValidationError = true;
    };

    // Read by the debug messenger callback. Lives inside Instance, so its address is stable.
    struct MessengerState
    {
        bool BreakOnError = true;
    };

    class Instance
    {
    public:
        // nullptr on failure (the reason is logged).
        static std::unique_ptr<Instance> Create(const InstanceDesc &desc);

        ~Instance();

        Instance(const Instance &) = delete;
        Instance &operator=(const Instance &) = delete;

        VkInstance GetHandle() const { return mInstance; }

        // VK_EXT_debug_utils is enabled: object names, labels and the messenger are available.
        bool HasDebugUtils() const { return mHasDebugUtils; }
        bool IsValidationEnabled() const { return mValidationEnabled; }

    private:
        Instance() = default;

        VkInstance mInstance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT mMessenger = VK_NULL_HANDLE;
        MessengerState mMessengerState;
        bool mHasDebugUtils = false;
        bool mValidationEnabled = false;
    };

} // namespace Lmao::Rhi::Vulkan
