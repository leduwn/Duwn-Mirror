#pragma once
// LatencyTelemetry.h — Fine-grained QPC latency instrumentation across 8 pipeline stages (T0–T7).
//
// Tracks monotonic QPC timestamps:
//   T0: RTP packet arrival
//   T1: Complete H264 AU assembled
//   T2: Decoder input submitted
//   T3: Decoder output sample available
//   T4: Decoded frame inserted into queue
//   T5: Frame selected for presentation
//   T6: VideoProcessor blit completed
//   T7: Present() invoked
//
// Computes stage durations, decoded queue age, and audio buffering.

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

namespace duwn::telemetry {

struct FrameAgePercentiles {
    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
    double max_val{0.0};
};

struct OutputFrameAgeStats {
    FrameAgePercentiles age_at_select;
    FrameAgePercentiles age_at_present;
    uint64_t sample_count{0};
};

struct PreviewFrameAgeStats {
    FrameAgePercentiles decode_to_select;
    FrameAgePercentiles age_at_select;
    FrameAgePercentiles select_to_present;
    FrameAgePercentiles age_at_present;
    uint64_t sample_count{0};
    uint64_t skips{0};
    uint64_t errors{0};
};

struct LatencyStageTimestamps {
    int64_t t0_rtp_arrival{0};        // T0: RTP packet arrival (QPC ticks)
    int64_t t1_au_assembled{0};       // T1: Complete H264 AU assembled (QPC ticks)
    int64_t t2_decoder_input{0};      // T2: Decoder input submitted (QPC ticks)
    int64_t t3_decoder_output{0};     // T3: Decoder output sample available (QPC ticks)
    int64_t t4_queue_inserted{0};     // T4: Decoded frame inserted into queue (QPC ticks)
    int64_t t5_frame_selected{0};     // T5: Frame selected for presentation (QPC ticks)
    int64_t t6_vp_completed{0};       // T6: VideoProcessor blit completed (QPC ticks)
    int64_t t7_present_invoked{0};    // T7: Present() invoked (QPC ticks)
};

struct LatencyMetricsSnapshot {
    // Stage durations (ms)
    double t0_to_t1_ms{0.0};          // AU assembly duration (T1 - T0)
    double t1_to_t2_ms{0.0};          // Ingest to decode input delay (T2 - T1)
    double t2_to_t3_ms{0.0};          // Decoder decode duration (T3 - T2)
    double t3_to_t4_ms{0.0};          // Decoder output to queue push (T4 - T3)
    double t4_to_t5_ms{0.0};          // Decoded queue dwell age (T5 - T4)
    double t5_to_t6_ms{0.0};          // VideoProcessor blit duration (T6 - T5)
    double t6_to_t7_ms{0.0};          // Present duration (T7 - T6)
    double total_video_pipeline_ms{0.0}; // Total pipeline latency: T7 - T0 (or T7 - T1)

    // Audio metrics (ms)
    double audio_ring_buffer_ms{0.0};
    double audio_wasapi_padding_ms{0.0};
    double audio_buffered_ms{0.0};    // Ring buffer + WASAPI padding

    // Frame counters
    uint64_t frames_measured{0};
};

class LatencyTelemetry {
public:
    static LatencyTelemetry& Get() noexcept;

    // Record stage timestamps for a completed frame
    void RecordFrameStages(const LatencyStageTimestamps& stages) noexcept;

    // Record audio buffering state
    void UpdateAudioBuffering(double ring_buffer_ms, double wasapi_padding_ms) noexcept;

    // Record frame age for Output
    void RecordOutputFrameAge(int64_t decoder_output_qpc, int64_t output_select_qpc, int64_t output_present_qpc) noexcept;

    // Record frame age for Preview
    void RecordPreviewSuccess(int64_t decoder_output_qpc, int64_t preview_select_qpc, int64_t preview_present_qpc) noexcept;
    void RecordPreviewSkip() noexcept;
    void RecordPreviewError() noexcept;
    void RecordPreviewFrameAge(int64_t decoder_output_qpc, int64_t preview_select_qpc, int64_t preview_vp_end_qpc, int64_t preview_present_qpc, bool skipped) noexcept;

    // Retrieve and reset 1-second frame age stats
    void GetFrameAgeStats(OutputFrameAgeStats& out_stats, PreviewFrameAgeStats& prev_stats) noexcept;

    // Get a current snapshot of latency metrics
    LatencyMetricsSnapshot Snapshot() const noexcept;

    // Reset session metrics
    void Reset() noexcept;

private:
    LatencyTelemetry() = default;

    mutable std::mutex m_mutex;

    // Rolling averages (ms)
    double m_t0_to_t1_ms{0.0};
    double m_t1_to_t2_ms{0.0};
    double m_t2_to_t3_ms{0.0};
    double m_t3_to_t4_ms{0.0};
    double m_t4_to_t5_ms{0.0};
    double m_t5_to_t6_ms{0.0};
    double m_t6_to_t7_ms{0.0};
    double m_total_video_ms{0.0};

    // Audio buffering (ms)
    double m_audio_ring_buffer_ms{0.0};
    double m_audio_wasapi_padding_ms{0.0};

    uint64_t m_frames_measured{0};

    // Frame Age samples for Output
    std::vector<double> m_output_select_age;
    std::vector<double> m_output_present_age;

    // Frame Age samples for Preview
    std::vector<double> m_preview_decode_to_select;
    std::vector<double> m_preview_select_to_present;
    std::vector<double> m_preview_present_age;
    uint64_t m_preview_skips_recent{0};
    uint64_t m_preview_errors_recent{0};
};

} // namespace duwn::telemetry
