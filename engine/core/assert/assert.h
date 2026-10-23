#pragma once

// LMAO_ASSERT(cond)               checked when LMAO_ASSERTS_ENABLED; cond is not evaluated otherwise.
// LMAO_ASSERT(cond, "fmt", ...)   same, with a formatted message.
// LMAO_VERIFY(cond, ...)          cond is always evaluated; only checked when LMAO_ASSERTS_ENABLED.
// LMAO_FATAL("fmt", ...)          always reports and breaks, in every build.
//
// A failed check logs at critical level, flushes the logs and breaks into the debugger.
// With no debugger attached the break is caught by the crash handler (dump / backtrace).

#include <spdlog/fmt/fmt.h>

#include <string>
#include <string_view>
#include <utility>

#if defined(_MSC_VER)
#define LMAO_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
#define LMAO_DEBUG_BREAK() __builtin_debugtrap()
#else
#include <csignal>
#define LMAO_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

namespace Lmao::Assert
{

    enum class Action
    {
        Break,   // break into the debugger (default)
        Continue // ignore and keep running, e.g. in tests
    };

    using Handler = Action (*)(const char *expression, const char *file, int line, std::string_view message);

    // Replaces the failure handler; nullptr restores the default. Returns the previous handler.
    Handler SetHandler(Handler handler);

    namespace Detail
    {
        // Returns true if the caller should break.
        bool Fail(const char *expression, const char *file, int line, std::string_view message = {});

        template <typename... Args>
        std::string Format(fmt::format_string<Args...> format, Args &&...args)
        {
            return fmt::format(format, std::forward<Args>(args)...);
        }
    } // namespace Detail

} // namespace Lmao::Assert

#define LMAO_ASSERT_CHECK_(cond, text, ...)                                                                             \
    do                                                                                                              \
    {                                                                                                               \
        if (!(cond)) [[unlikely]]                                                                                   \
        {                                                                                                           \
            if (::Lmao::Assert::Detail::Fail(text, __FILE__, __LINE__ __VA_OPT__(, ::Lmao::Assert::Detail::Format(__VA_ARGS__)))) \
                LMAO_DEBUG_BREAK();                                                                                 \
        }                                                                                                           \
    } while (0)

#define LMAO_FATAL(...)                                                                                     \
    do                                                                                                      \
    {                                                                                                       \
        ::Lmao::Assert::Detail::Fail("LMAO_FATAL", __FILE__, __LINE__, ::Lmao::Assert::Detail::Format(__VA_ARGS__)); \
        LMAO_DEBUG_BREAK();                                                                                 \
    } while (0)

#if LMAO_ASSERTS_ENABLED
#define LMAO_ASSERT(cond, ...) LMAO_ASSERT_CHECK_(cond, #cond __VA_OPT__(, ) __VA_ARGS__)
#define LMAO_VERIFY(cond, ...) LMAO_ASSERT_CHECK_(cond, #cond __VA_OPT__(, ) __VA_ARGS__)
#else
#define LMAO_ASSERT(cond, ...) ((void)sizeof(!(cond)))
#define LMAO_VERIFY(cond, ...) ((void)(cond))
#endif
