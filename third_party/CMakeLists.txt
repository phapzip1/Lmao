# Third-party libraries (git submodules). Never edit files inside a submodule;
# configure them from here. Each block sets options *before* add_subdirectory.

# Groups third-party targets under one folder in IDEs.
function(tp_folder)
    foreach(target IN LISTS ARGN)
        set_target_properties(${target} PROPERTIES FOLDER "third_party")
    endforeach()
endfunction()

# --- GLFW (zlib) -------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw SYSTEM EXCLUDE_FROM_ALL)

# --- Tracy (BSD-3) -----------------------------------------------------------
option(LMAO_ENABLE_PROFILING "Build with the Tracy profiler client" ON)

set(TRACY_ENABLE    ${LMAO_ENABLE_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON  CACHE BOOL "" FORCE)  # only collect data while the profiler is connected
set(TRACY_STATIC    ON  CACHE BOOL "" FORCE)
add_subdirectory(tracy SYSTEM EXCLUDE_FROM_ALL)

# --- spdlog (MIT) ------------------------------------------------------------
# Compiled library with its bundled fmt. SPDLOG_MSVC_UTF8 (default ON) adds /utf-8.
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH   OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog SYSTEM EXCLUDE_FROM_ALL)

# --- glm (MIT) ---------------------------------------------------------------
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)  # header-only INTERFACE target
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glm SYSTEM EXCLUDE_FROM_ALL)

# Engine conventions (right-handed, depth 0..1). Set on the target so every
# translation unit that includes glm agrees; mismatches are ODR violations.
target_compile_definitions(glm INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RIGHT_HANDED)

tp_folder(glfw TracyClient spdlog)
