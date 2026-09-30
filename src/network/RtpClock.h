#pragma once
// RtpClock — converts RTP timestamp units to wall-clock nanoseconds.
// Anchored by NTP SR (Sender Report) packets received from UxPlay sidecar
// via a side channel (Named Pipe control protocol).

#include <cstdint>
#include <optional>
#include <chrono>
#include <mutex>

namespace duwn::network {

class RtpClock {
public:
    // clock_rate: RTP timestamp units per second (e.g. 90000 for video, 44100/48000 for audio)
    explicit RtpClock(uint32_t clock_rate) noexcept;

    // Anchor: provide NTP-derived mapping (received from RTCP SR or UxPlay metadata).
    // ntp_ns: NTP timestamp expressed as nanoseconds since Unix epoch.
    // rtp_ts: corresponding RTP timestamp value.
    void SetAnchor(int64_t ntp_ns, uint32_t rtp_ts) noexcept;

    // Convert an RTP timestamp to monotonic nanoseconds (MonotonicClock epoch).
    // Returns nullopt if no anchor has been set yet.
    std::optional<int64_t> ToMonotonicNs(uint32_t rtp_ts) const noexcept;

    bool HasAnchor() const noexcept;

private:
    uint32_t m_clock_rate;
    mutable std::mutex m_mutex;
    bool     m_has_anchor{false};
    int64_t  m_anchor_ntp_ns{0};
    uint32_t m_anchor_rtp_ts{0};
    int64_t  m_monotonic_offset_ns{0}; // NTP epoch → MonotonicClock epoch offset
};

} // namespace duwn::network
