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
#include <array>

namespace duwn::telemetry {

enum class FrameTimingMetric : size_t {
    RtpIngest, AuComplete, DecoderOutput, Enqueue, Dequeue, PresentBegin, SocketRead,
    Assembly, AuToOutput, OutputToQueue, QueueDwell, SelectToReturn,
    ReceiveToReturn, PresentCall, ReceiverCallback, DecoderMutexWait,
    DecoderMutexHold, ProcessInput, ProcessOutput, Count
};

inline constexpr std::array<const char*, static_cast<size_t>(FrameTimingMetric::Count)> kFrameTimingNames{
    "rtp_arrival_interval", "au_interval", "decoder_interval", "enqueue_interval",
    "dequeue_interval", "present_begin_interval", "socket_read_interval",
    "rtp_to_au", "au_to_output",
    "output_to_enqueue", "queue_dwell", "dequeue_to_present_return",
    "rtp_to_present_return", "present_call", "receiver_callback",
    "decoder_mutex_wait", "decoder_mutex_hold", "process_input", "process_output"
};

struct FrameTimingStats {
    uint64_t count{0};
    size_t retained{0};
    double mean{0}, p50{0}, p95{0}, p99{0}, max{0};
    uint64_t over1{0}, gaps25{0}, gaps33{0}, gaps50{0};
};

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

    // Opt-in diagnostic windows; no allocation/logging/sorting on media threads.
    void SetFrameTimingEnabled(bool enabled) noexcept;
    bool FrameTimingEnabled() const noexcept { return m_frame_timing_enabled.load(std::memory_order_relaxed); }
    void RecordFrameInterval(FrameTimingMetric metric, int64_t qpc) noexcept;
    void RecordFrameDuration(FrameTimingMetric metric, int64_t start, int64_t end) noexcept;
    std::array<FrameTimingStats, kFrameTimingNames.size()> CollectFrameTiming() noexcept;

private:
    LatencyTelemetry() = default;

    struct TimingSamples {
        std::array<double, 256> values{};
        uint64_t count{0};
        double sum{0}, max{0};
        uint64_t over1{0}, gaps25{0}, gaps33{0}, gaps50{0};
        void Add(double ms) noexcept;
    };
    std::atomic<bool> m_frame_timing_enabled{false};
    std::mutex m_timing_mutex;
    int64_t m_timing_reset_qpc{0};
    std::array<TimingSamples, kFrameTimingNames.size()> m_timing_samples{};
    std::array<int64_t, 7> m_previous_frame_qpc{};

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
