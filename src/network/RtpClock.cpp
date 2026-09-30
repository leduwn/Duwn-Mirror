#include "RtpClock.h"
#include "common/clock/MonotonicClock.h"
#include <chrono>

namespace duwn::network {

RtpClock::RtpClock(uint32_t clock_rate) noexcept
    : m_clock_rate(clock_rate) {}

void RtpClock::SetAnchor(int64_t ntp_ns, uint32_t rtp_ts) noexcept {
    // Compute offset between NTP epoch (Unix 1900/1970) and MonotonicClock epoch.
    // MonotonicClock epoch = app start time.
    // We store: monotonic_ns_at_anchor = (MonotonicClock::Now())
    //           ntp_ns_at_anchor       = ntp_ns
    // so: mono(rtp) = (rtp - anchor_rtp) / clock_rate * 1e9 + mono_at_anchor
    int64_t mono_now_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();

    std::lock_guard lock{m_mutex};
    m_anchor_ntp_ns       = ntp_ns;
    m_anchor_rtp_ts       = rtp_ts;
    // monotonic time corresponding to the NTP anchor moment:
    m_monotonic_offset_ns = mono_now_ns; // approximate — refined on next SR
    // Exact offset requires knowing the NTP time NOW as well; approximate for M0.
    m_has_anchor = true;
}

std::optional<int64_t> RtpClock::ToMonotonicNs(uint32_t rtp_ts) const noexcept {
    std::lock_guard lock{m_mutex};
    if (!m_has_anchor) return std::nullopt;

    // Handle 32-bit RTP timestamp wrap (RFC 3550 §A.1).
    int32_t delta_ticks = static_cast<int32_t>(rtp_ts - m_anchor_rtp_ts);
    // delta_ticks in RTP clock units → nanoseconds
    int64_t delta_ns = static_cast<int64_t>(delta_ticks)
                       * 1'000'000'000LL
                       / static_cast<int64_t>(m_clock_rate);

    return m_monotonic_offset_ns + delta_ns;
}

bool RtpClock::HasAnchor() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_has_anchor;
}

} // namespace duwn::network
