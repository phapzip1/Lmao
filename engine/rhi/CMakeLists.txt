# Backend-agnostic RHI interface: headers only, no backend headers or libraries.
# Backends (rhis/<api>/, e.g. rhis/vulkan/) link this; they never link lmao_rhi.
add_library(lmao_rhi_interface INTERFACE)
add_library(Lmao::rhi_interface ALIAS lmao_rhi_interface)
target_link_libraries(lmao_rhi_interface INTERFACE Lmao::core)

# One target per backend, each in rhis/<api>/.
add_subdirectory(rhis/vulkan)

# What engine and game code link: the interface plus Device::Create, which picks a backend
# at runtime. Backends are PRIVATE so their headers and libraries don't reach consumers.
add_library(lmao_rhi STATIC
    device.h
    device.cpp
)
add_library(Lmao::rhi ALIAS lmao_rhi)

target_link_libraries(lmao_rhi
    PUBLIC lmao_rhi_interface
    PRIVATE lmao_rhi_vulkan)

lmao_set_warnings(lmao_rhi)
set_target_properties(lmao_rhi PROPERTIES FOLDER "engine")
