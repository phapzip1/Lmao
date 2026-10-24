#include "rhi/rhis/vulkan/vulkan_instance.h"

#include "core/assert/assert.h"
#include "core/profile/profile.h"

#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // IsDebuggerPresent
#endif

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        constexpr const char *validation_layer = "VK_LAYER_KHRONOS_validation";

        bool debugger_attached()
        {
#if defined(_WIN32)
            return IsDebuggerPresent() != FALSE;
#else
            return false; // TODO: detect a tracer on Linux (TracerPid in /proc/self/status)
#endif
        }

        const char *message_type_name(VkDebugUtilsMessageTypeFlagsEXT types)
        {
            if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
                return "validation";
            if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
                return "performance";
            return "general";
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL on_debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT types,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data, void *user_data)
        {
            const char *id = data->pMessageIdName ? data->pMessageIdName : "-";
            const char *message = data->pMessage ? data->pMessage : "";

            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                LMAO_LOG_ERROR(LogChannel(), "[{}] {}: {}", message_type_name(types), id, message);
                const auto *state = static_cast<const MessengerState *>(user_data);
                if (state->BreakOnError && debugger_attached())
                    LMAO_DEBUG_BREAK(); // the call stack shows the API call that triggered the error
            }
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                LMAO_LOG_WARN(LogChannel(), "[{}] {}: {}", message_type_name(types), id, message);
            }
            else
            {
                LMAO_LOG_DEBUG(LogChannel(), "[{}] {}: {}", message_type_name(types), id, message);
            }
            return VK_FALSE; // never abort the Vulkan call
        }

        VkDebugUtilsMessengerCreateInfoEXT messenger_create_info(MessengerState *state)
        {
            return {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = &on_debug_message,
                .pUserData = state,
            };
        }

        std::vector<VkLayerProperties> available_layers()
        {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            return layers;
        }

        // Extensions of the implementation and implicit layers, or of one layer if `layer` is set.
        std::vector<VkExtensionProperties> available_extensions(const char *layer = nullptr)
        {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
            std::vector<VkExtensionProperties> extensions(count);
            vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data());
            return extensions;
        }

        // Surface extensions per window system. Names are spelled out because their macros live in
        // vulkan_win32.h / vulkan_xlib.h / vulkan_wayland.h, which only vulkan_surface.cpp includes.
        struct WindowSystemExtension
        {
            NativeWindowType type;
            const char *name;
            const char *label;
        };

        constexpr WindowSystemExtension window_system_extensions[] = {
#if defined(_WIN32)
            {NativeWindowType::Win32, "VK_KHR_win32_surface", "Win32"},
#elif defined(__linux__)
            {NativeWindowType::X11, "VK_KHR_xlib_surface", "X11"},
            {NativeWindowType::Wayland, "VK_KHR_wayland_surface", "Wayland"},
#endif
        };

        std::string window_system_extension_names()
        {
            std::string names;
            for (const WindowSystemExtension &window_system : window_system_extensions)
                names += names.empty() ? window_system.name : fmt::format(", {}", window_system.name);
            return names;
        }

        bool has_layer(const std::vector<VkLayerProperties> &layers, const char *name)
        {
            for (const VkLayerProperties &layer : layers)
                if (std::strcmp(layer.layerName, name) == 0)
                    return true;
            return false;
        }

    } // namespace

    std::unique_ptr<Instance> Instance::Create(const InstanceDesc &desc)
    {
        LMAO_PROFILE_ZONE();

        if (!Check(volkInitialize(), "volkInitialize (is a Vulkan driver installed?)"))
            return nullptr;

        const uint32_t loader_version = volkGetInstanceVersion();
        if (loader_version < VK_API_VERSION_1_3)
        {
            LMAO_LOG_ERROR(LogChannel(), "Vulkan loader {} is too old: 1.3 is required (update the GPU driver)",
                           VersionToString(loader_version));
            return nullptr;
        }

        const std::vector<VkLayerProperties> layers = available_layers();
        const std::vector<VkExtensionProperties> extensions = available_extensions();
        std::vector<const char *> enabled_layers;
        std::vector<const char *> enabled_extensions;

        auto instance = std::unique_ptr<Instance>(new Instance());

        // The RHI presents to windows: VK_KHR_surface plus at least one window-system extension.
        if (!HasExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME))
        {
            LMAO_LOG_ERROR(LogChannel(), "Required instance extension {} is not supported", VK_KHR_SURFACE_EXTENSION_NAME);
            return nullptr;
        }
        enabled_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

        std::string window_systems;
        for (const WindowSystemExtension &window_system : window_system_extensions)
        {
            if (!HasExtension(extensions, window_system.name))
                continue;
            enabled_extensions.push_back(window_system.name);
            instance->mWindowTypes |= WindowTypeBit(window_system.type);
            window_systems += window_systems.empty() ? window_system.label : fmt::format(", {}", window_system.label);
        }
        if (window_systems.empty())
        {
            LMAO_LOG_ERROR(LogChannel(), "No window-system surface extension is supported (need one of: {})",
                           window_system_extension_names());
            return nullptr;
        }
        instance->mMessengerState.BreakOnError = desc.BreakOnValidationError;

        // Optional: object names and labels for RenderDoc, and the messenger below.
        instance->mHasDebugUtils = HasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (instance->mHasDebugUtils)
            enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        bool sync_validation = false;
        if (desc.EnableValidation)
        {
            if (has_layer(layers, validation_layer))
            {
                enabled_layers.push_back(validation_layer);
                // Synchronization validation catches missing barriers and semaphore misuse that core
                // validation doesn't. Configured through VK_EXT_layer_settings, which the layer provides.
                const std::vector<VkExtensionProperties> layer_extensions = available_extensions(validation_layer);
                sync_validation = HasExtension(layer_extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
                if (sync_validation)
                    enabled_extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
                else
                    LMAO_LOG_WARN(LogChannel(), "The validation layer is too old for {}; synchronization validation is off",
                                  VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
            }
            else
            {
                LMAO_LOG_WARN(LogChannel(), "Validation requested but {} is not installed (install the Vulkan SDK); "
                                            "continuing without it",
                              validation_layer);
            }
        }
        instance->mValidationEnabled = !enabled_layers.empty();

        const std::string application_name(desc.ApplicationName);
        const VkApplicationInfo application_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = application_name.c_str(),
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "Lmao",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        };

        // Also chained into vkCreateInstance, so problems during instance creation and
        // destruction are reported, not only those after the messenger exists.
        VkDebugUtilsMessengerCreateInfoEXT messenger_info = messenger_create_info(&instance->mMessengerState);

        const VkBool32 enable = VK_TRUE;
        const bool gpu_validation = sync_validation && desc.GpuValidation;
        if (desc.GpuValidation && !gpu_validation)
            LMAO_LOG_WARN(LogChannel(), "GPU-assisted validation requested but needs the validation layer with {}",
                          VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        const VkLayerSettingEXT layer_settings[] = {
            {validation_layer, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enable},
            // Instruments shaders: out-of-range or destroyed bindless indices, out-of-bounds access.
            {validation_layer, "gpuav_enable", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enable},
        };
        VkLayerSettingsCreateInfoEXT layer_settings_info{
            .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            .settingCount = gpu_validation ? 2u : 1u,
            .pSettings = layer_settings,
        };

        // pNext chain: [messenger] -> [layer settings]
        const void *next = nullptr;
        if (sync_validation)
            next = &layer_settings_info;
        if (instance->mHasDebugUtils)
        {
            messenger_info.pNext = next;
            next = &messenger_info;
        }

        const VkInstanceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = next,
            .pApplicationInfo = &application_info,
            .enabledLayerCount = static_cast<uint32_t>(enabled_layers.size()),
            .ppEnabledLayerNames = enabled_layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
            .ppEnabledExtensionNames = enabled_extensions.data(),
        };
        if (!Check(vkCreateInstance(&create_info, nullptr, &instance->mInstance), "vkCreateInstance"))
            return nullptr;

        volkLoadInstanceOnly(instance->mInstance);

        if (instance->mHasDebugUtils &&
            !Check(vkCreateDebugUtilsMessengerEXT(instance->mInstance, &messenger_info, nullptr, &instance->mMessenger),
                   "vkCreateDebugUtilsMessengerEXT"))
            return nullptr;

        LMAO_LOG_INFO(LogChannel(), "Vulkan instance created (loader {}, validation {}, surfaces: {})",
                      VersionToString(loader_version),
                      !instance->mValidationEnabled ? "off"
                      : gpu_validation              ? "on + sync + GPU-assisted"
                      : sync_validation             ? "on + sync"
                                                    : "on",
                      window_systems);
        return instance;
    }

    Instance::~Instance()
    {
        if (mMessenger != VK_NULL_HANDLE)
            vkDestroyDebugUtilsMessengerEXT(mInstance, mMessenger, nullptr);
        if (mInstance != VK_NULL_HANDLE)
            vkDestroyInstance(mInstance, nullptr);
    }

} // namespace Lmao::Rhi::Vulkan
