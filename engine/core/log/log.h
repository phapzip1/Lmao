#pragma once

// Engine logging on top of spdlog. Format strings use fmt syntax: LMAO_INFO("{} x {}", w, h).
//
// LMAO_INFO(...)               logs to the "core" channel.
// LMAO_LOG_INFO(channel, ...)  logs to a channel from Lmao::Log::Channel("render").
//
// Calls below SPDLOG_ACTIVE_LEVEL (trace in Debug, info otherwise) compile to nothing.
// Loggers lock internally: never log from the audio thread.

#include <spdlog/spdlog.h>

#include <string_view>

namespace Lmao::Log
{

    enum class Level
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

    struct Config
    {
        Level MinLevel = Level::Trace; // runtime filter on top of the compile-time one
        const char *FilePath = nullptr; // UTF-8; nullptr disables the file sink
    };

    // Creates the shared sinks and the "core" channel. Call once at startup, before other threads log.
    void Init(const Config &config = {});

    // Flushes and destroys all channels. Logging after this goes to spdlog's default console logger.
    void Shutdown();

    // Returns the channel with this name, creating it on first use. Thread-safe.
    // The pointer stays valid until Shutdown(); cache it in a static.
    spdlog::logger *Channel(std::string_view name);

    // The "core" channel (spdlog's default logger before Init()).
    inline spdlog::logger *Core() { return spdlog::default_logger_raw(); }

    void SetLevel(Level level);
    void Flush();

} // namespace Lmao::Log

#define LMAO_LOG_TRACE(channel, ...)    SPDLOG_LOGGER_TRACE(channel, __VA_ARGS__)
#define LMAO_LOG_DEBUG(channel, ...)    SPDLOG_LOGGER_DEBUG(channel, __VA_ARGS__)
#define LMAO_LOG_INFO(channel, ...)     SPDLOG_LOGGER_INFO(channel, __VA_ARGS__)
#define LMAO_LOG_WARN(channel, ...)     SPDLOG_LOGGER_WARN(channel, __VA_ARGS__)
#define LMAO_LOG_ERROR(channel, ...)    SPDLOG_LOGGER_ERROR(channel, __VA_ARGS__)
#define LMAO_LOG_CRITICAL(channel, ...) SPDLOG_LOGGER_CRITICAL(channel, __VA_ARGS__)

#define LMAO_TRACE(...)    LMAO_LOG_TRACE(::Lmao::Log::Core(), __VA_ARGS__)
#define LMAO_DEBUG(...)    LMAO_LOG_DEBUG(::Lmao::Log::Core(), __VA_ARGS__)
#define LMAO_INFO(...)     LMAO_LOG_INFO(::Lmao::Log::Core(), __VA_ARGS__)
#define LMAO_WARN(...)     LMAO_LOG_WARN(::Lmao::Log::Core(), __VA_ARGS__)
#define LMAO_ERROR(...)    LMAO_LOG_ERROR(::Lmao::Log::Core(), __VA_ARGS__)
#define LMAO_CRITICAL(...) LMAO_LOG_CRITICAL(::Lmao::Log::Core(), __VA_ARGS__)
