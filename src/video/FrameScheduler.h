#pragma once
// FrameScheduler — paces decoded frames at the correct wall-clock time.
// Runs on the Video Render Thread.
// Inputs:  VideoFrames from the decoder (push)
// Outputs: fires FramePresentCallback at the right moment (QPC-based)
//
// Does NOT use Sleep(16). Uses a waitable timer + DXGI present timing.
// Correctly handles source FPS != 60 without faking frames.

#include "VideoFrame.h"
#include "common/clock/MonotonicClock.h"
#include "common/streaming/StreamingPolicy.h"
#include <functional>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>

namespace duwn::video {

// Called on the render thread when a frame should be presented. Must NOT block.
using FramePresentCallback = std::function<void(VideoFrame&)>;
using DxgiWaitableProvider = std::function<void*()>; // Returns Win32 HANDLE

enum class SchedulerMode {
    GameLowLatency,      // Bounded decoded queue + DXGI frame latency waitable clock
    PresentationClock    // Experimental: synthetic presentation clock with de-jitter queue
};

struct SchedulerConfig {
    SchedulerMode mode{SchedulerMode::GameLowLatency};
    StreamingPolicy streaming_policy{};

    // Expected inter-frame duration in nanoseconds. Derived from source FPS.
    // Default 16.67ms (~60fps for gaming).
    int64_t frame_duration_ns{16'666'667LL};

    // Maximum queue depth before dropping oldest frames (PresentationClock mode only).
    int max_queue_depth{4};

    // Late-frame threshold: cadence-aware (~35-40ms, allowing ~1 frame of OS scheduling jitter).
    int64_t late_threshold_ns{38'000'000LL};

    bool drop_late_frames{false};
};

class FrameScheduler {
public:
    explicit FrameScheduler(SchedulerConfig cfg, FramePresentCallback on_present) noexcept;
    ~FrameScheduler();

    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    void Start() noexcept;
    void Stop() noexcept;

    // Push a decoded frame. Called from the decode thread.
    void PushFrame(VideoFrame frame) noexcept;

    void Flush() noexcept;
    void UpdateConfig(const SchedulerConfig& cfg) noexcept;

    // Thread-safe, live receiver delivery change; preserves source quality.
    void SetStreamingPolicy(StreamingPolicy policy) noexcept {
        m_streaming_policy.store(policy, std::memory_order_release);
    }

    // Set provider for DXGI frame-latency waitable object (Phase 6)
    void SetDxgiWaitableProvider(DxgiWaitableProvider provider) noexcept {
        m_dxgi_waitable_provider = std::move(provider);
    }

    // Direct accessors for diagnostics and unit testing
    int64_t TickIntervalNs() const noexcept { return m_tick_interval_ns.load(std::memory_order_relaxed); }
    bool IsClockAnchored() const noexcept { return m_clock_anchored.load(std::memory_order_relaxed); }
    size_t DeJitterQueueSize() noexcept;
    size_t DecodedQueueSize() noexcept;
    bool HasMailboxFrame() noexcept;
    bool PopDecodedFrameForTest(VideoFrame& out_frame) noexcept;

    // GameLowLatency mode: select FIFO or newest using the live delivery policy.
    // Balanced uses 1.25x source cadence; Custom uses a receiver queue age.
    // Fastest supersedes pending images; every mode displays a lone frame.
    bool PopLatestValidFrame(VideoFrame& out_frame, uint64_t& out_superseded_drops) noexcept;

    // Helper to obtain estimated source cadence in milliseconds
    double GetEstimatedCadenceMs() const noexcept;

    // Pop the newest eligible frame with PTS <= target_pts_ns + tolerance_ns (PresentationClock mode / test).
    // Superseded older frames are popped and returned in out_superseded.
    bool PopEligibleFrame(int64_t target_pts_ns, int64_t tolerance_ns,
                          VideoFrame& out_frame, uint64_t& out_superseded) noexcept;

private:
    void SchedulerLoop(std::stop_token stop) noexcept;
    void SchedulerLoopGameLowLatency(std::stop_token stop) noexcept;
    void SchedulerLoopPresentationClock(std::stop_token stop) noexcept;
    void UpdateCadenceEstimate(int64_t pts_ns) noexcept;

    SchedulerConfig         m_cfg;
    std::atomic<StreamingPolicy> m_streaming_policy{};
    FramePresentCallback    m_on_present;
    DxgiWaitableProvider    m_dxgi_waitable_provider;

    // GameLowLatency mode: 1-3 decoded frames, independently configurable.
    std::mutex                m_decoded_queue_mutex;
    std::deque<VideoFrame>    m_decoded_queue;
    void*                     m_frame_available_event{nullptr}; // Win32 auto-reset event
    double                    m_observed_display_interval_ms{16.6667}; // Measured DXGI interval (~16.67ms)
    int64_t                   m_last_dxgi_ready_qpc{0};
    std::vector<double>       m_dxgi_ready_samples;

    // PresentationClock mode: de-jitter queue
    std::mutex              m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::deque<VideoFrame>  m_queue;

    std::atomic_bool        m_running{false};
    std::jthread            m_thread;

    // Active format generation to detect transitions and reject stale frames
    uint64_t                m_active_generation{0};
    uint64_t                m_last_presented_sequence{0};

    // Source Cadence Classifier with Hysteresis
    enum class CadenceClass { Unknown, Class30, Class60 };
    CadenceClass            m_current_cadence_class{CadenceClass::Class30};
    CadenceClass            m_candidate_cadence_class{CadenceClass::Unknown};
    int                     m_consecutive_candidate_evals{0};
    uint32_t                m_cadence_eval_counter{0};

    // Presentation clock state
    std::atomic<int64_t>    m_tick_interval_ns{33'366'700LL}; // ~29.97 fps default
    std::atomic_bool        m_clock_anchored{false};

    // PTS of previously presented frame (for drift monitoring)
    int64_t                 m_prev_pts_ns{0};
    int64_t                 m_render_start_ns{0}; // MonotonicClock at first frame of generation
    int64_t                 m_pts_origin_ns{0};   // PTS of first frame of generation

    // Cadence estimation history (filtered non-outlier deltas)
    std::vector<int64_t>    m_cadence_delta_history;
    int64_t                 m_estimated_cadence_ns{33'366'700LL};
    int64_t                 m_last_source_pts_ns{0};

    // Windowed diagnostics samples (per 1 second)
    std::vector<double>     m_pts_delta_samples;
    std::vector<double>     m_wake_error_samples;
    std::vector<double>     m_lateness_samples;
    std::vector<double>     m_decode_to_present_samples;
    std::vector<double>     m_queue_age_samples;
    std::vector<double>     m_present_call_samples;
    std::vector<double>     m_present_interval_samples;
    int64_t                 m_last_metrics_sample_ns{0};
    int64_t                 m_last_present_time_ns{0};
};

} // namespace duwn::video
