# Vulkan backend. Vulkan headers and volk are PRIVATE: no Vulkan type leaves this target.
# Only lmao_rhi links it; engine and game code go through Rhi::Device.
add_library(lmao_rhi_vulkan STATIC
    vulkan_common.h
    vulkan_common.cpp
    vulkan_device.h
    vulkan_device.cpp
    vulkan_instance.h
    vulkan_instance.cpp
    vulkan_surface.h
    vulkan_surface.cpp # the only file that includes window-system headers
)

target_link_libraries(lmao_rhi_vulkan
    PUBLIC lmao_rhi_interface
    PRIVATE volk)

lmao_set_warnings(lmao_rhi_vulkan)
set_target_properties(lmao_rhi_vulkan PROPERTIES FOLDER "engine")
