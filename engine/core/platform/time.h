#pragma once

// High-resolution monotonic time. std::chrono::steady_clock is QueryPerformanceCounter on
// MSVC and clock_gettime(CLOCK_MONOTONIC) on Linux, so no per-platform code is needed.

#include <chrono>
#include <cstdint>

namespace Lmao::Time
{

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    static_assert(Clock::is_steady);

    inline TimePoint Now() { return Clock::now(); }

    inline double ToSeconds(Duration duration) { return std::chrono::duration<double>(duration).count(); }
    inline double ToMilliseconds(Duration duration) { return std::chrono::duration<double, std::milli>(duration).count(); }
    inline int64_t ToMicroseconds(Duration duration) { return std::chrono::duration_cast<std::chrono::microseconds>(duration).count(); }

} // namespace Lmao::Time

namespace Lmao
{

    // Measures elapsed time from construction or the last Reset()/Lap().
    // Lap() is the per-frame delta: it returns the time since the previous lap and restarts.
    class Stopwatch
    {
    public:
        Stopwatch() : mStart(Time::Now()) {}

        void Reset() { mStart = Time::Now(); }

        Time::Duration Elapsed() const { return Time::Now() - mStart; }
        double ElapsedSeconds() const { return Time::ToSeconds(Elapsed()); }
        double ElapsedMilliseconds() const { return Time::ToMilliseconds(Elapsed()); }

        double Lap()
        {
            const Time::TimePoint now = Time::Now();
            const double seconds = Time::ToSeconds(now - mStart);
            mStart = now;
            return seconds;
        }

    private:
        Time::TimePoint mStart;
    };

} // namespace Lmao
