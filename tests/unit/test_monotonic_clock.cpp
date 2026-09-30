// Tests for MonotonicClock

#include <thread>
#include <chrono>

DUWN_TEST(monotonic_clock_non_decreasing) {
    duwn::clock::MonotonicClock::Initialize();
    auto t1 = duwn::clock::MonotonicClock::Now();
    auto t2 = duwn::clock::MonotonicClock::Now();
    DUWN_ASSERT(t2 >= t1);
}

DUWN_TEST(monotonic_clock_roughly_correct) {
    // Sleep 10ms, verify at least 8ms elapsed (generous margin for scheduler jitter)
    duwn::clock::MonotonicClock::Initialize();
    auto t1 = duwn::clock::MonotonicClock::Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = duwn::clock::MonotonicClock::Now();
    auto elapsed_ns = (t2 - t1).count();
    DUWN_ASSERT(elapsed_ns >= 8'000'000LL);  // at least 8ms
    DUWN_ASSERT(elapsed_ns < 500'000'000LL); // less than 500ms (sanity)
}

DUWN_TEST(monotonic_clock_from_qpc) {
    duwn::clock::MonotonicClock::Initialize();
    LARGE_INTEGER ticks{};
    ::QueryPerformanceCounter(&ticks);
    auto from_qpc = duwn::clock::MonotonicClock::FromQpcTicks(ticks.QuadPart);
    auto now      = duwn::clock::MonotonicClock::Now();
    // Should be very close
    int64_t diff = std::abs((now - from_qpc).count());
    DUWN_ASSERT(diff < 1'000'000LL); // < 1ms
}
