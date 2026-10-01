// test_frame_scheduler.cpp — unit tests for FrameScheduler generation handling and pacing

#include "video/FrameScheduler.h"

#include <vector>

#include <chrono>

#include <unordered_map>

#include <thread>

#include <atomic>

#include <mutex>



using namespace duwn::video;



DUWN_TEST(frame_scheduler_generation_transition_purging) {

    SchedulerConfig cfg{};

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 8;

    cfg.late_threshold_ns = 38'000'000LL;

    cfg.drop_late_frames = false; // do not drop on latency in this test



    std::vector<uint64_t> presented_gens;

    auto on_present = [&presented_gens](const VideoFrame& f) {

        presented_gens.push_back(f.format_generation);

    };



    FrameScheduler scheduler(cfg, on_present);



    // Push 3 frames of generation 1

    VideoFrame f1{};

    f1.format_generation = 1;

    f1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f1);



    VideoFrame f2{};

    f2.format_generation = 1;

    f2.pts_ns = 1'033'366'700LL;

    scheduler.PushFrame(f2);



    VideoFrame f3{};

    f3.format_generation = 1;

    f3.pts_ns = 1'066'733'400LL;

    scheduler.PushFrame(f3);



    // Push frame of generation 2: this MUST purge unpresented gen 1 frames from queue

    VideoFrame f4{};

    f4.format_generation = 2;

    f4.pts_ns = 2'000'000'000LL;

    scheduler.PushFrame(f4);



    // Also attempt to push a stale frame from generation 1: MUST be dropped immediately

    VideoFrame stale{};

    stale.format_generation = 1;

    stale.pts_ns = 1'099'000'000LL;

    scheduler.PushFrame(stale);



    // Flush cleans everything cleanly

    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_queue_full_drops_oldest_retains_newest) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::PresentationClock;

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 2; // Small bounds: 2 frames max

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    uint64_t initial_q_full = duwn::GlobalMetrics().video_queue_full_drops.load();



    // Push frame 1 (pts = 100)

    VideoFrame f1{};

    f1.format_generation = 1;

    f1.pts_ns = 100;

    scheduler.PushFrame(f1);



    // Push frame 2 (pts = 200) -> Queue is now at capacity (2)

    VideoFrame f2{};

    f2.format_generation = 1;

    f2.pts_ns = 200;

    scheduler.PushFrame(f2);



    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_full_drops.load() == initial_q_full);



    // Push frame 3 (pts = 300) -> Over capacity: must drop oldest (f1) and accept f3

    VideoFrame f3{};

    f3.format_generation = 1;

    f3.pts_ns = 300;

    scheduler.PushFrame(f3);



    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_full_drops.load() == initial_q_full + 1);



    // Push frame 4 (pts = 400) -> Over capacity: must drop oldest (f2) and accept f4

    VideoFrame f4{};

    f4.format_generation = 1;

    f4.pts_ns = 400;

    scheduler.PushFrame(f4);



    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_full_drops.load() == initial_q_full + 2);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_burst_absorption_no_growth_past_max) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::PresentationClock;

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 3;

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    // Rapid burst of 10 frames into a capacity-3 queue

    for (int i = 0; i < 10; ++i) {

        VideoFrame f{};

        f.format_generation = 1;

        f.pts_ns = 1'000'000'000LL + i * 33'366'700LL;

        scheduler.PushFrame(f);

    }



    // Queue depth must never exceed max_queue_depth (3)

    // 10 pushed into capacity 3 => 7 dropped oldest

    int32_t q_depth = duwn::GlobalMetrics().video_queue_depth.load();

    DUWN_ASSERT(q_depth <= 3);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_lateness_sign_convention) {

    // Verify lateness convention:

    // schedule_lateness_ms = actual_present_start - target_present_time

    // Positive = late (presented after target)

    // Negative = early (presented before target)

    int64_t target_ns = 1'000'000'000LL;



    int64_t actual_late_ns = 1'005'000'000LL; // 5ms late

    double late_lateness_ms = static_cast<double>(actual_late_ns - target_ns) / 1'000'000.0;

    DUWN_ASSERT(late_lateness_ms > 0.0);

    DUWN_ASSERT(std::abs(late_lateness_ms - 5.0) < 0.001);



    int64_t actual_early_ns = 998'000'000LL; // 2ms early

    double early_lateness_ms = static_cast<double>(actual_early_ns - target_ns) / 1'000'000.0;

    DUWN_ASSERT(early_lateness_ms < 0.0);

    DUWN_ASSERT(std::abs(early_lateness_ms - (-2.0)) < 0.001);

}



