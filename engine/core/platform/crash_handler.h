#pragma once

// Writes crash information when the process dies from an unhandled exception, a fatal signal,
// std::terminate, abort, or a failed assert with no debugger attached.
//
// Windows: a minidump (lmao-<date>-<time>.dmp) via MiniDumpWriteDump. Open it in Visual Studio
//          or WinDbg together with the matching .pdb.
// Linux:   a backtrace (lmao-crash-<pid>.txt) via backtrace_symbols_fd, also printed to stderr.
//          The signal is then re-raised so the OS can still produce a core dump.

#include <string_view>

namespace Lmao::CrashHandler
{

    struct Config
    {
        std::string_view DumpDirectory = "crashes"; // UTF-8; created if missing
    };

    // Call once at startup on the main thread, after Log::Init().
    void Install(const Config &config = {});

} // namespace Lmao::CrashHandler
