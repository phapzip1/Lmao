#include "core/assert/assert.h"

#include "core/log/log.h"

#include <atomic>

namespace Lmao::Assert
{
    namespace
    {

        Action default_handler(const char *expression, const char *file, int line, std::string_view message)
        {
            if (message.empty())
                LMAO_CRITICAL("Assertion failed: {} ({}:{})", expression, file, line);
            else
                LMAO_CRITICAL("Assertion failed: {} ({}:{}): {}", expression, file, line, message);
            Log::Flush(); // the break below may end the process
            return Action::Break;
        }

        std::atomic<Handler> current_handler{&default_handler};

    } // namespace

    Handler SetHandler(Handler handler)
    {
        return current_handler.exchange(handler ? handler : &default_handler);
    }

    namespace Detail
    {
        bool Fail(const char *expression, const char *file, int line, std::string_view message)
        {
            return current_handler.load()(expression, file, line, message) == Action::Break;
        }
    } // namespace Detail

} // namespace Lmao::Assert