DUWN_TEST(frame_scheduler_29_97_fps_cadence_derivation) {

    // 29.97 fps standard AirPlay video cadence interval:

    // 1 / 29.97 = 0.033366700033... seconds = 33.3667 ms = 33'366'700 ns

    constexpr double fps_29_97 = 30000.0 / 1001.0; // exact NTSC 29.97002997...

    constexpr double interval_ms = 1000.0 / fps_29_97;

    int64_t interval_ns = static_cast<int64_t>(interval_ms * 1'000'000.0 + 0.5);



    DUWN_ASSERT(std::abs(interval_ms - 33.3667) < 0.01);

    DUWN_ASSERT(interval_ns >= 33'366'000LL && interval_ns <= 33'367'000LL);



    // Verify EMA smoothing converges on 29.97 fps interval

    int64_t cadence_ns = 33'366'700LL;

    for (int i = 0; i < 16; ++i) {

        cadence_ns = (cadence_ns * 7 + interval_ns) / 8;

    }

    DUWN_ASSERT(cadence_ns >= 33'366'000LL && cadence_ns <= 33'367'000LL);

}



DUWN_TEST(frame_scheduler_60_fps_cadence_derivation) {

    // 60.0 fps gaming cadence interval:

    // 1 / 60.0 = 0.016666666... seconds = 16.6667 ms = 16'666'667 ns

    constexpr double fps_60 = 60.0;

    constexpr double interval_ms = 1000.0 / fps_60;

    int64_t interval_ns = static_cast<int64_t>(interval_ms * 1'000'000.0 + 0.5);



    DUWN_ASSERT(std::abs(interval_ms - 16.6667) < 0.01);

    DUWN_ASSERT(interval_ns >= 16'666'000LL && interval_ns <= 16'667'000LL);



    // Initialized at 33.37ms (standard AirPlay default), transitions to 60 fps

    int64_t cadence_ns = 33'366'700LL;

    for (int i = 0; i < 24; ++i) {

        cadence_ns = (cadence_ns * 7 + interval_ns) / 8;

    }

    // After 24 frames (~400ms at 60fps), cadence should converge to ~16.67ms

    DUWN_ASSERT(cadence_ns >= 16'666'000LL && cadence_ns <= 17'500'000LL);



    // Effective late threshold at 60fps: 1.25x estimated cadence = ~20.8ms

    int64_t effective_late_threshold_ns = (cadence_ns * 5) / 4;

    DUWN_ASSERT(effective_late_threshold_ns >= 20'000'000LL && effective_late_threshold_ns <= 22'000'000LL);

}



