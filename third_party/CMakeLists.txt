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
if(WIN32)
    # filename_t = std::wstring, so UTF-8 paths (via Lmao::PathFromUtf8) open correctly.
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
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

# --- Vulkan-Headers (Apache-2.0 / MIT) ---------------------------------------
# Header-only INTERFACE target Vulkan::Headers. Keep the tag in sync with volk.
set(VULKAN_HEADERS_ENABLE_TESTS   OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_MODULE  OFF CACHE BOOL "" FORCE)  # C++ modules are off project-wide
add_subdirectory(Vulkan-Headers SYSTEM EXCLUDE_FROM_ALL)

# --- volk (MIT) --------------------------------------------------------------
# Vulkan function loader: the engine never links vulkan-1.lib / libvulkan.so.
# VOLK_PULL_IN_VULKAN is off because volk would otherwise prefer an installed Vulkan SDK's
# headers over the submodule; Vulkan::Headers is linked explicitly below instead.
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_INSTALL        OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY   OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Loads vkCreateWin32SurfaceKHR for surfaces made from native window handles.
    # vulkan.h then includes <windows.h>, so trim it.
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX CACHE STRING "" FORCE)
endif()
# Linux needs no defines here: X11/Wayland surface functions are loaded with vkGetInstanceProcAddr
# in engine/rhi/rhis/vulkan/vulkan_surface.cpp, keeping <X11/Xlib.h> macros out of the backend.
add_subdirectory(volk SYSTEM EXCLUDE_FROM_ALL)
target_link_libraries(volk PUBLIC Vulkan::Headers)

# --- VulkanMemoryAllocator (MIT) ---------------------------------------------
# Header-only INTERFACE target GPUOpen::VulkanMemoryAllocator. VMA_IMPLEMENTATION goes in
# exactly one .cpp in engine/rhi/rhis/vulkan/, with VMA_STATIC_VULKAN_FUNCTIONS=0 and
# VMA_DYNAMIC_VULKAN_FUNCTIONS=1 (functions come from volk).
set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(VMA_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
add_subdirectory(VulkanMemoryAllocator SYSTEM EXCLUDE_FROM_ALL)
target_link_libraries(VulkanMemoryAllocator INTERFACE Vulkan::Headers)

tp_folder(glfw TracyClient spdlog volk)
