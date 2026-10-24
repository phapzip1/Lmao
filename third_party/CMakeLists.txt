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
# No VK_USE_PLATFORM_* defines: surface functions (Win32, X11, Wayland) are loaded with
# vkGetInstanceProcAddr in engine/rhi/rhis/vulkan/vulkan_surface.cpp, so <windows.h> and
# <X11/Xlib.h> stay out of the rest of the backend. Forced empty (not just unset) so build
# directories configured before this change drop the old cached value.
set(VOLK_STATIC_DEFINES "" CACHE STRING "" FORCE)
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

# --- cgltf (MIT) -------------------------------------------------------------
# Single-header glTF 2.0 parser, no CMake. CGLTF_IMPLEMENTATION is defined in exactly one
# engine file: engine/resource/third_party_impl.cpp.
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE cgltf)

# --- stb (MIT / public domain) -------------------------------------------------
# Single-header libraries, no CMake; only stb_image is used. STB_IMAGE_IMPLEMENTATION (with
# STBI_NO_STDIO: the engine does its own file IO) is defined in
# engine/resource/third_party_impl.cpp.
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE stb)

tp_folder(glfw TracyClient spdlog volk)