DUWN_TEST(frame_scheduler_dejitter_pop_eligible_selection) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::PresentationClock;

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 4;

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    // Push 3 frames of generation 1 in PTS order:

    // f1: 1'000'000'000

    // f2: 1'033'366'700

    // f3: 1'066'733'400

    VideoFrame f1{};

    f1.format_generation = 1;

    f1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f1);



    VideoFrame f2{};

    f2.format_generation = 1;

    f2.pts_ns = 1'033'366'700LL;

    scheduler.PushFrame(f2);



    VideoFrame f3{};

    f3.format_generation = 1;

    f3.pts_ns = 1'066'733'400LL;

    scheduler.PushFrame(f3);



    DUWN_ASSERT(scheduler.DeJitterQueueSize() == 3);



    // Target PTS 1'050'000'000 with tolerance 10'000'000:

    // f1 (1'000'000'000) and f2 (1'033'366'700) are <= 1'060'000'000.

    // f3 (1'066'733'400) is > 1'060'000'000.

    // PopEligibleFrame must choose f2, supersede f1 (count = 1), and leave f3 in queue.

    VideoFrame out_frame{};

    uint64_t superseded = 0;

    bool popped = scheduler.PopEligibleFrame(1'050'000'000LL, 10'000'000LL, out_frame, superseded);

    DUWN_ASSERT(popped);

    DUWN_ASSERT(out_frame.pts_ns == 1'033'366'700LL);

    DUWN_ASSERT(superseded == 1);

    DUWN_ASSERT(scheduler.DeJitterQueueSize() == 1);



    // Next tick: Target PTS 1'080'000'000 with tolerance 10'000'000:

    // f3 (1'066'733'400) <= 1'090'000'000.

    popped = scheduler.PopEligibleFrame(1'080'000'000LL, 10'000'000LL, out_frame, superseded);

    DUWN_ASSERT(popped);

    DUWN_ASSERT(out_frame.pts_ns == 1'066'733'400LL);

    DUWN_ASSERT(superseded == 0);

    DUWN_ASSERT(scheduler.DeJitterQueueSize() == 0);



    // Third tick with empty queue: returns false without error or crash

    popped = scheduler.PopEligibleFrame(1'110'000'000LL, 10'000'000LL, out_frame, superseded);

    DUWN_ASSERT(!popped);

    DUWN_ASSERT(superseded == 0);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_cadence_outlier_rejection) {

    SchedulerConfig cfg{};

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 16;

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    uint64_t initial_outliers = duwn::GlobalMetrics().source_outliers.load();



    // Push 15 frames with regular ~30fps delta (33.37ms)

    int64_t pts = 1'000'000'000LL;

    int64_t last_pts = pts;

    for (int i = 0; i < 15; ++i) {

        VideoFrame f{};

        f.format_generation = 1;

        f.pts_ns = pts;

        scheduler.PushFrame(f);

        last_pts = pts;

        pts += 33'366'700LL;

    }



    // Verify steady nominal fps is 29.97 and outliers haven't grown

    DUWN_ASSERT(duwn::GlobalMetrics().source_nominal_fps.load() == 29.97);

    DUWN_ASSERT(duwn::GlobalMetrics().source_outliers.load() == initial_outliers);

    DUWN_ASSERT(scheduler.TickIntervalNs() == 33'366'700LL);



    // Push an outlier delta < 8ms (e.g. 2ms duplicate/subslice)

    VideoFrame f_small{};

    f_small.format_generation = 1;

    f_small.pts_ns = last_pts + 2'000'000LL;

    scheduler.PushFrame(f_small);



    DUWN_ASSERT(duwn::GlobalMetrics().source_outliers.load() == initial_outliers + 1);



    // Push an outlier delta > 75ms (e.g. 120ms network stall)

    VideoFrame f_large{};

    f_large.format_generation = 1;

    f_large.pts_ns = f_small.pts_ns + 120'000'000LL;

    scheduler.PushFrame(f_large);



    DUWN_ASSERT(duwn::GlobalMetrics().source_outliers.load() == initial_outliers + 2);



    // Outliers must NOT change the classified nominal cadence (remains 29.97 fps)

    DUWN_ASSERT(duwn::GlobalMetrics().source_nominal_fps.load() == 29.97);

    DUWN_ASSERT(scheduler.TickIntervalNs() == 33'366'700LL);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_cadence_hysteresis_short_burst) {

    SchedulerConfig cfg{};

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 32;

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    // Ensure initial nominal is 29.97

    duwn::GlobalMetrics().source_nominal_fps.store(29.97);



    // Feed a short burst of 16 frames with 60fps delta (only 1 evaluation window)

    int64_t pts = 1'000'000'000LL;

    for (int i = 0; i < 16; ++i) {

        VideoFrame f{};

        f.format_generation = 1;

        f.pts_ns = pts;

        scheduler.PushFrame(f);

        pts += 16'666'667LL;

    }



    // Due to hysteresis (requires 2 consecutive windows), nominal fps must NOT flip yet

    DUWN_ASSERT(duwn::GlobalMetrics().source_nominal_fps.load() == 29.97);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_cadence_60fps_transition) {

    SchedulerConfig cfg{};

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 32;

    cfg.drop_late_frames = false;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    // Feed 50 frames with 60fps delta (16'666'667 ns), spanning >= 2 evaluation windows (>= 20 samples in history)

    int64_t pts = 2'000'000'000LL;

    for (int i = 0; i < 50; ++i) {

        VideoFrame f{};

        f.format_generation = 1;

        f.pts_ns = pts;

        scheduler.PushFrame(f);

        pts += 16'666'667LL;

    }



    // Must confirm and lock to 60.0 fps nominal and 16.67ms tick interval

    DUWN_ASSERT(duwn::GlobalMetrics().source_nominal_fps.load() == 60.0);

    DUWN_ASSERT(scheduler.TickIntervalNs() == 16'666'667LL);



    scheduler.Flush();

}



DUWN_TEST(frame_scheduler_flush_resets_session_metrics) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::PresentationClock;

    cfg.frame_duration_ns = 33'366'700LL;

    cfg.max_queue_depth = 4;



    FrameScheduler scheduler(cfg, [](const VideoFrame&) {});



    // Set artificial session metrics

    duwn::GlobalMetrics().session_q_full.store(15);

    duwn::GlobalMetrics().session_drops.store(25);



    VideoFrame f{};

    f.format_generation = 1;

    f.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f);

    DUWN_ASSERT(scheduler.DeJitterQueueSize() == 1);



    // Flush must reset session metrics and empty de-jitter buffer

    scheduler.Flush();



    DUWN_ASSERT(scheduler.DeJitterQueueSize() == 0);

    DUWN_ASSERT(duwn::GlobalMetrics().session_q_full.load() == 0);

    DUWN_ASSERT(duwn::GlobalMetrics().session_drops.load() == 0);

    DUWN_ASSERT(scheduler.IsClockAnchored() == false);

}



DUWN_TEST(TestDecodedFrameQueue_BurstRetention) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t init_drops = duwn::GlobalMetrics().video_dropped_frames.load();



    // Push frame 1

    VideoFrame f1{};

    f1.sequence_number = 1;

    f1.format_generation = 1;

    f1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f1);



    // Push frame 2 with timestamp 0.5ms apart (rapid burst)

    VideoFrame f2{};

    f2.sequence_number = 2;

    f2.format_generation = 1;

    f2.pts_ns = 1'000'500'000LL;

    scheduler.PushFrame(f2);



    // Verify: queue depth = 2, 0 drops, both frames retrievable in FIFO order

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 2);

    DUWN_ASSERT(duwn::GlobalMetrics().video_dropped_frames.load() == init_drops);



    VideoFrame out1{};

    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out1) == true);

    DUWN_ASSERT(out1.sequence_number == 1);



    VideoFrame out2{};

    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out2) == true);

    DUWN_ASSERT(out2.sequence_number == 2);



    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);

    scheduler.Flush();

}



