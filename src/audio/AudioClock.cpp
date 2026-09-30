#include "AudioClock.h"
#include "common/clock/MonotonicClock.h"

namespace duwn::audio {

void AudioClock::Update(uint32_t rtp_ts, uint32_t clock_rate) noexcept {
    std::lock_guard lock{m_mutex};
    if (!m_anchored) {
        m_anchor_rtp     = rtp_ts;
        m_anchor_mono_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
        m_clock_rate     = clock_rate;
        m_anchored       = true;
    }
    // Always update clock rate if it changes (shouldn't happen mid-session)
    m_clock_rate = clock_rate;
}

int64_t AudioClock::CurrentPtsNs() const noexcept {
    std::lock_guard lock{m_mutex};
    if (!m_anchored) return 0;
    // Current audio PTS = anchor_time + elapsed_rtp / rate
    int64_t now_ns   = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    int64_t elapsed  = now_ns - m_anchor_mono_ns;
    return m_anchor_mono_ns + elapsed; // simplified: just return current mono time as audio PTS
    // Full implementation tracks RTP deltas in Milestone 3.
}

bool AudioClock::HasAnchor() const noexcept {
    std::lock_guard lock{m_mutex};
    return m_anchored;
}

} // namespace duwn::audio
