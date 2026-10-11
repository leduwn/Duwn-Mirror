#pragma once

#include <cstdint>
#include <cstdlib>

namespace duwn::video {

// Builds a relative presentation timeline from RTP deltas without claiming
// synchronization between the sender clock and the local monotonic clock.
class RtpTimestampMapper {
public:
    explicit RtpTimestampMapper(uint32_t clock_rate = 90'000) noexcept
        : m_clock_rate(clock_rate) {}

    int64_t Map(uint32_t timestamp, int64_t arrival_ns) noexcept {
        if (m_clock_rate == 0 || arrival_ns <= 0) return arrival_ns;
        if (!m_initialized) {
            m_initialized = true;
            m_last_timestamp = timestamp;
            m_last_pts_ns = arrival_ns;
            return arrival_ns;
        }

        const int32_t delta_ticks = static_cast<int32_t>(timestamp - m_last_timestamp);
        const int64_t discontinuity_ticks = static_cast<int64_t>(m_clock_rate) * 10;
        if (std::abs(static_cast<int64_t>(delta_ticks)) > discontinuity_ticks) {
            m_last_timestamp = timestamp;
            m_last_pts_ns = arrival_ns;
            m_fraction = 0;
            return arrival_ns;
        }

        m_last_timestamp = timestamp;
        const int64_t scaled = static_cast<int64_t>(delta_ticks) * 1'000'000'000LL + m_fraction;
        m_last_pts_ns += scaled / static_cast<int64_t>(m_clock_rate);
        m_fraction = scaled % static_cast<int64_t>(m_clock_rate);
        return m_last_pts_ns;
    }

    void Reset() noexcept {
        m_initialized = false;
        m_last_timestamp = 0;
        m_last_pts_ns = 0;
        m_fraction = 0;
    }

private:
    uint32_t m_clock_rate;
    bool m_initialized{false};
    uint32_t m_last_timestamp{0};
    int64_t m_last_pts_ns{0};
    int64_t m_fraction{0};
};

} // namespace duwn::video