DUWN_TEST(TestDecodedFrameQueue_Capacity3) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t init_drops = duwn::GlobalMetrics().video_dropped_frames.load();



    // Push 3 frames

    for (int i = 1; i <= 3; ++i) {

        VideoFrame f{};

        f.sequence_number = static_cast<uint64_t>(i);

        f.format_generation = 1;

        f.pts_ns = 1'000'000'000LL + i * 500'000LL;

        scheduler.PushFrame(f);

    }



    // Verify: queue depth = 3, 0 drops

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);

    DUWN_ASSERT(duwn::GlobalMetrics().video_dropped_frames.load() == init_drops);



    scheduler.Flush();

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);

}



DUWN_TEST(TestDecodedFrameQueue_StalenessDrop) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t init_catchup_drops = duwn::GlobalMetrics().video_latency_catchup_drops.load();



    // Push 3 frames

    for (int i = 1; i <= 3; ++i) {

        VideoFrame f{};

        f.sequence_number = static_cast<uint64_t>(i);

        f.format_generation = 1;

        f.pts_ns = 1'000'000'000LL + i * 500'000LL;

        scheduler.PushFrame(f);

    }

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);



    // Advance time past staleness threshold (16.67ms * 1.75 = ~29.2ms)

    std::this_thread::sleep_for(std::chrono::milliseconds(40));



    // Push 4th frame

    VideoFrame f4{};

    f4.sequence_number = 4;

    f4.format_generation = 1;

    f4.pts_ns = 1'000'000'000LL + 40'000'000LL;

    scheduler.PushFrame(f4);



    // Verify: oldest frame dropped, video_latency_catchup_drops incremented, queue depth = 3, newest 3 frames retained (2, 3, 4)

    DUWN_ASSERT(duwn::GlobalMetrics().video_latency_catchup_drops.load() == init_catchup_drops + 1);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);



    VideoFrame out{};

    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out) == true);

    DUWN_ASSERT(out.sequence_number == 2); // Frame 1 was dropped, so 2 is first



    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out) == true);

    DUWN_ASSERT(out.sequence_number == 3);



    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out) == true);

    DUWN_ASSERT(out.sequence_number == 4);



    scheduler.Flush();

}



DUWN_TEST(TestDecodedFrameQueue_BurstOverflow) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t init_overflow = duwn::GlobalMetrics().video_queue_overflow_drops.load();



    // Push 4 frames within 1ms (burst with no sleep, all fresh)

    for (int i = 1; i <= 4; ++i) {

        VideoFrame f{};

        f.sequence_number = static_cast<uint64_t>(i);

        f.format_generation = 1;

        f.pts_ns = 1'000'000'000LL + i * 100'000LL;

        scheduler.PushFrame(f);

    }



    // Verify: video_queue_overflow_drops incremented, queue depth = 3

    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_overflow_drops.load() == init_overflow + 1);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);



    scheduler.Flush();

}



DUWN_TEST(TestFrameAccounting_ZeroDelta) {

    // Invariant: decoded = presented + explicit_drops + queued

    auto& m = duwn::GlobalMetrics();



    // Take baseline snapshot

    uint64_t base_decoded = m.video_decoded_frames.load();

    uint64_t base_rendered = m.video_rendered_frames.load();

    uint64_t base_catchup = m.video_latency_catchup_drops.load();

    uint64_t base_overflow = m.video_queue_overflow_drops.load();

    uint64_t base_format = m.video_format_transition_drops.load();

    uint64_t base_stale = m.video_stale_generation_drops.load();

    uint64_t base_late = m.video_presentation_late_drops.load();



    // Simulate pipeline run of 10 decoded frames: 6 presented, 2 catchup drops, 1 overflow drop, 1 still queued

    m.video_decoded_frames.fetch_add(10);

    m.video_rendered_frames.fetch_add(6);

    m.video_latency_catchup_drops.fetch_add(2);

    m.video_queue_overflow_drops.fetch_add(1);

    m.video_queue_depth.store(1);



    int64_t delta_decoded = static_cast<int64_t>(m.video_decoded_frames.load() - base_decoded);

    int64_t delta_presented = static_cast<int64_t>(m.video_rendered_frames.load() - base_rendered);

    int64_t delta_drops = static_cast<int64_t>(

        (m.video_latency_catchup_drops.load() - base_catchup) +

        (m.video_queue_overflow_drops.load() - base_overflow) +

        (m.video_format_transition_drops.load() - base_format) +

        (m.video_stale_generation_drops.load() - base_stale) +

        (m.video_presentation_late_drops.load() - base_late));

    int64_t queued = static_cast<int64_t>(m.video_queue_depth.load());



    int64_t delta = delta_decoded - (delta_presented + delta_drops + queued);

    m.accounting_delta.store(delta);



    DUWN_ASSERT(delta == 0);

    DUWN_ASSERT(m.accounting_delta.load() == 0);



    // Rollback test values

    m.video_decoded_frames.store(base_decoded);

    m.video_rendered_frames.store(base_rendered);

    m.video_latency_catchup_drops.store(base_catchup);

    m.video_queue_overflow_drops.store(base_overflow);

    m.video_queue_depth.store(0);

}



