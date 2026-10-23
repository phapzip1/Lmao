#pragma once

// Conversions between the engine's UTF-8 strings and std::filesystem::path.
// Use these instead of constructing a path from std::string: on Windows that
// decodes with the ANSI code page, not UTF-8.

#include <filesystem>
#include <string>
#include <string_view>

namespace Lmao
{

    // UTF-8 -> path. On Windows the path holds UTF-16; .native() / .c_str() feed W APIs directly.
    inline std::filesystem::path PathFromUtf8(std::string_view utf8)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
    }

    // path -> UTF-8, for logging, UI and serialization.
    inline std::string PathToUtf8(const std::filesystem::path &path)
    {
        const std::u8string u8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
    }

} // namespace Lmao
