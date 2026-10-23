cmake_minimum_required(VERSION 3.25)

# A configure that ran where cl.exe wasn't found (outside a VS developer environment) caches a
# GNU linker, e.g. ld.exe from a MinGW/GCC on PATH. Later configures keep it and every link fails,
# so catch it before project() runs the compiler checks.
if(CMAKE_CXX_COMPILER AND CMAKE_LINKER)
    get_filename_component(lmao_compiler_name "${CMAKE_CXX_COMPILER}" NAME_WE)
    get_filename_component(lmao_linker_name "${CMAKE_LINKER}" NAME_WE)
    string(TOLOWER "${lmao_compiler_name}" lmao_compiler_name)
    string(TOLOWER "${lmao_linker_name}" lmao_linker_name)
    if(lmao_compiler_name MATCHES "^(cl|clang-cl)$" AND NOT lmao_linker_name MATCHES "^(link|lld-link)$")
        message(FATAL_ERROR
            "MSVC compiler with a non-MSVC linker cached: ${CMAKE_LINKER}\n"
            "Delete this build directory (or run 'CMake: Delete Cache and Reconfigure' in VS Code) "
            "and configure from a Visual Studio developer environment.")
    endif()
endif()

project(Lmao LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The engine doesn't use C++ modules. With scanning on, CMake 4.5 adds `import std`
# support to every C++20 target, which breaks C-only targets like glfw under Ninja
# (missing CXXInterfaceObjects.rsp). Set before third_party so it applies there too.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LmaoWarnings)

add_subdirectory(third_party SYSTEM)

add_subdirectory(engine)
add_subdirectory(game)