DUWN_TEST(TestDecodedFrameQueue_GenerationTransition) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    // Push gen 1 frame

    VideoFrame f1{};

    f1.sequence_number = 1;

    f1.format_generation = 1;

    f1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f1);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);



    // Push gen 2 frame: triggers format generation transition

    VideoFrame f2{};

    f2.sequence_number = 2;

    f2.format_generation = 2;

    f2.pts_ns = 2'000'000'000LL;

    scheduler.PushFrame(f2);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1); // Gen 1 purged, gen 2 added



    // Attempt to push stale gen 1 frame: must be dropped immediately and queue untouched

    uint64_t stale_before = duwn::GlobalMetrics().video_stale_generation_drops.load();

    VideoFrame f_stale{};

    f_stale.sequence_number = 3;

    f_stale.format_generation = 1;

    f_stale.pts_ns = 1'016'666'667LL;

    scheduler.PushFrame(f_stale);



    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_generation_drops.load() == stale_before + 1);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);



    scheduler.Flush();

}



DUWN_TEST(TestDecodedFrameQueue_PopLatestValidFrame_HealthySingle) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    VideoFrame f1{};

    f1.sequence_number = 10;

    f1.format_generation = 1;

    f1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f1);



    VideoFrame out{};

    uint64_t superseded_drops = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded_drops);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 10);

    DUWN_ASSERT(superseded_drops == 0);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);



    scheduler.Flush();

}



DUWN_TEST(TestDecodedFrameQueue_PopLatestValidFrame_BacklogDrop) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t init_catchup = duwn::GlobalMetrics().video_latency_catchup_drops.load();



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // Push 3 frames of generation 1, oldest exceeds freshness budget

    for (int i = 1; i <= 3; ++i) {

        VideoFrame f{};

        f.sequence_number = static_cast<uint64_t>(i);

        f.format_generation = 1;

        f.pts_ns = 1'000'000'000LL + i * 16'666'667LL;

        if (i == 1) {

            f.queue_push_qpc = now_qpc - (qpc_freq * 40 / 1000); // 40ms age -> stale

        } else {

            f.queue_push_qpc = now_qpc - (qpc_freq * (3 - i) * 5 / 1000);

        }

        scheduler.PushFrame(f);

    }

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);



    // PopLatestValidFrame should drop older frames (1 and 2) and return the newest (3)

    VideoFrame out{};

    uint64_t superseded_drops = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded_drops);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 3);

    DUWN_ASSERT(superseded_drops == 2);

    DUWN_ASSERT(duwn::GlobalMetrics().video_latency_catchup_drops.load() == init_catchup + 2);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);



    scheduler.Flush();

}



DUWN_TEST(depth2_both_fresh_no_drop) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;

    cfg.frame_duration_ns = 16'666'667LL; // 60 FPS

    duwn::GlobalMetrics().source_nominal_fps.store(60.0);



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // Frame A: age = 10ms

    VideoFrame fA{};

    fA.sequence_number = 1;

    fA.format_generation = 1;

    fA.queue_push_qpc = now_qpc - (qpc_freq * 10 / 1000);

    scheduler.PushFrame(fA);



    // Frame B: age = 2ms

    VideoFrame fB{};

    fB.sequence_number = 2;

    fB.format_generation = 1;

    fB.queue_push_qpc = now_qpc - (qpc_freq * 2 / 1000);

    scheduler.PushFrame(fB);



    DUWN_ASSERT(scheduler.DecodedQueueSize() == 2);



    uint64_t init_drops = duwn::GlobalMetrics().video_stale_age_drops.load();

    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);



    // Both are fresh! Present A normally, no drop

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 1);

    DUWN_ASSERT(superseded == 0);

    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_age_drops.load() == init_drops);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1); // Frame B still in queue



    scheduler.Flush();

}



DUWN_TEST(depth3_old_history_select_newest) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;

    cfg.frame_duration_ns = 16'666'667LL; // 60 FPS

    duwn::GlobalMetrics().source_nominal_fps.store(60.0);



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // A: age = 39ms

    VideoFrame fA{};

    fA.sequence_number = 1;

    fA.format_generation = 1;

    fA.queue_push_qpc = now_qpc - (qpc_freq * 39 / 1000);

    scheduler.PushFrame(fA);



    // B: age = 22ms

    VideoFrame fB{};

    fB.sequence_number = 2;

    fB.format_generation = 1;

    fB.queue_push_qpc = now_qpc - (qpc_freq * 22 / 1000);

    scheduler.PushFrame(fB);



    // C: age = 5ms

    VideoFrame fC{};

    fC.sequence_number = 3;

    fC.format_generation = 1;

    fC.queue_push_qpc = now_qpc - (qpc_freq * 5 / 1000);

    scheduler.PushFrame(fC);



    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);



    uint64_t init_stale_drops = duwn::GlobalMetrics().video_stale_age_drops.load();

    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);



    // A is 39ms > 20.8ms freshness budget -> stale history discarded, return C

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 3);

    DUWN_ASSERT(superseded == 2);

    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_age_drops.load() == init_stale_drops + 2);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);



    scheduler.Flush();

}



