// The one translation unit that compiles VulkanMemoryAllocator. Its configuration
// (VMA_STATIC_VULKAN_FUNCTIONS=0, VMA_DYNAMIC_VULKAN_FUNCTIONS=1, VMA_VULKAN_VERSION) is set on
// the lmao_rhi_vulkan target so every file that includes vk_mem_alloc.h agrees.

#include "rhi/rhis/vulkan/vulkan_common.h" // volk before VMA

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
