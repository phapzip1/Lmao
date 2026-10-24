#include "core/platform/paths.h"

#include <unistd.h>

#include <cstdlib>
#include <string>

namespace Lmao
{
    namespace
    {

        // $variable if set and absolute (the XDG spec says to ignore relative ones), else
        // $HOME/fallback, else "".
        std::string xdg_directory(const char *variable, const char *fallback)
        {
            if (const char *value = std::getenv(variable); value && value[0] == '/')
                return value;
            if (const char *home = std::getenv("HOME"); home && home[0] != '\0')
                return std::string(home) + "/" + fallback;
            return {};
        }

        std::string with_application(std::string directory, std::string_view application)
        {
            if (directory.empty())
                return {};
            return directory + "/" + std::string(application);
        }

    } // namespace

    std::string GetExecutableDirectory()
    {
        // Linux: the kernel's link to the executable (paths are bytes; UTF-8 on any sane system).
        std::string path(4096, '\0');
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length <= 0 || static_cast<size_t>(length) >= path.size())
            return {};
        path.resize(static_cast<size_t>(length));
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::string GetUserDataDirectory(std::string_view application)
    {
        return with_application(xdg_directory("XDG_DATA_HOME", ".local/share"), application);
    }

    std::string GetUserCacheDirectory(std::string_view application)
    {
        return with_application(xdg_directory("XDG_CACHE_HOME", ".cache"), application);
    }

} // namespace Lmao