DUWN_TEST(single_fresh_present) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // A: age = 6ms

    VideoFrame fA{};

    fA.sequence_number = 100;

    fA.format_generation = 1;

    fA.queue_push_qpc = now_qpc - (qpc_freq * 6 / 1000);

    scheduler.PushFrame(fA);



    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);



    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 100);

    DUWN_ASSERT(superseded == 0);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);



    scheduler.Flush();

}



DUWN_TEST(thirty_fps_freshness_budget) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;

    cfg.frame_duration_ns = 33'366'700LL;

    duwn::GlobalMetrics().source_nominal_fps.store(29.97);



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // At 30 FPS, cadence = 33.37ms, freshness budget = 1.25 * 33.37 = ~41.7ms

    // Frame A: age = 35ms (within 41.7ms budget)

    VideoFrame fA{};

    fA.sequence_number = 10;

    fA.format_generation = 1;

    fA.queue_push_qpc = now_qpc - (qpc_freq * 35 / 1000);

    scheduler.PushFrame(fA);



    // Frame B: age = 5ms

    VideoFrame fB{};

    fB.sequence_number = 11;

    fB.format_generation = 1;

    fB.queue_push_qpc = now_qpc - (qpc_freq * 5 / 1000);

    scheduler.PushFrame(fB);



    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);



    // 35ms is fresh for 30 FPS! Present A, no drop

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 10);

    DUWN_ASSERT(superseded == 0);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);



    // Now push a frame with age = 50ms (> 41.7ms)

    VideoFrame f_stale{};

    f_stale.sequence_number = 12;

    f_stale.format_generation = 1;

    f_stale.queue_push_qpc = now_qpc - (qpc_freq * 50 / 1000);



    // Clear and test stale 30fps

    scheduler.Flush();

    scheduler.PushFrame(f_stale);



    VideoFrame f_fresh{};

    f_fresh.sequence_number = 13;

    f_fresh.format_generation = 1;

    f_fresh.queue_push_qpc = now_qpc - (qpc_freq * 5 / 1000);

    scheduler.PushFrame(f_fresh);



    ok = scheduler.PopLatestValidFrame(out, superseded);

    // 50ms exceeds 41.7ms -> stale history dropped, newest (13) returned

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 13);

    DUWN_ASSERT(superseded == 1);



    scheduler.Flush();

}



DUWN_TEST(sixty_fps_freshness_budget) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;

    cfg.frame_duration_ns = 16'666'667LL;

    duwn::GlobalMetrics().source_nominal_fps.store(60.0);



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();

    int64_t qpc_freq = clock::Frequency();



    // At 60 FPS, cadence = 16.67ms, freshness budget = 1.25 * 16.67 = ~20.83ms

    // Frame A: age = 15ms (<= 20.83ms -> fresh)

    VideoFrame fA{};

    fA.sequence_number = 20;

    fA.format_generation = 1;

    fA.queue_push_qpc = now_qpc - (qpc_freq * 15 / 1000);

    scheduler.PushFrame(fA);



    VideoFrame fB{};

    fB.sequence_number = 21;

    fB.format_generation = 1;

    fB.queue_push_qpc = now_qpc - (qpc_freq * 2 / 1000);

    scheduler.PushFrame(fB);



    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 20);

    DUWN_ASSERT(superseded == 0);



    // Frame with age = 25ms (> 20.83ms -> stale)

    scheduler.Flush();

    VideoFrame f_stale{};

    f_stale.sequence_number = 22;

    f_stale.format_generation = 1;

    f_stale.queue_push_qpc = now_qpc - (qpc_freq * 25 / 1000);

    scheduler.PushFrame(f_stale);



    VideoFrame f_new{};

    f_new.sequence_number = 23;

    f_new.format_generation = 1;

    f_new.queue_push_qpc = now_qpc - (qpc_freq * 2 / 1000);

    scheduler.PushFrame(f_new);



    ok = scheduler.PopLatestValidFrame(out, superseded);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 23);

    DUWN_ASSERT(superseded == 1);



    scheduler.Flush();

}



DUWN_TEST(generation_invalid_frames_always_drop) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    // Active generation is set to 2

    VideoFrame f_gen2{};

    f_gen2.sequence_number = 1;

    f_gen2.format_generation = 2;

    f_gen2.pts_ns = 2'000'000'000LL;

    scheduler.PushFrame(f_gen2);



    // Now attempt to pop: returns gen 2 frame

    VideoFrame out{};

    uint64_t superseded = 0;

    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded) == true);

    DUWN_ASSERT(out.format_generation == 2);



    // Push gen 1 frame: PushFrame drops it immediately because gen 1 < active_generation (2)

    uint64_t stale_before = duwn::GlobalMetrics().video_stale_generation_drops.load();

    VideoFrame f_gen1{};

    f_gen1.sequence_number = 2;

    f_gen1.format_generation = 1;

    f_gen1.pts_ns = 1'000'000'000LL;

    scheduler.PushFrame(f_gen1);



    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_generation_drops.load() == stale_before + 1);

    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);



    // Pop returns false, no invalid generation is ever returned

    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded) == false);



    scheduler.Flush();

}



