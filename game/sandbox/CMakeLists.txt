add_executable(lmao_sandbox main.cpp fly_camera.h)
target_link_libraries(lmao_sandbox PRIVATE Lmao::core Lmao::rhi Lmao::resource Lmao::render Lmao::imgui)
# Default model location for development runs (scripts/fetch_assets.sh puts models there).
target_compile_definitions(lmao_sandbox PRIVATE LMAO_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
lmao_add_shaders(lmao_sandbox
    ${PROJECT_SOURCE_DIR}/shaders/cube.slang
    ${PROJECT_SOURCE_DIR}/shaders/model.slang
    ${PROJECT_SOURCE_DIR}/shaders/triangle.slang)
lmao_set_warnings(lmao_sandbox)
set_target_properties(lmao_sandbox PROPERTIES FOLDER "game")
