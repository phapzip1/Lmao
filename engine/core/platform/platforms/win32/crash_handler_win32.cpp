#include "core/platform/crash_handler.h"

#include "core/io/path.h"
#include "core/log/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h must come after windows.h
#include <dbghelp.h>

#include <csignal>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <system_error>

namespace Lmao::CrashHandler
{
    namespace
    {

        // Raised for std::terminate / abort so they reach the unhandled-exception filter.
        constexpr DWORD fatal_exception_code = 0xE04C4D41; // 'LMA' with the customer bit set

        // Prepared at Install(): the filter runs in a broken process and should not allocate.
        wchar_t dump_directory[MAX_PATH * 2];

        LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS *exception)
        {
            SYSTEMTIME now;
            GetLocalTime(&now);
            wchar_t path[MAX_PATH * 2 + 64];
            swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\lmao-%04d%02d%02d-%02d%02d%02d.dmp", dump_directory,
                     now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

            // Write the dump first: logging below might fail in a corrupted process.
            bool written = false;
            DWORD error = 0;
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                error = GetLastError();
            else
            {
                MINIDUMP_EXCEPTION_INFORMATION info{};
                info.ThreadId = GetCurrentThreadId();
                info.ExceptionPointers = exception;
                info.ClientPointers = FALSE;

                const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                                                             MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
                written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr) != FALSE;
                if (!written)
                    error = GetLastError();
                CloseHandle(file);
            }

            const DWORD code = exception->ExceptionRecord->ExceptionCode;
            if (written)
                LMAO_CRITICAL("Fatal exception 0x{:08X}; minidump written to '{}'", code, PathToUtf8(path));
            else
                LMAO_CRITICAL("Fatal exception 0x{:08X}; writing the minidump failed (error {})", code, error);
            Log::Flush();

            return EXCEPTION_EXECUTE_HANDLER; // terminate the process
        }

        void raise_fatal() { RaiseException(fatal_exception_code, EXCEPTION_NONCONTINUABLE, 0, nullptr); }

        void on_abort_signal(int) { raise_fatal(); }

        void on_terminate()
        {
            if (std::exception_ptr exception = std::current_exception())
            {
                try
                {
                    std::rethrow_exception(exception);
                }
                catch (const std::exception &e)
                {
                    LMAO_CRITICAL("std::terminate: unhandled exception: {}", e.what());
                }
                catch (...)
                {
                    LMAO_CRITICAL("std::terminate: unhandled non-standard exception");
                }
            }
            else
            {
                LMAO_CRITICAL("std::terminate called");
            }
            Log::Flush();
            raise_fatal();
        }

        void on_invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
        {
            LMAO_CRITICAL("CRT invalid parameter");
            raise_fatal();
        }

        void on_pure_call()
        {
            LMAO_CRITICAL("Pure virtual function call");
            raise_fatal();
        }

    } // namespace

    void Install(const Config &config)
    {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::absolute(PathFromUtf8(config.DumpDirectory), error);
        std::filesystem::create_directories(directory, error);
        if (error)
            LMAO_WARN("Crash handler: cannot create '{}': {}", config.DumpDirectory, error.message());
        wcsncpy_s(dump_directory, directory.c_str(), _TRUNCATE);

        // Leave room for the filter to run on a thread that overflowed its stack.
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);

        SetUnhandledExceptionFilter(&on_unhandled_exception);
        std::set_terminate(&on_terminate);
        std::signal(SIGABRT, &on_abort_signal);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // no abort() message box
        _set_invalid_parameter_handler(&on_invalid_parameter);
        _set_purecall_handler(&on_pure_call);

        LMAO_INFO("Crash handler installed; minidumps go to '{}'", PathToUtf8(directory));
    }

} // namespace Lmao::CrashHandler
