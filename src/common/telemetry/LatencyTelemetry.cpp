#include "LatencyTelemetry.h"
#include "clock/MonotonicClock.h"
#include <algorithm>

namespace duwn::telemetry {

LatencyTelemetry& LatencyTelemetry::Get() noexcept {
    static LatencyTelemetry s_instance;
    return s_instance;
}

static inline double SafeDeltaMs(int64_t start, int64_t end) noexcept {
    if (start <= 0 || end <= 0 || end < start) {
        return 0.0;
    }
    return clock::MonotonicClock::QpcDeltaMs(start, end);
}

void LatencyTelemetry::RecordFrameStages(const LatencyStageTimestamps& stages) noexcept {
    // Determine effective T0: use t0_rtp_arrival if present, else fallback to t1_au_assembled
    const int64_t effective_t0 = stages.t0_rtp_arrival > 0 ? stages.t0_rtp_arrival : stages.t1_au_assembled;
    const int64_t t1 = stages.t1_au_assembled;
    const int64_t t2 = stages.t2_decoder_input;
    const int64_t t3 = stages.t3_decoder_output;
    const int64_t t4 = stages.t4_queue_inserted;
    const int64_t t5 = stages.t5_frame_selected;
    const int64_t t6 = stages.t6_vp_completed;
    const int64_t t7 = stages.t7_present_invoked;

    const double d_01 = (stages.t0_rtp_arrival > 0 && t1 >= stages.t0_rtp_arrival)
        ? SafeDeltaMs(stages.t0_rtp_arrival, t1) : 0.0;
    const double d_12 = SafeDeltaMs(t1, t2);
    const double d_23 = SafeDeltaMs(t2, t3);
    const double d_34 = SafeDeltaMs(t3, t4);
    const double d_45 = SafeDeltaMs(t4, t5);
    const double d_56 = SafeDeltaMs(t5, t6);
    const double d_67 = SafeDeltaMs(t6, t7);
    const double d_total = SafeDeltaMs(effective_t0, t7);

    std::lock_guard lock(m_mutex);
    constexpr double kAlpha = 0.10; // Smoothing factor for 10-frame rolling average

    if (m_frames_measured == 0) {
        m_t0_to_t1_ms = d_01;
        m_t1_to_t2_ms = d_12;
        m_t2_to_t3_ms = d_23;
        m_t3_to_t4_ms = d_34;
        m_t4_to_t5_ms = d_45;
        m_t5_to_t6_ms = d_56;
        m_t6_to_t7_ms = d_67;
        m_total_video_ms = d_total;
    } else {
        if (d_01 > 0.0) m_t0_to_t1_ms = m_t0_to_t1_ms * (1.0 - kAlpha) + d_01 * kAlpha;
        if (d_12 > 0.0) m_t1_to_t2_ms = m_t1_to_t2_ms * (1.0 - kAlpha) + d_12 * kAlpha;
        if (d_23 > 0.0) m_t2_to_t3_ms = m_t2_to_t3_ms * (1.0 - kAlpha) + d_23 * kAlpha;
        if (d_34 > 0.0) m_t3_to_t4_ms = m_t3_to_t4_ms * (1.0 - kAlpha) + d_34 * kAlpha;
        if (d_45 > 0.0) m_t4_to_t5_ms = m_t4_to_t5_ms * (1.0 - kAlpha) + d_45 * kAlpha;
        if (d_56 > 0.0) m_t5_to_t6_ms = m_t5_to_t6_ms * (1.0 - kAlpha) + d_56 * kAlpha;
        if (d_67 > 0.0) m_t6_to_t7_ms = m_t6_to_t7_ms * (1.0 - kAlpha) + d_67 * kAlpha;
        if (d_total > 0.0) m_total_video_ms = m_total_video_ms * (1.0 - kAlpha) + d_total * kAlpha;
    }
    m_frames_measured++;
}

void LatencyTelemetry::UpdateAudioBuffering(double ring_buffer_ms, double wasapi_padding_ms) noexcept {
    std::lock_guard lock(m_mutex);
    m_audio_ring_buffer_ms = std::max(0.0, ring_buffer_ms);
    m_audio_wasapi_padding_ms = std::max(0.0, wasapi_padding_ms);
}

LatencyMetricsSnapshot LatencyTelemetry::Snapshot() const noexcept {
    std::lock_guard lock(m_mutex);
    LatencyMetricsSnapshot s{};
    s.t0_to_t1_ms = m_t0_to_t1_ms;
    s.t1_to_t2_ms = m_t1_to_t2_ms;
    s.t2_to_t3_ms = m_t2_to_t3_ms;
    s.t3_to_t4_ms = m_t3_to_t4_ms;
    s.t4_to_t5_ms = m_t4_to_t5_ms;
    s.t5_to_t6_ms = m_t5_to_t6_ms;
    s.t6_to_t7_ms = m_t6_to_t7_ms;
    s.total_video_pipeline_ms = m_total_video_ms;

    s.audio_ring_buffer_ms = m_audio_ring_buffer_ms;
    s.audio_wasapi_padding_ms = m_audio_wasapi_padding_ms;
    s.audio_buffered_ms = m_audio_ring_buffer_ms + m_audio_wasapi_padding_ms;

    s.frames_measured = m_frames_measured;
    return s;
}

void LatencyTelemetry::Reset() noexcept {
    std::lock_guard lock(m_mutex);
    m_t0_to_t1_ms = 0.0;
    m_t1_to_t2_ms = 0.0;
    m_t2_to_t3_ms = 0.0;
    m_t3_to_t4_ms = 0.0;
    m_t4_to_t5_ms = 0.0;
    m_t5_to_t6_ms = 0.0;
    m_t6_to_t7_ms = 0.0;
    m_total_video_ms = 0.0;
    m_audio_ring_buffer_ms = 0.0;
    m_audio_wasapi_padding_ms = 0.0;
    m_frames_measured = 0;
    m_output_select_age.clear();
    m_output_present_age.clear();
    m_preview_decode_to_select.clear();
    m_preview_select_to_present.clear();
    m_preview_present_age.clear();
    m_preview_skips_recent = 0;
}

static FrameAgePercentiles CalcPercentiles(std::vector<double>& v) noexcept {
    if (v.empty()) return {};
    std::sort(v.begin(), v.end());
    FrameAgePercentiles p;
    p.p50 = v[v.size() * 50 / 100];
    size_t i95 = v.size() * 95 / 100;
    if (i95 >= v.size()) i95 = v.size() - 1;
    p.p95 = v[i95];
    size_t i99 = v.size() * 99 / 100;
    if (i99 >= v.size()) i99 = v.size() - 1;
    p.p99 = v[i99];
    p.max_val = v.back();
    return p;
}

void LatencyTelemetry::RecordOutputFrameAge(int64_t decoder_output_qpc, int64_t output_select_qpc, int64_t output_present_qpc) noexcept {
    if (decoder_output_qpc <= 0 || output_select_qpc <= 0) return;
    const double age_select = SafeDeltaMs(decoder_output_qpc, output_select_qpc);
    const double age_present = SafeDeltaMs(decoder_output_qpc, output_present_qpc);

    std::lock_guard lock(m_mutex);
    m_output_select_age.push_back(age_select);
    m_output_present_age.push_back(age_present);
}

void LatencyTelemetry::RecordPreviewFrameAge(int64_t decoder_output_qpc, int64_t preview_select_qpc, int64_t preview_vp_end_qpc, int64_t preview_present_qpc, bool skipped) noexcept {
    (void)preview_vp_end_qpc;
    if (decoder_output_qpc <= 0 || preview_select_qpc <= 0) return;
    const double dec_to_select = SafeDeltaMs(decoder_output_qpc, preview_select_qpc);
    const double sel_to_pres = SafeDeltaMs(preview_select_qpc, preview_present_qpc);
    const double age_present = SafeDeltaMs(decoder_output_qpc, preview_present_qpc);

    std::lock_guard lock(m_mutex);
    m_preview_decode_to_select.push_back(dec_to_select);
    m_preview_select_to_present.push_back(sel_to_pres);
    m_preview_present_age.push_back(age_present);
    if (skipped) {
        m_preview_skips_recent++;
    }
}

void LatencyTelemetry::GetFrameAgeStats(OutputFrameAgeStats& out_stats, PreviewFrameAgeStats& prev_stats) noexcept {
    std::vector<double> out_sel, out_pres;
    std::vector<double> prev_dec_sel, prev_sel_pres, prev_pres;
    uint64_t skips = 0;

    {
        std::lock_guard lock(m_mutex);
        out_sel.swap(m_output_select_age);
        out_pres.swap(m_output_present_age);
        prev_dec_sel.swap(m_preview_decode_to_select);
        prev_sel_pres.swap(m_preview_select_to_present);
        prev_pres.swap(m_preview_present_age);
        skips = m_preview_skips_recent;
        m_preview_skips_recent = 0;
    }

    out_stats.sample_count = out_sel.size();
    out_stats.age_at_select = CalcPercentiles(out_sel);
    out_stats.age_at_present = CalcPercentiles(out_pres);

    prev_stats.sample_count = prev_dec_sel.size();
    prev_stats.skips = skips;
    prev_stats.decode_to_select = CalcPercentiles(prev_dec_sel);
    prev_stats.age_at_select = prev_stats.decode_to_select;
    prev_stats.select_to_present = CalcPercentiles(prev_sel_pres);
    prev_stats.age_at_present = CalcPercentiles(prev_pres);
}

} // namespace duwn::telemetry
