# cmake -DINPUT=<file.spv> -DOUTPUT=<file.h> -DNAME=<ArrayName> -DSOURCE=<file.slang> -P EmbedSpirv.cmake
# Writes the SPIR-V module as an inline constexpr uint32_t array (little-endian words), plus
# the source path for shader hot reload.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR remainder "${length} % 8")
if(length EQUAL 0 OR NOT remainder EQUAL 0)
    message(FATAL_ERROR "${INPUT} is not a SPIR-V module (size is not a multiple of 4 bytes)")
endif()

# Bytes b0 b1 b2 b3 -> word 0xb3b2b1b0, eight words per line.
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1u, " words "${hex}")
# (CMake regexes have no {n} quantifier, so the eight-word group is spelled out.)
string(REPEAT "0x........u, " 8 line_pattern)
string(REGEX REPLACE "(${line_pattern})" "\\1\n        " words "${words}")

file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} by cmake/EmbedSpirv.cmake. Do not edit.
#pragma once

#include <cstdint>
#include <string_view>

namespace Lmao::Shaders
{
    inline constexpr uint32_t ${NAME}[] = {
        ${words}
    };

    // What Render::PipelineLibrary recompiles when it changes. Empty in builds without hot
    // reload, so shipping binaries don't carry development paths.
#if LMAO_SHADER_HOT_RELOAD
    inline constexpr std::string_view ${NAME}Source = \"${SOURCE}\";
#else
    inline constexpr std::string_view ${NAME}Source;
#endif
} // namespace Lmao::Shaders
")
