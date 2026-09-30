// Tests for RtpClock

#include "network/RtpClock.h"

DUWN_TEST(rtp_clock_no_anchor) {
    duwn::network::RtpClock clock{90000};
    DUWN_ASSERT(!clock.HasAnchor());
    auto result = clock.ToMonotonicNs(0);
    DUWN_ASSERT(!result.has_value());
}

DUWN_TEST(rtp_clock_with_anchor) {
    duwn::network::RtpClock clock{90000};
    clock.SetAnchor(0LL, 0u);
    DUWN_ASSERT(clock.HasAnchor());
    // RTP ts=0 → same as anchor
    auto r = clock.ToMonotonicNs(0u);
    DUWN_ASSERT(r.has_value());
}

DUWN_TEST(rtp_clock_delta_90k) {
    // 90000 ticks = 1 second at 90kHz
    duwn::network::RtpClock clock{90000};
    clock.SetAnchor(0LL, 1000u);
    // ts = 1000 + 90000 → should be 1 second later
    auto base = clock.ToMonotonicNs(1000u);
    auto plus1s = clock.ToMonotonicNs(91000u);
    DUWN_ASSERT(base.has_value() && plus1s.has_value());
    int64_t diff = *plus1s - *base;
    // Should be ~1,000,000,000 ns (1 second)
    DUWN_ASSERT(diff == 1'000'000'000LL);
}

DUWN_TEST(rtp_clock_wrap_32bit) {
    // Sequence wrap: 0xFFFF0000 + 90000 wraps to positive
    duwn::network::RtpClock clock{90000};
    clock.SetAnchor(0LL, 0xFFFF0000u);
    // 1s later: 0xFFFF0000 + 90000 overflows uint32, lands at correct value
    uint32_t ts_1s = 0xFFFF0000u + 90000u;
    auto r = clock.ToMonotonicNs(ts_1s);
    DUWN_ASSERT(r.has_value());
}
