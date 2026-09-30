#include "FrameScheduler.h"

#include "common/logging/Logger.h"

#include "common/metrics/Metrics.h"

#include <windows.h>

#include <avrt.h>

#include <algorithm>

#include <cmath>



#pragma comment(lib, "avrt.lib")



namespace duwn::video {



FrameScheduler::FrameScheduler(SchedulerConfig cfg, FramePresentCallback on_present) noexcept

    : m_cfg(cfg)

    , m_on_present(std::move(on_present)) {

    m_frame_available_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

}



FrameScheduler::~FrameScheduler() {

    Stop();

    if (m_frame_available_event) {

        ::CloseHandle(static_cast<HANDLE>(m_frame_available_event));

        m_frame_available_event = nullptr;

    }

}



void FrameScheduler::Start() noexcept {

    m_running.store(true, std::memory_order_release);

    m_thread = std::jthread([this](std::stop_token st) {

        SchedulerLoop(std::move(st));

    });

}



void FrameScheduler::Stop() noexcept {

    m_running.store(false, std::memory_order_release);

    if (m_frame_available_event) {

        ::SetEvent(static_cast<HANDLE>(m_frame_available_event));

    }

    m_queue_cv.notify_all();

    m_thread.request_stop();

    if (m_thread.joinable()) m_thread.join();

}



size_t FrameScheduler::DeJitterQueueSize() noexcept {

    std::lock_guard lock{m_queue_mutex};

    return m_queue.size();

}



size_t FrameScheduler::DecodedQueueSize() noexcept {

    std::lock_guard lock{m_decoded_queue_mutex};

    return m_decoded_queue.size();

}



bool FrameScheduler::HasMailboxFrame() noexcept {

    return DecodedQueueSize() > 0;

}



bool FrameScheduler::PopDecodedFrameForTest(VideoFrame& out_frame) noexcept {

    std::lock_guard lock{m_decoded_queue_mutex};

    if (m_decoded_queue.empty()) return false;

    out_frame = std::move(m_decoded_queue.front());

    m_decoded_queue.pop_front();

    return true;

}



double FrameScheduler::GetEstimatedCadenceMs() const noexcept {

    double fps = GlobalMetrics().source_nominal_fps.load(std::memory_order_relaxed);

    if (fps > 0.0) {

        return 1000.0 / fps;

    }

    int64_t tick_ns = m_tick_interval_ns.load(std::memory_order_relaxed);

    if (tick_ns > 0) {

        return static_cast<double>(tick_ns) / 1'000'000.0;

    }

    if (m_cfg.frame_duration_ns > 0) {

        return static_cast<double>(m_cfg.frame_duration_ns) / 1'000'000.0;

    }

    return 16.6667;

}



bool FrameScheduler::PopLatestValidFrame(VideoFrame& out_frame, uint64_t& out_superseded_drops) noexcept {

    std::lock_guard lock{m_decoded_queue_mutex};

    out_superseded_drops = 0;



    // 1. Purge stale generation frames from the front

    while (!m_decoded_queue.empty() &&

           m_active_generation != 0 &&

           m_decoded_queue.front().format_generation < m_active_generation) {

        m_decoded_queue.pop_front();

        GlobalMetrics().video_stale_generation_drops.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

    }



    if (m_decoded_queue.empty()) {

        return false;

    }



    // 2. Cadence-aware stale frame policy:

    // If depth == 1: present frame if generation valid

    if (m_decoded_queue.size() == 1) {

        out_frame = std::move(m_decoded_queue.front());

        m_decoded_queue.pop_front();

        out_superseded_drops = 0;

        GlobalMetrics().video_queue_depth.store(0, std::memory_order_relaxed);

        return true;

    }



    // 3. If depth > 1: evaluate cadence-aware staleness threshold (1.0-1.25x cadence)

    using clock = duwn::clock::MonotonicClock;

    const double cadence_ms = GetEstimatedCadenceMs();

    const double freshness_threshold_ms = 1.25 * cadence_ms;

    const int64_t now_qpc = clock::NowQpcTicks();



    const auto& oldest = m_decoded_queue.front();

    double oldest_age_ms = 0.0;

    if (oldest.queue_push_qpc > 0) {

        oldest_age_ms = clock::QpcDeltaMs(oldest.queue_push_qpc, now_qpc);

    } else if (oldest.arrival_ns > 0) {

        oldest_age_ms = static_cast<double>(clock::Now().time_since_epoch().count() - oldest.arrival_ns) / 1'000'000.0;

    }



    if (oldest_age_ms <= freshness_threshold_ms) {

        // Oldest valid frame is still within freshness budget: present normally without drop

        out_frame = std::move(m_decoded_queue.front());

        m_decoded_queue.pop_front();

        out_superseded_drops = 0;

    } else {

        // Oldest valid frame exceeds freshness budget: discard stale history, return newest valid frame

        out_superseded_drops = m_decoded_queue.size() - 1;

        GlobalMetrics().video_stale_age_drops.fetch_add(out_superseded_drops, std::memory_order_relaxed);

        GlobalMetrics().video_latency_catchup_drops.fetch_add(out_superseded_drops, std::memory_order_relaxed);

        GlobalMetrics().video_dropped_frames.fetch_add(out_superseded_drops, std::memory_order_relaxed);

        GlobalMetrics().duwn_dropped_frames.fetch_add(out_superseded_drops, std::memory_order_relaxed);

        GlobalMetrics().session_drops.fetch_add(out_superseded_drops, std::memory_order_relaxed);



        out_frame = std::move(m_decoded_queue.back());

        m_decoded_queue.clear();

    }



    const int32_t depth = static_cast<int32_t>(m_decoded_queue.size());

    GlobalMetrics().video_queue_depth.store(depth, std::memory_order_relaxed);

    return true;

}



bool FrameScheduler::PopEligibleFrame(int64_t target_pts_ns, int64_t tolerance_ns,

                                      VideoFrame& out_frame, uint64_t& out_superseded) noexcept {

    std::lock_guard lock{m_queue_mutex};

    out_superseded = 0;

    if (m_queue.empty()) return false;



    // Find newest frame with PTS <= target_pts_ns + tolerance_ns

    size_t best_idx = size_t(-1);

    for (size_t i = 0; i < m_queue.size(); ++i) {

        if (m_queue[i].pts_ns <= target_pts_ns + tolerance_ns) {

            best_idx = i;

        } else {

            break; // m_queue is sorted by PTS

        }

    }



    if (best_idx == size_t(-1)) {

        return false;

    }



    // All frames before best_idx are superseded by burst/newer frames

    out_superseded = best_idx;

    for (size_t i = 0; i < best_idx; ++i) {

        m_queue.pop_front();

    }



    out_frame = std::move(m_queue.front());

    m_queue.pop_front();



    GlobalMetrics().video_queue_depth.store(

        static_cast<int32_t>(m_queue.size()), std::memory_order_relaxed);



    return true;

}



void FrameScheduler::PushFrame(VideoFrame frame) noexcept {

    using clock = duwn::clock::MonotonicClock;



    // Format generation transition handling

    if (frame.format_generation > m_active_generation) {

        {

            std::lock_guard lock{m_decoded_queue_mutex};

            uint64_t purged = 0;

            while (!m_decoded_queue.empty() && m_decoded_queue.front().format_generation < frame.format_generation) {

                m_decoded_queue.pop_front();

                ++purged;

            }

            if (purged > 0) {

                GlobalMetrics().video_format_transition_drops.fetch_add(purged, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(purged, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(purged, std::memory_order_relaxed);

            }

        }

        {

            std::lock_guard lock{m_queue_mutex};

            uint64_t purged = 0;

            while (!m_queue.empty() && m_queue.front().format_generation < frame.format_generation) {

                m_queue.pop_front();

                ++purged;

            }

            if (purged > 0) {

                GlobalMetrics().video_format_transition_drops.fetch_add(purged, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(purged, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(purged, std::memory_order_relaxed);

            }

        }

        m_active_generation       = frame.format_generation;

        m_last_presented_sequence = 0;

        m_clock_anchored.store(false, std::memory_order_relaxed);

        GlobalMetrics().video_format_generation.store(m_active_generation, std::memory_order_relaxed);

    } else if (frame.format_generation < m_active_generation) {

        GlobalMetrics().video_stale_generation_drops.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

        GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

        return;

    }



    // Update cadence estimation with robust outlier rejection and hysteresis

    UpdateCadenceEstimate(frame.pts_ns);



    if (m_cfg.mode == SchedulerMode::GameLowLatency) {

        // Elastic 3-frame DecodedFrameQueue (Phase 5)

        std::lock_guard lock{m_decoded_queue_mutex};



        if (frame.queue_push_qpc == 0) {

            frame.queue_push_qpc = clock::NowQpcTicks();

        }



        // Check if queue is at capacity (3)

        if (m_decoded_queue.size() >= kDecodedQueueCapacity) {

            // Evaluate staleness: staleness threshold ~1.5 - 2x observed display refresh interval

            const double staleness_threshold_ms = m_observed_display_interval_ms * 1.75;

            const int64_t now_qpc = clock::NowQpcTicks();

            const double oldest_age_ms = clock::QpcDeltaMs(m_decoded_queue.front().queue_push_qpc, now_qpc);



            if (oldest_age_ms > staleness_threshold_ms) {

                // Oldest frame IS stale: drop oldest frame (latency catchup drop)

                m_decoded_queue.pop_front();

                GlobalMetrics().video_stale_age_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_latency_catchup_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

            } else {

                // Oldest frame is NOT stale (burst of fresh frames while waiting for display interval).

                // Drop the oldest frame to absorb fresh frame without falling behind.

                m_decoded_queue.pop_front();

                GlobalMetrics().video_queue_overflow_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_queue_full_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);



                static int64_t s_last_overflow_log_qpc = 0;

                if (clock::QpcDeltaMs(s_last_overflow_log_qpc, now_qpc) > 1000.0) {

                    s_last_overflow_log_qpc = now_qpc;

                    DUWN_LOG_WARN("FrameScheduler", "DecodedFrameQueue full with fresh frames — decoder ahead of display");

                }

            }

        }



        m_decoded_queue.push_back(std::move(frame));

        GlobalMetrics().video_queue_depth.store(

            static_cast<int32_t>(m_decoded_queue.size()), std::memory_order_relaxed);



        if (m_frame_available_event) {

            ::SetEvent(static_cast<HANDLE>(m_frame_available_event));

        }

    } else {

        // PresentationClock mode: de-jitter queue

        std::lock_guard lock{m_queue_mutex};



        // Check if frame is too late relative to current presentation position

        if (m_prev_pts_ns > 0 && frame.pts_ns < m_prev_pts_ns) {

            GlobalMetrics().video_presentation_late_drops.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().video_late_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

            return;

        }



        // Max queue depth enforcement — drop oldest if buffer overflows

        if (static_cast<int>(m_queue.size()) >= m_cfg.max_queue_depth) {

            m_queue.pop_front();

            GlobalMetrics().video_queue_overflow_drops.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().video_queue_full_drops.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().session_q_full.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

        }



        m_queue.push_back(std::move(frame));

        GlobalMetrics().video_queue_depth.store(

            static_cast<int32_t>(m_queue.size()), std::memory_order_relaxed);

        m_queue_cv.notify_one();

    }

}



void FrameScheduler::UpdateCadenceEstimate(int64_t pts_ns) noexcept {

    if (m_last_source_pts_ns <= 0 || pts_ns <= m_last_source_pts_ns) {

        m_last_source_pts_ns = pts_ns;

        return;

    }



    int64_t delta_ns = pts_ns - m_last_source_pts_ns;

    m_last_source_pts_ns = pts_ns;



    // Outlier rejection: deltas < 8ms or > 75ms are rejected from cadence estimation

    if (delta_ns < 8'000'000LL || delta_ns > 75'000'000LL) {

        GlobalMetrics().source_outliers.fetch_add(1, std::memory_order_relaxed);

        return;

    }



    double delta_ms = static_cast<double>(delta_ns) / 1'000'000.0;

    m_pts_delta_samples.push_back(delta_ms);



    m_cadence_delta_history.push_back(delta_ns);

    if (m_cadence_delta_history.size() > 60) {

        m_cadence_delta_history.erase(m_cadence_delta_history.begin());

    }



    if (m_cadence_delta_history.size() >= 10) {

        std::vector<int64_t> sorted = m_cadence_delta_history;

        std::sort(sorted.begin(), sorted.end());

        int64_t median_delta_ns = sorted[sorted.size() / 2];

        double median_delta_ms = static_cast<double>(median_delta_ns) / 1'000'000.0;



        std::vector<double> jitters;

        jitters.reserve(m_cadence_delta_history.size());

        for (int64_t d : m_cadence_delta_history) {

            double j = std::abs(static_cast<double>(d - median_delta_ns)) / 1'000'000.0;

            jitters.push_back(j);

        }

        std::sort(jitters.begin(), jitters.end());

        double jitter_p50 = jitters[jitters.size() / 2];

        size_t p95_idx = static_cast<size_t>(jitters.size() * 0.95);

        if (p95_idx >= jitters.size()) p95_idx = jitters.size() - 1;

        double jitter_p95 = jitters[p95_idx];



        GlobalMetrics().source_jitter_p50_ms.store(jitter_p50, std::memory_order_relaxed);

        GlobalMetrics().source_jitter_p95_ms.store(jitter_p95, std::memory_order_relaxed);



        // Hysteresis classification:

        // Requires >= 20 samples in history and evaluates every 15 samples across multiple consecutive windows

        m_cadence_eval_counter++;

        if (m_cadence_delta_history.size() >= 20 && (m_cadence_eval_counter % 15) == 0) {

            CadenceClass window_candidate = CadenceClass::Unknown;

            if (median_delta_ms >= 14.5 && median_delta_ms <= 19.5) {

                window_candidate = CadenceClass::Class60;

            } else if (median_delta_ms >= 28.5 && median_delta_ms <= 38.0) {

                window_candidate = CadenceClass::Class30;

            }



            if (window_candidate != CadenceClass::Unknown) {

                if (window_candidate == m_candidate_cadence_class) {

                    m_consecutive_candidate_evals++;

                } else {

                    m_candidate_cadence_class = window_candidate;

                    m_consecutive_candidate_evals = 1;

                }



                // Require 2 consecutive window evaluations to switch class (hysteresis)

                if (m_consecutive_candidate_evals >= 2) {

                    m_current_cadence_class = m_candidate_cadence_class;

                }

            }

        }



        if (m_current_cadence_class == CadenceClass::Class60) {

            GlobalMetrics().source_nominal_fps.store(60.0, std::memory_order_relaxed);

            m_tick_interval_ns.store(16'666'667LL, std::memory_order_relaxed);

        } else if (m_current_cadence_class == CadenceClass::Class30) {

            GlobalMetrics().source_nominal_fps.store(29.97, std::memory_order_relaxed);

            m_tick_interval_ns.store(33'366'700LL, std::memory_order_relaxed);

        } else {

            double classified_fps = (median_delta_ms > 0.0) ? (1000.0 / median_delta_ms) : 30.0;

            GlobalMetrics().source_nominal_fps.store(classified_fps, std::memory_order_relaxed);

            m_tick_interval_ns.store(median_delta_ns, std::memory_order_relaxed);

        }

    }

}



void FrameScheduler::Flush() noexcept {

    {

        std::lock_guard lock{m_decoded_queue_mutex};

        m_decoded_queue.clear();

    }

    {

        std::lock_guard lock{m_queue_mutex};

        m_queue.clear();

    }

    m_active_generation           = 0;

    m_last_presented_sequence     = 0;

    m_prev_pts_ns                 = 0;

    m_render_start_ns             = 0;

    m_pts_origin_ns               = 0;

    m_last_source_pts_ns          = 0;

    m_last_present_time_ns        = 0;

    m_last_dxgi_ready_qpc         = 0;

    m_dxgi_ready_samples.clear();

    m_clock_anchored.store(false, std::memory_order_relaxed);

    m_cadence_delta_history.clear();

    m_current_cadence_class       = CadenceClass::Class30;

    m_candidate_cadence_class     = CadenceClass::Unknown;

    m_consecutive_candidate_evals = 0;

    m_cadence_eval_counter        = 0;

    m_pts_delta_samples.clear();

    m_wake_error_samples.clear();

    m_lateness_samples.clear();

    m_decode_to_present_samples.clear();

    m_queue_age_samples.clear();

    m_present_call_samples.clear();

    m_present_interval_samples.clear();

    GlobalMetrics().session_q_full.store(0, std::memory_order_relaxed);

    GlobalMetrics().session_drops.store(0, std::memory_order_relaxed);

    GlobalMetrics().video_queue_depth.store(0, std::memory_order_relaxed);

}



void FrameScheduler::UpdateConfig(const SchedulerConfig& cfg) noexcept {

    std::lock_guard lock{m_queue_mutex};

    m_cfg = cfg;

}



static void CalcDiagnosticsStats(std::vector<double>& samples,

                                 std::atomic<double>& out_avg,

                                 std::atomic<double>* out_p50 = nullptr,

                                 std::atomic<double>* out_p95 = nullptr,

                                 std::atomic<double>* out_max = nullptr) {

    if (samples.empty()) return;

    std::sort(samples.begin(), samples.end());

    double sum = 0.0;

    for (double s : samples) sum += s;

    double avg = sum / static_cast<double>(samples.size());

    out_avg.store(avg, std::memory_order_relaxed);

    if (out_p50) {

        size_t p50_idx = samples.size() / 2;

        out_p50->store(samples[p50_idx], std::memory_order_relaxed);

    }

    if (out_p95) {

        size_t p95_idx = static_cast<size_t>(static_cast<double>(samples.size()) * 0.95);

        if (p95_idx >= samples.size()) p95_idx = samples.size() - 1;

        out_p95->store(samples[p95_idx], std::memory_order_relaxed);

    }

    if (out_max) {

        out_max->store(samples.back(), std::memory_order_relaxed);

    }

    samples.clear();

}



void FrameScheduler::SchedulerLoop(std::stop_token stop) noexcept {

    if (m_cfg.mode == SchedulerMode::GameLowLatency) {

        SchedulerLoopGameLowLatency(stop);

    } else {

        SchedulerLoopPresentationClock(stop);

    }

}



void FrameScheduler::SchedulerLoopGameLowLatency(std::stop_token stop) noexcept {

    using clock = duwn::clock::MonotonicClock;



    // Register render thread with MMCSS "Playback" (Phase 9)

    DWORD task_idx = 0;

    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Playback", &task_idx);

    if (!mmcss) {

        DUWN_LOG_WARN("FrameScheduler", "Failed to register render thread with MMCSS Playback");

    }

    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);



    HANDLE event_handle = static_cast<HANDLE>(m_frame_available_event);



    std::vector<double> queue_depth_samples;

    std::vector<double> queue_residence_samples;



    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {

        bool queue_empty = false;

        {

            std::lock_guard lock{m_decoded_queue_mutex};

            queue_empty = m_decoded_queue.empty();

        }



        HANDLE waitable = m_dxgi_waitable_provider ? static_cast<HANDLE>(m_dxgi_waitable_provider()) : nullptr;



        if (queue_empty) {

            // Wait for new frame to be pushed into queue (timeout 50ms)

            ::WaitForSingleObject(event_handle, 50);

            if (stop.stop_requested()) break;



            std::lock_guard lock{m_decoded_queue_mutex};

            if (m_decoded_queue.empty()) {

                GlobalMetrics().presentation_repeated_ticks.fetch_add(1, std::memory_order_relaxed);

                continue;

            }

        }



        // Wait on DXGI frame-latency waitable object as master presentation clock (Phase 6 & 8)

        if (waitable) {

            DWORD wr = ::WaitForSingleObject(waitable, 100);

            if (stop.stop_requested()) break;

            if (wr == WAIT_OBJECT_0) {

                GlobalMetrics().dxgi_ready_signals.fetch_add(1, std::memory_order_relaxed);

                int64_t now_qpc = clock::NowQpcTicks();

                if (m_last_dxgi_ready_qpc > 0) {

                    double delta_ms = clock::QpcDeltaMs(m_last_dxgi_ready_qpc, now_qpc);

                    m_dxgi_ready_samples.push_back(delta_ms);

                    if (delta_ms >= 5.0 && delta_ms <= 50.0) {

                        m_observed_display_interval_ms = 0.9 * m_observed_display_interval_ms + 0.1 * delta_ms;

                    }

                }

                m_last_dxgi_ready_qpc = now_qpc;

            }

        } else {

            ::WaitForSingleObject(event_handle, 16);

            if (stop.stop_requested()) break;

        }



        GlobalMetrics().display_opportunities.fetch_add(1, std::memory_order_relaxed);



        VideoFrame frame;

        uint64_t superseded_drops = 0;

        bool has_frame = PopLatestValidFrame(frame, superseded_drops);

        if (has_frame) {

            queue_depth_samples.push_back(static_cast<double>(GlobalMetrics().video_queue_depth.load(std::memory_order_relaxed)));

        }



        if (has_frame) {

            frame.queue_pop_qpc = clock::NowQpcTicks();

            if (frame.queue_push_qpc > 0) {

                double q_res_ms = clock::QpcDeltaMs(frame.queue_push_qpc, frame.queue_pop_qpc);

                queue_residence_samples.push_back(q_res_ms);

            }



            // Sequence order verification: never present older frame after newer already presented

            if (frame.sequence_number != 0 && m_last_presented_sequence != 0 &&

                frame.sequence_number <= m_last_presented_sequence) {

                GlobalMetrics().video_presentation_late_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

                continue;

            }



            // Generation verification

            if (frame.format_generation != m_active_generation && m_active_generation != 0) {

                GlobalMetrics().video_stale_generation_drops.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().duwn_dropped_frames.fetch_add(1, std::memory_order_relaxed);

                GlobalMetrics().session_drops.fetch_add(1, std::memory_order_relaxed);

                continue;

            }



            m_last_presented_sequence = frame.sequence_number;



            int64_t present_start_ns = clock::Now().time_since_epoch().count();



            if (frame.arrival_ns > 0) {

                double dec_to_pres_ms = static_cast<double>(present_start_ns - frame.arrival_ns) / 1'000'000.0;

                m_decode_to_present_samples.push_back(dec_to_pres_ms);

            }



            if (m_last_present_time_ns > 0) {

                double interval_ms = static_cast<double>(present_start_ns - m_last_present_time_ns) / 1'000'000.0;

                m_present_interval_samples.push_back(interval_ms);

                if (interval_ms > 25.0) {

                    GlobalMetrics().stutter_intervals_gt_25ms.fetch_add(1, std::memory_order_relaxed);

                }

                if (interval_ms > 33.33) {

                    GlobalMetrics().stutter_intervals_gt_33ms.fetch_add(1, std::memory_order_relaxed);

                }

                if (interval_ms > 50.0) {

                    GlobalMetrics().stutter_intervals_gt_50ms.fetch_add(1, std::memory_order_relaxed);

                }

            }

            m_last_present_time_ns = present_start_ns;



            if (m_on_present) {

                m_on_present(frame);

            }



            int64_t present_end_ns = clock::Now().time_since_epoch().count();

            double present_call_ms = static_cast<double>(present_end_ns - present_start_ns) / 1'000'000.0;

            m_present_call_samples.push_back(present_call_ms);



            GlobalMetrics().video_render_time_ms.store(present_call_ms, std::memory_order_relaxed);

            GlobalMetrics().video_rendered_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().presentation_unique_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().presentation_ticks.fetch_add(1, std::memory_order_relaxed);

            m_prev_pts_ns = frame.pts_ns;

        } else {

            GlobalMetrics().presentation_repeated_ticks.fetch_add(1, std::memory_order_relaxed);

        }



        // 1 Hz percentiles publication and accounting invariant check (Phase 13)

        int64_t stats_now = clock::Now().time_since_epoch().count();

        if (m_last_metrics_sample_ns == 0) {

            m_last_metrics_sample_ns = stats_now;

        } else if (stats_now - m_last_metrics_sample_ns >= 1'000'000'000LL) {

            CalcDiagnosticsStats(m_pts_delta_samples,

                                 GlobalMetrics().video_pts_delta_avg_ms,

                                 &GlobalMetrics().video_pts_delta_p50_ms,

                                 &GlobalMetrics().video_pts_delta_p95_ms);

            CalcDiagnosticsStats(m_decode_to_present_samples,

                                 GlobalMetrics().video_decode_to_present_avg_ms,

                                 nullptr,

                                 nullptr);

            CalcDiagnosticsStats(m_present_call_samples,

                                 GlobalMetrics().video_present_call_avg_ms,

                                 &GlobalMetrics().video_present_call_p50_ms,

                                 &GlobalMetrics().video_present_call_p95_ms);

            CalcDiagnosticsStats(m_present_interval_samples,

                                 GlobalMetrics().video_present_interval_avg_ms,

                                 nullptr,

                                 &GlobalMetrics().video_present_interval_p95_ms);

            CalcDiagnosticsStats(m_dxgi_ready_samples,

                                 GlobalMetrics().dxgi_ready_interval_avg_ms,

                                 &GlobalMetrics().dxgi_ready_interval_p50_ms,

                                 &GlobalMetrics().dxgi_ready_interval_p95_ms,

                                 &GlobalMetrics().dxgi_ready_interval_max_ms);

            CalcDiagnosticsStats(queue_residence_samples,

                                 GlobalMetrics().queue_residence_avg_ms,

                                 nullptr,

                                 &GlobalMetrics().queue_residence_p95_ms);



            // Queue depth percentiles and max

            if (!queue_depth_samples.empty()) {

                std::sort(queue_depth_samples.begin(), queue_depth_samples.end());

                double p50 = queue_depth_samples[queue_depth_samples.size() / 2];

                size_t p95_i = static_cast<size_t>(queue_depth_samples.size() * 0.95);

                if (p95_i >= queue_depth_samples.size()) p95_i = queue_depth_samples.size() - 1;

                double p95 = queue_depth_samples[p95_i];

                double max_depth = queue_depth_samples.back();

                GlobalMetrics().queue_depth_p50.store(p50, std::memory_order_relaxed);

                GlobalMetrics().queue_depth_p95.store(p95, std::memory_order_relaxed);

                GlobalMetrics().queue_depth_max.store(static_cast<int32_t>(max_depth), std::memory_order_relaxed);

                queue_depth_samples.clear();

            }



            // Invariant: decoded = presented + explicit_drops + queued

            int64_t decoded = static_cast<int64_t>(GlobalMetrics().video_decoded_frames.load(std::memory_order_relaxed));

            int64_t presented = static_cast<int64_t>(GlobalMetrics().video_rendered_frames.load(std::memory_order_relaxed));

            int64_t catchup_drops = static_cast<int64_t>(GlobalMetrics().video_latency_catchup_drops.load(std::memory_order_relaxed));

            int64_t overflow_drops = static_cast<int64_t>(GlobalMetrics().video_queue_overflow_drops.load(std::memory_order_relaxed));

            int64_t format_drops = static_cast<int64_t>(GlobalMetrics().video_format_transition_drops.load(std::memory_order_relaxed));

            int64_t stale_drops = static_cast<int64_t>(GlobalMetrics().video_stale_generation_drops.load(std::memory_order_relaxed));

            int64_t late_drops = static_cast<int64_t>(GlobalMetrics().video_presentation_late_drops.load(std::memory_order_relaxed));

            int64_t q_depth = static_cast<int64_t>(GlobalMetrics().video_queue_depth.load(std::memory_order_relaxed));



            int64_t explicit_drops = catchup_drops + overflow_drops + format_drops + stale_drops + late_drops;

            int64_t delta = decoded - (presented + explicit_drops + q_depth);

            GlobalMetrics().accounting_delta.store(delta, std::memory_order_relaxed);



            m_last_metrics_sample_ns = stats_now;

        }

    }



    if (mmcss) {

        ::AvRevertMmThreadCharacteristics(mmcss);

    }

}



void FrameScheduler::SchedulerLoopPresentationClock(std::stop_token stop) noexcept {

    using clock = duwn::clock::MonotonicClock;



    HANDLE timer = ::CreateWaitableTimerExW(

        nullptr, nullptr,

        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,

        TIMER_ALL_ACCESS);

    if (!timer) {

        timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);

    }



    int64_t clock_anchor_wall_ns = 0;

    int64_t media_anchor_pts_ns  = 0;

    int64_t next_tick_wall_ns    = 0;

    uint64_t active_gen          = 0;



    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {

        int64_t cur_tick_interval = m_tick_interval_ns.load(std::memory_order_relaxed);



        // Wait for first frame to anchor clock if not yet anchored

        if (!m_clock_anchored.load(std::memory_order_relaxed)) {

            std::unique_lock lock{m_queue_mutex};

            m_queue_cv.wait_for(lock, std::chrono::milliseconds(10), [this] {

                return !m_queue.empty();

            });

            if (m_queue.empty()) continue;



            const auto& first = m_queue.front();

            active_gen = first.format_generation;

            media_anchor_pts_ns = first.pts_ns;

            clock_anchor_wall_ns = clock::Now().time_since_epoch().count();

            next_tick_wall_ns = clock_anchor_wall_ns + cur_tick_interval;

            m_clock_anchored.store(true, std::memory_order_relaxed);

        }



        // Wait until next tick deadline using waitable timer

        int64_t now_ns = clock::Now().time_since_epoch().count();

        int64_t sleep_ns = next_tick_wall_ns - now_ns;



        if (sleep_ns > 1'000'000LL && timer) {

            LARGE_INTEGER due_time{};

            due_time.QuadPart = -(sleep_ns / 100LL);

            ::SetWaitableTimerEx(timer, &due_time, 0, nullptr, nullptr, nullptr, 0);

            ::WaitForSingleObject(timer, static_cast<DWORD>(sleep_ns / 1'000'000LL + 5));

        }



        int64_t actual_wake_ns = clock::Now().time_since_epoch().count();

        double wake_error_ms = static_cast<double>(actual_wake_ns - next_tick_wall_ns) / 1'000'000.0;

        m_wake_error_samples.push_back(wake_error_ms);



        // Advance next tick deadline

        next_tick_wall_ns += cur_tick_interval;

        if (actual_wake_ns - next_tick_wall_ns > cur_tick_interval * 2) {

            next_tick_wall_ns = actual_wake_ns + cur_tick_interval;

        }



        int64_t elapsed_wall_ns = actual_wake_ns - clock_anchor_wall_ns;

        int64_t target_pts_ns = media_anchor_pts_ns + elapsed_wall_ns;

        int64_t tolerance_ns = cur_tick_interval / 4;



        VideoFrame frame_to_present;

        uint64_t superseded_count = 0;



        // Check if stream timeline needs resynchronization

        {

            std::lock_guard lock{m_queue_mutex};

            if (!m_queue.empty()) {

                if (m_queue.front().format_generation != active_gen) {

                    active_gen = m_queue.front().format_generation;

                    media_anchor_pts_ns = m_queue.front().pts_ns;

                    clock_anchor_wall_ns = actual_wake_ns;

                    target_pts_ns = media_anchor_pts_ns;

                } else if (m_queue.front().pts_ns - target_pts_ns > cur_tick_interval * 2) {

                    media_anchor_pts_ns = m_queue.front().pts_ns;

                    clock_anchor_wall_ns = actual_wake_ns;

                    target_pts_ns = media_anchor_pts_ns;

                } else if (target_pts_ns - m_queue.back().pts_ns > 200'000'000LL) {

                    media_anchor_pts_ns = m_queue.front().pts_ns;

                    clock_anchor_wall_ns = actual_wake_ns;

                    target_pts_ns = media_anchor_pts_ns;

                }

            }

        }



        bool has_frame = PopEligibleFrame(target_pts_ns, tolerance_ns, frame_to_present, superseded_count);



        GlobalMetrics().presentation_ticks.fetch_add(1, std::memory_order_relaxed);



        if (superseded_count > 0) {

            GlobalMetrics().video_decoded_superseded.fetch_add(superseded_count, std::memory_order_relaxed);

            GlobalMetrics().video_dropped_frames.fetch_add(superseded_count, std::memory_order_relaxed);

            GlobalMetrics().session_drops.fetch_add(superseded_count, std::memory_order_relaxed);

        }



        if (has_frame) {

            int64_t present_start_ns = clock::Now().time_since_epoch().count();



            double schedule_lateness_ms = static_cast<double>(present_start_ns - target_pts_ns) / 1'000'000.0;

            m_lateness_samples.push_back(schedule_lateness_ms);



            if (frame_to_present.arrival_ns > 0) {

                double dec_to_pres_ms = static_cast<double>(present_start_ns - frame_to_present.arrival_ns) / 1'000'000.0;

                double queue_age_ms = static_cast<double>(actual_wake_ns - frame_to_present.arrival_ns) / 1'000'000.0;

                m_decode_to_present_samples.push_back(dec_to_pres_ms);

                m_queue_age_samples.push_back(queue_age_ms);

            }



            if (m_last_present_time_ns > 0) {

                double interval_ms = static_cast<double>(present_start_ns - m_last_present_time_ns) / 1'000'000.0;

                m_present_interval_samples.push_back(interval_ms);

            }

            m_last_present_time_ns = present_start_ns;



            if (m_on_present) m_on_present(frame_to_present);



            int64_t present_end_ns = clock::Now().time_since_epoch().count();

            double present_call_ms = static_cast<double>(present_end_ns - present_start_ns) / 1'000'000.0;

            m_present_call_samples.push_back(present_call_ms);



            GlobalMetrics().video_render_time_ms.store(present_call_ms, std::memory_order_relaxed);

            GlobalMetrics().video_rendered_frames.fetch_add(1, std::memory_order_relaxed);

            GlobalMetrics().presentation_unique_frames.fetch_add(1, std::memory_order_relaxed);

            m_prev_pts_ns = frame_to_present.pts_ns;

        } else {

            GlobalMetrics().presentation_repeated_ticks.fetch_add(1, std::memory_order_relaxed);

        }



        // 1 Hz percentiles publication

        int64_t stats_now = clock::Now().time_since_epoch().count();

        if (m_last_metrics_sample_ns == 0) {

            m_last_metrics_sample_ns = stats_now;

        } else if (stats_now - m_last_metrics_sample_ns >= 1'000'000'000LL) {

            CalcDiagnosticsStats(m_pts_delta_samples,

                                 GlobalMetrics().video_pts_delta_avg_ms,

                                 &GlobalMetrics().video_pts_delta_p50_ms,

                                 &GlobalMetrics().video_pts_delta_p95_ms);

            CalcDiagnosticsStats(m_wake_error_samples,

                                 GlobalMetrics().video_wake_error_avg_ms,

                                 nullptr,

                                 &GlobalMetrics().video_wake_error_p95_ms);

            CalcDiagnosticsStats(m_lateness_samples,

                                 GlobalMetrics().video_schedule_lateness_avg_ms,

                                 nullptr,

                                 nullptr);

            CalcDiagnosticsStats(m_decode_to_present_samples,

                                 GlobalMetrics().video_decode_to_present_avg_ms,

                                 nullptr,

                                 nullptr);

            CalcDiagnosticsStats(m_queue_age_samples,

                                 GlobalMetrics().video_queue_age_avg_ms,

                                 nullptr,

                                 nullptr);

            CalcDiagnosticsStats(m_present_call_samples,

                                 GlobalMetrics().video_present_call_avg_ms,

                                 &GlobalMetrics().video_present_call_p50_ms,

                                 &GlobalMetrics().video_present_call_p95_ms);

            CalcDiagnosticsStats(m_present_interval_samples,

                                 GlobalMetrics().video_present_interval_avg_ms,

                                 nullptr,

                                 &GlobalMetrics().video_present_interval_p95_ms);

            m_last_metrics_sample_ns = stats_now;

        }

    }



    if (timer) ::CloseHandle(timer);

}



} // namespace duwn::video

