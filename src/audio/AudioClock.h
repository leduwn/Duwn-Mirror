#pragma once
// AudioClock — tracks audio playback position from RTP timestamps.
// Used by AvSynchronizer for A/V offset measurement.

#include <cstdint>
#include <atomic>
#include <mutex>

namespace duwn::audio {

class AudioClock {
public:
    // Called on each audio packet received.
    void Update(uint32_t rtp_ts, uint32_t clock_rate) noexcept;

    // Returns current audio PTS in MonotonicClock nanoseconds.
    // Returns 0 if not yet anchored.
    int64_t CurrentPtsNs() const noexcept;

    bool HasAnchor() const noexcept;

private:
    mutable std::mutex m_mutex;
    bool     m_anchored{false};
    uint32_t m_anchor_rtp{0};
    int64_t  m_anchor_mono_ns{0};
    uint32_t m_clock_rate{48000};
};

} // namespace duwn::audio