DUWN_TEST(stale_drop_counter_separate_from_overflow) {

    SchedulerConfig cfg{};

    cfg.mode = SchedulerMode::GameLowLatency;

    cfg.frame_duration_ns = 16'666'667LL;

    duwn::GlobalMetrics().source_nominal_fps.store(60.0);



    FrameScheduler scheduler(cfg, [](VideoFrame&) {});



    uint64_t base_overflow = duwn::GlobalMetrics().video_queue_overflow_drops.load();

    uint64_t base_stale = duwn::GlobalMetrics().video_stale_age_drops.load();



    using clock = duwn::clock::MonotonicClock;

    int64_t now_qpc = clock::NowQpcTicks();



    // 1. Trigger QUEUE OVERFLOW: push 4 fresh frames into capacity-3 queue

    for (int i = 1; i <= 4; ++i) {

        VideoFrame f{};

        f.sequence_number = static_cast<uint64_t>(i);

        f.format_generation = 1;

        f.queue_push_qpc = now_qpc; // All fresh (0ms age)

        f.pts_ns = 1'000'000'000LL + i * 1'000'000LL;

        scheduler.PushFrame(f);

    }



    // Overflow drops should increment, stale age drops should NOT

    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_overflow_drops.load() == base_overflow + 1);

    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_age_drops.load() == base_stale);



    scheduler.Flush();



    // 2. Trigger STALE AGE DROP: push 2 frames with oldest age = 40ms (> 20.8ms)

    int64_t qpc_freq = clock::Frequency();

    VideoFrame f_stale{};

    f_stale.sequence_number = 10;

    f_stale.format_generation = 1;

    f_stale.queue_push_qpc = now_qpc - (qpc_freq * 40 / 1000);

    scheduler.PushFrame(f_stale);



    VideoFrame f_fresh{};

    f_fresh.sequence_number = 11;

    f_fresh.format_generation = 1;

    f_fresh.queue_push_qpc = now_qpc - (qpc_freq * 2 / 1000);

    scheduler.PushFrame(f_fresh);



    VideoFrame out{};

    uint64_t superseded = 0;

    bool ok = scheduler.PopLatestValidFrame(out, superseded);

    DUWN_ASSERT(ok == true);

    DUWN_ASSERT(out.sequence_number == 11);

    DUWN_ASSERT(superseded == 1);



    // Stale age drops should increment, overflow drops should NOT

    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_age_drops.load() == base_stale + 1);

    DUWN_ASSERT(duwn::GlobalMetrics().video_queue_overflow_drops.load() == base_overflow + 1);



    scheduler.Flush();

}



DUWN_TEST(FrameScheduler_ConcurrentMultiThreadStress) {
    SchedulerConfig cfg{};
    cfg.mode = SchedulerMode::GameLowLatency;
    cfg.frame_duration_ns = 16'666'667LL; // 60 FPS

    std::atomic<uint64_t> frames_received{0};
    std::atomic<uint64_t> frames_corrupted{0};
    std::atomic<uint64_t> sequence_inversions{0};

    std::mutex tracker_mutex;
    std::unordered_map<uint64_t, uint64_t> last_seq_per_gen;

    auto on_present = [&](VideoFrame& f) {
        frames_received.fetch_add(1, std::memory_order_relaxed);

        if (f.format_generation == 0 || f.pts_ns <= 0) {
            frames_corrupted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::lock_guard lock(tracker_mutex);
        auto it = last_seq_per_gen.find(f.format_generation);
        if (it != last_seq_per_gen.end()) {
            if (f.sequence_number <= it->second) {
                sequence_inversions.fetch_add(1, std::memory_order_relaxed);
            }
            it->second = f.sequence_number;
        } else {
            last_seq_per_gen[f.format_generation] = f.sequence_number;
        }
    };

    FrameScheduler scheduler(cfg, on_present);
    scheduler.Start();

    std::atomic<bool> start_all{false};
    std::atomic<bool> producer_active{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<int> flushes_while_producer_active{0};
    std::atomic<int> switches_while_producer_active{0};

    constexpr int kPhases = 5;
    constexpr int kFramesPerPhase = 80;

    // Thread 1: Decode Producer Thread
    std::thread producer_thread([&]() {
        while (!start_all.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        using clock = duwn::clock::MonotonicClock;
        producer_active.store(true, std::memory_order_release);

        for (int phase = 0; phase < kPhases && !stop_flag.load(std::memory_order_relaxed); ++phase) {
            uint64_t cur_gen = static_cast<uint64_t>(phase + 1);
            for (int i = 1; i <= kFramesPerPhase && !stop_flag.load(std::memory_order_relaxed); ++i) {
                VideoFrame f{};
                f.sequence_number = static_cast<uint64_t>(i);
                f.format_generation = cur_gen;
                f.pts_ns = 1'000'000'000LL * cur_gen + i * 16'666'667LL;
                f.queue_push_qpc = clock::NowQpcTicks();
                f.process_output_qpc = f.queue_push_qpc;
                f.visible_width = 1920;
                f.visible_height = 1080;

                scheduler.PushFrame(std::move(f));
                std::this_thread::sleep_for(std::chrono::microseconds(400));
            }
        }

        producer_active.store(false, std::memory_order_release);
    });

    // Thread 2: UI Flush Thread
    std::thread flush_thread([&]() {
        while (!start_all.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop_flag.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (producer_active.load(std::memory_order_acquire)) {
                scheduler.Flush();
                flushes_while_producer_active.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // Thread 3: UI Policy Switch Thread
    std::thread policy_thread([&]() {
        while (!start_all.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        int switch_count = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            if (producer_active.load(std::memory_order_acquire)) {
                switch (switch_count % 4) {
                case 0:
                    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::LowLatency));
                    break;
                case 1:
                    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::SmoothLive));
                    break;
                case 2:
                    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::Compatibility));
                    break;
                case 3:
                    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 25, 2));
                    break;
                }
                switches_while_producer_active.fetch_add(1, std::memory_order_relaxed);
                switch_count++;
            }
        }
    });

    // Watchdog to prevent test hang
    std::atomic<bool> test_timed_out{false};
    std::thread watchdog([&]() {
        for (int i = 0; i < 80 && !stop_flag.load(std::memory_order_relaxed); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!stop_flag.load(std::memory_order_relaxed)) {
            test_timed_out.store(true, std::memory_order_release);
            stop_flag.store(true, std::memory_order_release);
        }
    });

    // Start all concurrent threads simultaneously
    start_all.store(true, std::memory_order_release);

    producer_thread.join();
    stop_flag.store(true, std::memory_order_release);
    flush_thread.join();
    policy_thread.join();
    watchdog.join();

    scheduler.Stop();

    DUWN_ASSERT(!test_timed_out.load());
    DUWN_ASSERT(flushes_while_producer_active.load() >= 5);
    DUWN_ASSERT(switches_while_producer_active.load() >= 5);
    DUWN_ASSERT(frames_corrupted.load() == 0);
    DUWN_ASSERT(sequence_inversions.load() == 0);
    DUWN_ASSERT(frames_received.load() > 0);
}

