# Null backend: no GPU, records nothing. Headless runs (CI smoke tests, servers, tools).
add_library(lmao_rhi_null STATIC
    null_device.h
    null_device.cpp
)

target_link_libraries(lmao_rhi_null PUBLIC lmao_rhi_interface)

lmao_set_warnings(lmao_rhi_null)
set_target_properties(lmao_rhi_null PROPERTIES FOLDER "engine")
