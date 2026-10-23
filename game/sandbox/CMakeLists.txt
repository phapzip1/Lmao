add_executable(lmao_sandbox main.cpp)
target_link_libraries(lmao_sandbox PRIVATE Lmao::core)
lmao_set_warnings(lmao_sandbox)
set_target_properties(lmao_sandbox PROPERTIES FOLDER "game")
