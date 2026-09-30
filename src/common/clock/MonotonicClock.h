#pragma once
// MonotonicClock — thin wrapper around QueryPerformanceCounter.
// All return values are std::chrono types so callers cannot confuse units.

#include <chrono>
#include <cstdint>
#include <windows.h>

namespace duwn::clock {

// High-resolution monotonic clock backed by QueryPerformanceCounter.
// Suitable for frame timing, A/V sync, drift tracking.
// NOT wall-clock time. NOT affected by system clock adjustments.
class MonotonicClock {
public:
    using rep        = std::int64_t;
    using period     = std::nano;
    using duration   = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<MonotonicClock>;

    static constexpr bool is_steady = true;

    // Initialise once at startup. Thread-safe; safe to call multiple times.
    static void Initialize() noexcept;

    // Current time as nanoseconds since Initialize() was called.
    static time_point Now() noexcept;

    // Convert a QPC tick count to a MonotonicClock time_point.
    static time_point FromQpcTicks(std::int64_t ticks) noexcept;

    // Elapsed nanoseconds since a prior snapshot.
    static duration Elapsed(time_point since) noexcept {
        return Now() - since;
    }

    // Direct QPC tick count.
    static std::int64_t NowQpcTicks() noexcept;

    // Convert QPC tick delta to milliseconds.
    static double QpcDeltaMs(std::int64_t start_ticks, std::int64_t end_ticks) noexcept;

    // Convert QPC frequency.
    static std::int64_t Frequency() noexcept { return s_freq; }

private:
    static std::int64_t s_freq;   // QPC frequency (ticks/second)
    static std::int64_t s_origin; // QPC tick at Initialize()
};

} // namespace duwn::clock
