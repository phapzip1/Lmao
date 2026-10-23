#include "core/platform/crash_handler.h"

#include "core/io/path.h"
#include "core/log/log.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace Lmao::CrashHandler
{
    namespace
    {

        constexpr int fatal_signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP};

        // Everything the signal handler needs is prepared up front: it may only call
        // async-signal-safe functions, so no allocation, no locks, no spdlog.
        char crash_file_path[4096];
        alignas(16) char alternate_stack[64 * 1024]; // lets the handler run after a stack overflow

        void write_str(int fd, const char *text)
        {
            if (fd >= 0)
                (void)!write(fd, text, std::strlen(text));
        }

        void on_fatal_signal(int signal_number, siginfo_t *, void *)
        {
            const int file = open(crash_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            char header[64];
            // snprintf is not formally async-signal-safe but does not allocate for %d in glibc.
            std::snprintf(header, sizeof(header), "Fatal signal %d (%s)\n", signal_number, sigabbrev_np(signal_number));
            for (int fd : {STDERR_FILENO, file})
            {
                write_str(fd, header);
                write_str(fd, "Backtrace:\n");
            }

            void *frames[128];
            const int count = backtrace(frames, 128);
            backtrace_symbols_fd(frames, count, STDERR_FILENO);
            if (file >= 0)
            {
                backtrace_symbols_fd(frames, count, file);
                close(file);
            }

            // SA_RESETHAND restored the default action: re-raise to terminate (and core dump).
            raise(signal_number);
        }

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
            std::abort(); // SIGABRT -> on_fatal_signal
        }

    } // namespace

    void Install(const Config &config)
    {
        std::error_code error;
        const std::filesystem::path directory = PathFromUtf8(config.DumpDirectory);
        std::filesystem::create_directories(directory, error);
        if (error)
            LMAO_WARN("Crash handler: cannot create '{}': {}", config.DumpDirectory, error.message());

        const std::filesystem::path path = directory / ("lmao-crash-" + std::to_string(getpid()) + ".txt");
        std::snprintf(crash_file_path, sizeof(crash_file_path), "%s", path.c_str());

        // backtrace() loads libgcc on first use, which allocates; do that now, not in the handler.
        void *warm_up[1];
        backtrace(warm_up, 1);

        stack_t stack{};
        stack.ss_sp = alternate_stack;
        stack.ss_size = sizeof(alternate_stack);
        sigaltstack(&stack, nullptr);

        struct sigaction action{};
        action.sa_sigaction = &on_fatal_signal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        sigemptyset(&action.sa_mask);
        for (int signal_number : fatal_signals)
            sigaction(signal_number, &action, nullptr);

        std::set_terminate(&on_terminate);
        LMAO_INFO("Crash handler installed; reports go to '{}'", PathToUtf8(path));
    }

} // namespace Lmao::CrashHandler