DUWN_TEST(FrameScheduler_FormatGenerationChangeAndSequenceRestartAfterFlush) {
    SchedulerConfig cfg{};
    cfg.mode = SchedulerMode::GameLowLatency;
    cfg.frame_duration_ns = 16'666'667LL;

    std::atomic<uint64_t> presented_count{0};
    std::vector<uint64_t> presented_seqs;
    std::vector<uint64_t> presented_gens;
    std::mutex pres_mutex;

    FrameScheduler scheduler(cfg, [&](VideoFrame& f) {
        std::lock_guard lock(pres_mutex);
        presented_count.fetch_add(1, std::memory_order_relaxed);
        presented_seqs.push_back(f.sequence_number);
        presented_gens.push_back(f.format_generation);
    });
    scheduler.Start();

    using clock = duwn::clock::MonotonicClock;

    // Phase 1: Push generation 1, sequences 1, 2, 3
    for (uint64_t i = 1; i <= 3; ++i) {
        VideoFrame f{};
        f.sequence_number = i;
        f.format_generation = 1;
        f.pts_ns = 1'000'000'000LL + i * 16'666'667LL;
        f.queue_push_qpc = clock::NowQpcTicks();
        f.process_output_qpc = f.queue_push_qpc;
        f.visible_width = 1920;
        f.visible_height = 1080;
        scheduler.PushFrame(std::move(f));
    }

    auto start_t = std::chrono::steady_clock::now();
    while (presented_count.load() < 1 &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_t).count() < 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    DUWN_ASSERT(presented_count.load() > 0);

    // Phase 2: Flush contract verification (clears queue, resets sequence anchor)
    scheduler.Flush();

    // Phase 3: Push generation 2, sequence restarts from 1
    const auto stale_drops_before = duwn::GlobalMetrics().video_stale_generation_drops.load();
    for (uint64_t i = 1; i <= 3; ++i) {
        VideoFrame f{};
        f.sequence_number = i; // Restarted sequence from 1
        f.format_generation = 2; // Advanced generation
        f.pts_ns = 2'000'000'000LL + i * 16'666'667LL;
        f.queue_push_qpc = clock::NowQpcTicks();
        f.process_output_qpc = f.queue_push_qpc;
        f.visible_width = 1920;
        f.visible_height = 1080;
        scheduler.PushFrame(std::move(f));
    }

    start_t = std::chrono::steady_clock::now();
    const auto count_before_gen2 = presented_count.load();
    while (presented_count.load() == count_before_gen2 &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_t).count() < 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    DUWN_ASSERT(presented_count.load() > count_before_gen2);

    // Phase 4: Stale generation frame rejection
    VideoFrame stale_frame{};
    stale_frame.sequence_number = 99;
    stale_frame.format_generation = 1; // older than active gen 2
    stale_frame.pts_ns = 3'000'000'000LL;
    stale_frame.queue_push_qpc = clock::NowQpcTicks();
    stale_frame.process_output_qpc = stale_frame.queue_push_qpc;
    stale_frame.visible_width = 1920;
    stale_frame.visible_height = 1080;
    scheduler.PushFrame(std::move(stale_frame));

    DUWN_ASSERT(duwn::GlobalMetrics().video_stale_generation_drops.load() == stale_drops_before + 1);

    scheduler.Stop();
}



