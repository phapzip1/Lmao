# Dear ImGui on the engine's platform events and RHI (no GLFW or Vulkan ImGui backends).
# imgui is PUBLIC: users build their UI with the ImGui API directly.
add_library(lmao_imgui STATIC
    imgui_backend.h
    imgui_backend.cpp
)
add_library(Lmao::imgui ALIAS lmao_imgui)

target_link_libraries(lmao_imgui PUBLIC Lmao::core Lmao::rhi imgui)
lmao_add_shaders(lmao_imgui ${PROJECT_SOURCE_DIR}/shaders/imgui.slang)

lmao_set_warnings(lmao_imgui)
set_target_properties(lmao_imgui PROPERTIES FOLDER "engine")
