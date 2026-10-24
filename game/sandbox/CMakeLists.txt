add_executable(lmao_sandbox main.cpp)
target_link_libraries(lmao_sandbox PRIVATE Lmao::core Lmao::rhi)
lmao_add_shaders(lmao_sandbox
    ${PROJECT_SOURCE_DIR}/shaders/cube.slang
    ${PROJECT_SOURCE_DIR}/shaders/triangle.slang)
lmao_set_warnings(lmao_sandbox)
set_target_properties(lmao_sandbox PROPERTIES FOLDER "game")
