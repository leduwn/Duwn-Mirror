#include "MonotonicClock.h"
#include <cassert>
#include <atomic>

namespace duwn::clock {

std::int64_t MonotonicClock::s_freq   = 0;
std::int64_t MonotonicClock::s_origin = 0;

void MonotonicClock::Initialize() noexcept {
    // QueryPerformanceFrequency never fails on Windows XP+.
    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    s_freq = freq.QuadPart;

    LARGE_INTEGER origin{};
    ::QueryPerformanceCounter(&origin);
    s_origin = origin.QuadPart;
}

MonotonicClock::time_point MonotonicClock::Now() noexcept {
    if (s_freq <= 0) {
        Initialize();
    }
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    // Avoid 64-bit overflow: compute delta in ticks first, then scale.
    // ticks/s = s_freq, target: nanoseconds
    // ns = (delta_ticks * 1'000'000'000) / freq
    // Use 128-bit intermediate via __int128 or careful split to avoid overflow.
    std::int64_t delta = now.QuadPart - s_origin;
    // Scale: multiply by 1e9 then divide by freq.
    // s_freq is ~10^7..10^9. delta * 1e9 can overflow int64 after ~9 seconds
    // with a 10^9 Hz counter. Safe approach: divide first, then multiply remainder.
    std::int64_t ns = (delta / s_freq) * 1'000'000'000LL
                    + (delta % s_freq) * 1'000'000'000LL / s_freq;
    return time_point{duration{ns}};
}

MonotonicClock::time_point MonotonicClock::FromQpcTicks(std::int64_t ticks) noexcept {
    if (s_freq <= 0) {
        Initialize();
    }
    std::int64_t delta = ticks - s_origin;
    std::int64_t ns = (delta / s_freq) * 1'000'000'000LL
                    + (delta % s_freq) * 1'000'000'000LL / s_freq;
    return time_point{duration{ns}};
}

std::int64_t MonotonicClock::NowQpcTicks() noexcept {
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    return now.QuadPart;
}

double MonotonicClock::QpcDeltaMs(std::int64_t start_ticks, std::int64_t end_ticks) noexcept {
    if (s_freq <= 0) return 0.0;
    return static_cast<double>(end_ticks - start_ticks) * 1000.0 / static_cast<double>(s_freq);
}

} // namespace duwn::clock
