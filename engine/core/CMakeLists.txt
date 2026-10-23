add_library(lmao_core STATIC
    log/log.h
    log/log.cpp
    math/math.h
)
add_library(Lmao::core ALIAS lmao_core)

# Headers are included as "core/<module>/<file>.h".
target_include_directories(lmao_core PUBLIC ${PROJECT_SOURCE_DIR}/engine)

target_link_libraries(lmao_core PUBLIC spdlog::spdlog glm::glm)

# Strip log calls below this level at compile time.
target_compile_definitions(lmao_core PUBLIC
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_INFO>)

lmao_set_warnings(lmao_core)
set_target_properties(lmao_core PROPERTIES FOLDER "engine")
