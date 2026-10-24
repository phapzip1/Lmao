# Slang shader compilation (docs/decisions/0006).
#
#   lmao_add_shaders(<target> <file.slang>...)
#
# Compiles every entry point of each file into one SPIR-V module at build time and embeds it in
# a generated header the target can include:
#
#   #include "shaders/triangle.spv.h"   // from shaders/triangle.slang
#   Lmao::Shaders::Triangle             // inline constexpr uint32_t[] (SPIR-V words)
#
# The array name is the file name in PascalCase (my_shader.slang -> MyShader).
# slangc comes from tools/bin/slang (scripts/setup.sh / setup.ps1), or LMAO_SLANGC if set.

set(LMAO_SLANGC "" CACHE FILEPATH "Path to slangc; empty = tools/bin/slang/bin/slangc")

function(lmao_find_slangc out_var)
    if(LMAO_SLANGC)
        set(${out_var} "${LMAO_SLANGC}" PARENT_SCOPE)
        return()
    endif()
    find_program(lmao_slangc_path slangc
        PATHS "${PROJECT_SOURCE_DIR}/tools/bin/slang/bin"
        NO_DEFAULT_PATH)
    if(NOT lmao_slangc_path)
        if(CMAKE_HOST_WIN32)
            set(setup "powershell -ExecutionPolicy Bypass -File scripts/setup.ps1")
        else()
            set(setup "scripts/setup.sh")
        endif()
        message(FATAL_ERROR "slangc not found in tools/bin/slang/bin. Run `${setup}` from the repository root, "
                            "then configure again (or set LMAO_SLANGC).")
    endif()
    set(${out_var} "${lmao_slangc_path}" PARENT_SCOPE)
endfunction()

function(lmao_add_shaders target)
    lmao_find_slangc(slangc)
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

    foreach(source IN LISTS ARGN)
        get_filename_component(source_path "${source}" ABSOLUTE)
        get_filename_component(name "${source}" NAME_WE)

        # my_shader -> MyShader
        string(REPLACE "_" ";" words "${name}")
        set(array_name "")
        foreach(word IN LISTS words)
            string(SUBSTRING "${word}" 0 1 first)
            string(SUBSTRING "${word}" 1 -1 rest)
            string(TOUPPER "${first}" first)
            string(APPEND array_name "${first}${rest}")
        endforeach()

        set(spv "${generated_dir}/shaders/${name}.spv")
        set(header "${generated_dir}/shaders/${name}.spv.h")
        add_custom_command(
            OUTPUT "${spv}" "${header}" # spv first: slangc's depfile names it as the target
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}/shaders"
            COMMAND "${slangc}" "${source_path}"
                    -target spirv -profile spirv_1_6
                    -fvk-use-entrypoint-name    # keep vertexMain/fragmentMain as SPIR-V entry names
                    -matrix-layout-column-major # matches glm and the engine convention
                    -warnings-as-errors all
                    -warnings-disable 41012     # "profile implicitly upgraded": Texture2D lists optional
                                                # capabilities; only the ones used are emitted
                    -I "${PROJECT_SOURCE_DIR}/shaders" # import common.bindless;
                    "$<IF:$<CONFIG:Debug>,-g;-O0,-O2>" # -g: source-level debugging in RenderDoc
                    -depfile "${spv}.d"
                    -o "${spv}"
            COMMAND "${CMAKE_COMMAND}" -DINPUT=${spv} -DOUTPUT=${header} -DNAME=${array_name}
                    -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            MAIN_DEPENDENCY "${source_path}"
            DEPENDS "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPFILE "${spv}.d"
            COMMAND_EXPAND_LISTS
            COMMENT "Compiling shader ${name}.slang"
            VERBATIM)
        target_sources(${target} PRIVATE "${header}")
    endforeach()

    target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()
