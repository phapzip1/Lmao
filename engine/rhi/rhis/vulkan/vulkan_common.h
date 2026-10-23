#pragma once

// Shared by the Vulkan backend's .cpp files. Never include from outside rhi/rhis/vulkan/.
// volk.h must come before any other Vulkan header: it defines VK_NO_PROTOTYPES.

#include "core/log/log.h"

#include <volk.h>

#include <span>
#include <string>

namespace Lmao::Rhi::Vulkan
{

    // The "vulkan" log channel.
    spdlog::logger *LogChannel();

    // True for VK_SUCCESS and other non-error results; logs `what` and the result otherwise.
    bool Check(VkResult result, const char *what);

    // "1.3.280" from a VK_MAKE_API_VERSION value.
    std::string VersionToString(uint32_t version);

    bool HasExtension(std::span<const VkExtensionProperties> extensions, const char *name);

} // namespace Lmao::Rhi::Vulkan
