#include "core/platform/paths.h"

#include "core/io/path.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h
#include <knownfolders.h>
#include <shlobj.h>

#include <filesystem>
#include <string>

namespace Lmao
{
    namespace
    {

        std::string local_app_data()
        {
            PWSTR path = nullptr;
            std::string result;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path)))
                result = PathToUtf8(std::filesystem::path(path));
            CoTaskMemFree(path);
            return result;
        }

    } // namespace

    std::string GetExecutableDirectory()
    {
        // GetModuleFileNameW truncates silently: grow until the path fits.
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
                return {};
            if (length < path.size())
            {
                path.resize(length);
                break;
            }
            path.resize(path.size() * 2);
        }
        return PathToUtf8(std::filesystem::path(path).parent_path());
    }

    std::string GetUserDataDirectory(std::string_view application)
    {
        const std::string base = local_app_data();
        return base.empty() ? std::string() : base + "\\" + std::string(application);
    }

    std::string GetUserCacheDirectory(std::string_view application)
    {
        const std::string data = GetUserDataDirectory(application);
        return data.empty() ? std::string() : data + "\\Cache";
    }

} // namespace Lmao
