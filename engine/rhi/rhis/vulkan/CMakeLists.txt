# Vulkan backend. Vulkan headers and volk are PRIVATE: no Vulkan type leaves this target.
# Only lmao_rhi links it; engine and game code go through Rhi::Device.
add_library(lmao_rhi_vulkan STATIC
    vulkan_common.h
    vulkan_common.cpp
    vulkan_descriptors.cpp
    vulkan_command_list.h
    vulkan_command_list.cpp
    vulkan_device.h
    vulkan_device.cpp
    vulkan_device_internal.h
    vulkan_instance.h
    vulkan_instance.cpp
    vulkan_memory.cpp # VMA_IMPLEMENTATION
    vulkan_pipeline.cpp
    vulkan_pipeline_cache.cpp
    vulkan_resources.cpp
    vulkan_surface.h
    vulkan_surface.cpp # the only file that includes window-system headers
    vulkan_swapchain.h
    vulkan_swapchain.cpp
    vulkan_uploads.cpp
)

target_link_libraries(lmao_rhi_vulkan
    PUBLIC lmao_rhi_interface
    PRIVATE volk GPUOpen::VulkanMemoryAllocator)

# VMA gets its Vulkan functions through vkGetInstanceProcAddr/vkGetDeviceProcAddr (from volk).
# Set target-wide so every file that includes vk_mem_alloc.h sees the same configuration.
target_compile_definitions(lmao_rhi_vulkan PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    VMA_VULKAN_VERSION=1003000)

lmao_set_warnings(lmao_rhi_vulkan)
set_target_properties(lmao_rhi_vulkan PROPERTIES FOLDER "engine")
