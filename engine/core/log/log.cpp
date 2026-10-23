#include "core/log/log.h"

#include "core/io/path.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if defined(_WIN32)
#include <spdlog/sinks/msvc_sink.h>
#endif

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Lmao::Log
{
    namespace
    {

        static_assert(static_cast<int>(Level::Trace) == spdlog::level::trace);
        static_assert(static_cast<int>(Level::Critical) == spdlog::level::critical);
        static_assert(static_cast<int>(Level::Off) == spdlog::level::off);

        constexpr const char *console_pattern = "[%T.%e] [%^%l%$] [%n] %v";
        constexpr const char *file_pattern = "[%Y-%m-%d %T.%e] [%l] [%n] [thread %t] %v (%s:%#)";

        struct State
        {
            std::mutex mutex;
            std::vector<spdlog::sink_ptr> sinks;
            spdlog::level::level_enum level = spdlog::level::trace;
            bool initialized = false;
        };

        State &state()
        {
            static State s;
            return s;
        }

        spdlog::level::level_enum to_spdlog(Level level)
        {
            return static_cast<spdlog::level::level_enum>(level);
        }

        // Caller holds state().mutex.
        std::shared_ptr<spdlog::logger> make_logger(State &s, std::string name)
        {
            auto logger = std::make_shared<spdlog::logger>(std::move(name), s.sinks.begin(), s.sinks.end());
            logger->set_level(s.level);
            logger->flush_on(spdlog::level::warn); // keep warnings and errors if we crash right after
            spdlog::register_logger(logger);
            return logger;
        }

        void init_lock(State &s, const Config &config)
        {
            if (s.initialized)
                return;

            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern(console_pattern);
            s.sinks.push_back(console);

#if defined(_WIN32)
            // Visual Studio / debugger Output window.
            auto debugger = std::make_shared<spdlog::sinks::msvc_sink_mt>(/*check_debugger_present=*/true);
            debugger->set_pattern(console_pattern);
            s.sinks.push_back(debugger);
#endif

            if (!config.FilePath.empty())
            {
                auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(PathFromUtf8(config.FilePath).native(), /*truncate=*/true);
                file->set_pattern(file_pattern);
                s.sinks.push_back(file);
            }

            s.level = to_spdlog(config.MinLevel);
            spdlog::set_default_logger(make_logger(s, "core"));
            s.initialized = true;
        }



    } // namespace

    void Init(const Config &config)
    {
        State &s = state();
        std::lock_guard lock(s.mutex);
        init_lock(s, config);
    }

    void Shutdown()
    {
        State &s = state();
        std::lock_guard lock(s.mutex);
        if (!s.initialized)
            return;

        spdlog::shutdown(); // flushes, drops every registered logger
        spdlog::set_default_logger(spdlog::stdout_color_mt("default"));
        s.sinks.clear();
        s.initialized = false;
    }

    spdlog::logger *Channel(std::string_view name)
    {
        State &s = state();
        std::lock_guard lock(s.mutex);
        if (!state().initialized)
            init_lock(s, {});

        std::string key(name);
        if (auto existing = spdlog::get(key))
            return existing.get();
        return make_logger(s, std::move(key)).get(); // the registry keeps it alive
    }

    void SetLevel(Level level)
    {
        State &s = state();
        std::lock_guard lock(s.mutex);
        s.level = to_spdlog(level);
        spdlog::set_level(s.level); // applies to every registered channel
    }

    void Flush()
    {
        spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger)
                          { logger->flush(); });
    }

} // namespace Lmao::Log
