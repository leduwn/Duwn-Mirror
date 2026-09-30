#pragma once
// AvSynchronizer — measures A/V offset.
// Milestone 0: basic measurement only. Correction in Milestone 3.

#include <atomic>
#include <cstdint>

namespace duwn::sync {

class AvSynchronizer {
public:
    // Report video PTS at render time.
    void ReportVideoRender(int64_t video_pts_ns, int64_t render_wall_ns) noexcept;

    // Report audio PTS at play time.
    void ReportAudioPlay(int64_t audio_pts_ns, int64_t play_wall_ns) noexcept;

    // A/V offset in milliseconds: positive = video ahead of audio.
    double OffsetMs() const noexcept;

private:
    std::atomic<int64_t> m_last_video_pts{0};
    std::atomic<int64_t> m_last_audio_pts{0};
    std::atomic<double>  m_offset_ms{0.0};
};

} // namespace duwn::sync
