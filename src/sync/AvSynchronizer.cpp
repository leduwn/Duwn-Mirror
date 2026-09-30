#include "AvSynchronizer.h"
#include "common/metrics/Metrics.h"

namespace duwn::sync {

void AvSynchronizer::ReportVideoRender(int64_t video_pts_ns, int64_t /*render_wall_ns*/) noexcept {
    m_last_video_pts.store(video_pts_ns, std::memory_order_relaxed);
    int64_t ats = m_last_audio_pts.load(std::memory_order_relaxed);
    if (ats != 0) {
        double off = static_cast<double>(video_pts_ns - ats) / 1'000'000.0;
        m_offset_ms.store(off, std::memory_order_relaxed);
        duwn::GlobalMetrics().av_offset_ms.store(off, std::memory_order_relaxed);
    }
}

void AvSynchronizer::ReportAudioPlay(int64_t audio_pts_ns, int64_t /*play_wall_ns*/) noexcept {
    m_last_audio_pts.store(audio_pts_ns, std::memory_order_relaxed);
}

double AvSynchronizer::OffsetMs() const noexcept {
    return m_offset_ms.load(std::memory_order_relaxed);
}

} // namespace duwn::sync
