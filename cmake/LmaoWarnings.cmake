# Strict warnings for Lmao-owned targets only. Never call this on third_party targets.
function(lmao_set_warnings target)
    # CMAKE_CXX_COMPILER_FRONTEND_VARIANT is "MSVC" for both cl.exe and clang-cl
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    # Applies /WX or -Werror; turn off with `cmake --compile-no-warning-as-error`
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endfunction()