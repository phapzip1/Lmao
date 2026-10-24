#pragma once

// Well-known directories, as UTF-8 paths without a trailing separator. They are not created;
// use std::filesystem::create_directories(Lmao::PathFromUtf8(...)) before writing. An empty
// string means the directory can't be determined.

#include <string>
#include <string_view>

namespace Lmao
{

    // The folder containing the running executable.
    std::string GetExecutableDirectory();

    // Per-user application data that should be kept (settings, saves):
    //   Windows  %LOCALAPPDATA%\<application>
    //   Linux    $XDG_DATA_HOME/<application>, default ~/.local/share/<application>
    std::string GetUserDataDirectory(std::string_view application);

    // Per-user data that can be deleted at any time and rebuilt (shader / pipeline caches):
    //   Windows  %LOCALAPPDATA%\<application>\Cache
    //   Linux    $XDG_CACHE_HOME/<application>, default ~/.cache/<application>
    std::string GetUserCacheDirectory(std::string_view application);

} // namespace Lmao
