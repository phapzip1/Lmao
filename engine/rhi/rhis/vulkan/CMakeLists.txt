# Vulkan backend. Vulkan headers and volk are PRIVATE: no Vulkan type leaves this target.
# Only lmao_rhi links it; engine and game code go through Rhi::Device.
add_library(lmao_rhi_vulkan STATIC
    vulkan_common.h
    vulkan_common.cpp
    vulkan_command_list.h
    vulkan_command_list.cpp
    vulkan_device.h
    vulkan_device.cpp
    vulkan_device_internal.h
    vulkan_instance.h
    vulkan_instance.cpp
    vulkan_pipeline.cpp
    vulkan_surface.h
    vulkan_surface.cpp # the only file that includes window-system headers
    vulkan_swapchain.h
    vulkan_swapchain.cpp
)

target_link_libraries(lmao_rhi_vulkan
    PUBLIC lmao_rhi_interface
    PRIVATE volk)

lmao_set_warnings(lmao_rhi_vulkan)
set_target_properties(lmao_rhi_vulkan PROPERTIES FOLDER "engine")
