add_library(lmao_core STATIC
    assert/assert.h
    assert/assert.cpp
    containers/enum_flags.h
    containers/handle.h
    containers/slot_map.h
    io/path.h
    log/log.h
    log/log.cpp
    math/math.h
    platform/crash_handler.h
    platform/event.h
    platform/input.h
    platform/input.cpp
    platform/paths.h
    platform/platform.h
    platform/time.h
    platform/window.h
    platform/platforms/glfw/glfw_native.h
    platform/platforms/glfw/glfw_native.cpp
    platform/platforms/glfw/glfw_platform.cpp
    profile/profile.h
)
add_library(Lmao::core ALIAS lmao_core)

if(WIN32)
    target_sources(lmao_core PRIVATE
        platform/platforms/win32/crash_handler_win32.cpp
        platform/platforms/win32/paths_win32.cpp)
    target_link_libraries(lmao_core PRIVATE dbghelp shell32 ole32) # ole32: CoTaskMemFree
else()
    target_sources(lmao_core PRIVATE
        platform/platforms/posix/crash_handler_posix.cpp
        platform/platforms/posix/paths_posix.cpp)
endif()

# Headers are included as "core/<module>/<file>.h".
target_include_directories(lmao_core PUBLIC ${PROJECT_SOURCE_DIR}/engine)

target_link_libraries(lmao_core
    PUBLIC spdlog::spdlog glm::glm Tracy::TracyClient
    PRIVATE glfw) # GLFW stays inside platform/platforms/glfw/; nothing public exposes it

option(LMAO_ASSERTS_IN_RELEASE "Keep LMAO_ASSERT checks in non-Debug builds" OFF)

target_compile_definitions(lmao_core PUBLIC
    # Strip log calls below this level at compile time.
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_INFO>
    LMAO_ASSERTS_ENABLED=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${LMAO_ASSERTS_IN_RELEASE}>>,1,0>)

# Assert macros use __VA_OPT__, which needs MSVC's conforming preprocessor.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(lmao_core PUBLIC /Zc:preprocessor)
endif()

lmao_set_warnings(lmao_core)
set_target_properties(lmao_core PROPERTIES FOLDER "engine")
