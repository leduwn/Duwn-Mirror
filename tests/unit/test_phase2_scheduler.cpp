#include "video/FrameScheduler.h"
#include "video/RtpTimestampMapper.h"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace phase2_scheduler_tests {

duwn::video::VideoFrame Frame(uint64_t sequence, uint64_t generation,
                              int64_t pts_ns, int64_t age_ms = 0) {
    duwn::video::VideoFrame frame{};
    frame.sequence_number = sequence;
    frame.format_generation = generation;
    frame.pts_ns = pts_ns;
    frame.queue_push_qpc = duwn::clock::MonotonicClock::NowQpcTicks() -
        duwn::clock::MonotonicClock::Frequency() * age_ms / 1000;
    return frame;
}

} // namespace phase2_scheduler_tests

DUWN_TEST(Phase2_RtpTimelinePreservesCadenceWrapResetAndDiscontinuity) {
    duwn::video::RtpTimestampMapper timeline;
    constexpr int64_t arrival = 1'000'000'000LL;
    const uint32_t first = 0xFFFFF000u;
    DUWN_ASSERT(timeline.Map(first, arrival) == arrival);
    DUWN_ASSERT(timeline.Map(first + 1500u, arrival + 1'000'000LL) == arrival + 16'666'666LL);
    DUWN_ASSERT(timeline.Map(first + 3000u, arrival + 2'000'000LL) == arrival + 33'333'333LL);

    const int64_t after_discontinuity = timeline.Map(first + 2'000'000u, arrival + 5'000'000'000LL);
    DUWN_ASSERT(after_discontinuity == arrival + 5'000'000'000LL);
    timeline.Reset();
    DUWN_ASSERT(timeline.Map(42, arrival + 6'000'000'000LL) == arrival + 6'000'000'000LL);
}

DUWN_TEST(Phase2_BurstyArrivalRegressionUsesRtpCadence) {
    using namespace duwn::video;
    using phase2_scheduler_tests::Frame;

    SchedulerConfig cfg{};
    cfg.mode = SchedulerMode::GameLowLatency;
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::SmoothLive);
    VideoFrame out{};
    uint64_t superseded = 0;

    // Legacy input used 1 ms packet-arrival deltas as PTS, so 60 FPS was never learned.
    duwn::GlobalMetrics().source_nominal_fps.store(29.97);
    FrameScheduler arrival_scheduler(cfg, [](VideoFrame&) {});
    for (uint64_t i = 0; i < 60; ++i) {
        arrival_scheduler.PushFrame(Frame(i + 1, 1, 1'000'000'000LL + static_cast<int64_t>(i) * 1'000'000LL));
        DUWN_ASSERT(arrival_scheduler.PopLatestValidFrame(out, superseded));
    }
    DUWN_ASSERT(std::abs(duwn::GlobalMetrics().source_nominal_fps.load() - 29.97) < 0.01);
    arrival_scheduler.PushFrame(Frame(61, 1, 2'000'000'000LL, 25));
    arrival_scheduler.PushFrame(Frame(62, 1, 2'016'666'667LL, 1));
    DUWN_ASSERT(arrival_scheduler.PopLatestValidFrame(out, superseded));
    DUWN_ASSERT(out.sequence_number == 61 && superseded == 0);

    // RTP deltas recover 60 FPS and make the same 25 ms-old frame stale.
    duwn::GlobalMetrics().source_nominal_fps.store(29.97);
    FrameScheduler rtp_scheduler(cfg, [](VideoFrame&) {});
    RtpTimestampMapper timeline;
    for (uint64_t i = 0; i < 60; ++i) {
        const int64_t pts = timeline.Map(90'000u + static_cast<uint32_t>(i * 1500),
                                         3'000'000'000LL + static_cast<int64_t>(i) * 1'000'000LL);
        rtp_scheduler.PushFrame(Frame(i + 1, 1, pts));
        DUWN_ASSERT(rtp_scheduler.PopLatestValidFrame(out, superseded));
    }
    DUWN_ASSERT(std::abs(duwn::GlobalMetrics().source_nominal_fps.load() - 60.0) < 0.01);
    rtp_scheduler.PushFrame(Frame(61, 1, 4'000'000'000LL, 25));
    rtp_scheduler.PushFrame(Frame(62, 1, 4'016'666'667LL, 1));
    DUWN_ASSERT(rtp_scheduler.PopLatestValidFrame(out, superseded));
    DUWN_ASSERT(out.sequence_number == 62 && superseded == 1);
}

DUWN_TEST(Phase2_DeterministicProductionSchedulerMatrix) {
    using namespace duwn::video;
    using phase2_scheduler_tests::Frame;

    SchedulerConfig cfg{};
    cfg.mode = SchedulerMode::GameLowLatency;
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::SmoothLive);
    FrameScheduler scheduler(cfg, [](VideoFrame&) {});
    RtpTimestampMapper timeline;

    constexpr int64_t arrival_anchor = 10'000'000'000LL;
    constexpr uint32_t rtp_anchor = 90'000u;
    VideoFrame out{};
    uint64_t superseded = 0;

    // Stable 60 FPS delivered in 1 ms bursts still retains the 90 kHz source cadence.
    for (uint64_t i = 0; i < 60; ++i) {
        const int64_t pts = timeline.Map(rtp_anchor + static_cast<uint32_t>(i * 1500),
                                         arrival_anchor + static_cast<int64_t>(i) * 1'000'000LL);
        scheduler.PushFrame(Frame(i + 1, 1, pts));
        DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
        DUWN_ASSERT(out.sequence_number == i + 1 && superseded == 0);
        DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);
    }
    DUWN_ASSERT(std::abs(duwn::GlobalMetrics().source_nominal_fps.load() - 60.0) < 0.01);

    // VFR/static interval is an outlier for cadence estimation, not a fabricated frame.
    const int64_t static_pts = timeline.Map(rtp_anchor + 60 * 1500 + 180'000,
                                            arrival_anchor + 3'000'000'000LL);
    scheduler.PushFrame(Frame(61, 1, static_pts));
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
    DUWN_ASSERT(out.sequence_number == 61 && superseded == 0);

    // A three-frame fresh burst remains FIFO and bounded when there is display budget.
    for (uint64_t i = 62; i <= 64; ++i)
        scheduler.PushFrame(Frame(i, 1, static_pts + static_cast<int64_t>(i - 61) * 16'666'667LL));
    DUWN_ASSERT(scheduler.DecodedQueueSize() == 3);
    for (uint64_t expected = 62; expected <= 64; ++expected) {
        DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
        DUWN_ASSERT(out.sequence_number == expected && superseded == 0);
    }

    // Delayed output: a stale frame is superseded once a valid newer image exists.
    scheduler.PushFrame(Frame(65, 1, static_pts + 4 * 16'666'667LL, 25));
    scheduler.PushFrame(Frame(66, 1, static_pts + 5 * 16'666'667LL, 1));
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
    DUWN_ASSERT(out.sequence_number == 66 && superseded == 1);

    // A simulated GPU stall cannot grow the decoded queue beyond three frames.
    for (uint64_t i = 67; i < 667; ++i) {
        scheduler.PushFrame(Frame(i, 1, static_pts + static_cast<int64_t>(i) * 16'666'667LL, 100));
        DUWN_ASSERT(scheduler.DecodedQueueSize() <= 3);
    }
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
    DUWN_ASSERT(out.sequence_number == 666 && scheduler.DecodedQueueSize() == 0);

    // Rotation/generation transition purges old geometry; late old-generation output is rejected.
    auto resized = Frame(1, 2, static_pts + 12'000'000'000LL);
    resized.width = resized.visible_width = 1920;
    resized.height = resized.visible_height = 1328;
    scheduler.PushFrame(std::move(resized));
    scheduler.PushFrame(Frame(999, 1, static_pts + 12'016'666'667LL));
    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded) && out.format_generation == 2);
    DUWN_ASSERT(out.visible_width == 1920 && out.visible_height == 1328);
    scheduler.Flush();
    auto reconnected = Frame(1, 3, static_pts + 13'000'000'000LL);
    reconnected.width = reconnected.visible_width = 1920;
    reconnected.height = reconnected.visible_height = 896;
    scheduler.PushFrame(std::move(reconnected));
    scheduler.PushFrame(Frame(2, 2, static_pts + 13'016'666'667LL));
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded) && out.format_generation == 3);
    DUWN_ASSERT(out.visible_width == 1920 && out.visible_height == 896);
}

DUWN_TEST(Phase2_LongRunOneHourEquivalentRemainsBounded) {
    using namespace duwn::video;
    using phase2_scheduler_tests::Frame;

    SchedulerConfig cfg{};
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::SmoothLive);
    FrameScheduler scheduler(cfg, [](VideoFrame&) {});
    RtpTimestampMapper timeline;
    VideoFrame out{};
    uint64_t superseded = 0;
    constexpr uint64_t frames = 60 * 60 * 60;
    std::vector<int64_t> queue_age_ns;
    queue_age_ns.reserve(frames);
    const uint64_t dropped_before = duwn::GlobalMetrics().video_dropped_frames.load();

    const auto started = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < frames; ++i) {
        const int64_t pts = timeline.Map(static_cast<uint32_t>(i * 1500u),
                                         20'000'000'000LL + static_cast<int64_t>(i) * 500'000LL);
        scheduler.PushFrame(Frame(i + 1, 1, pts));
        DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);
        DUWN_ASSERT(scheduler.PopLatestValidFrame(out, superseded));
        DUWN_ASSERT(out.sequence_number == i + 1 && superseded == 0);
        queue_age_ns.push_back(
            (duwn::clock::MonotonicClock::NowQpcTicks() - out.queue_push_qpc) * 1'000'000'000LL /
            duwn::clock::MonotonicClock::Frequency());
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::sort(queue_age_ns.begin(), queue_age_ns.end());
    const auto percentile = [&](double p) {
        return queue_age_ns[static_cast<size_t>((queue_age_ns.size() - 1) * p)];
    };
    std::printf("[PHASE2] one-hour-equivalent=%llu frames elapsed=%.2fms ns_per_frame=%.1f "
                "queue_age_us_p50=%.2f p95=%.2f p99=%.2f\n",
                static_cast<unsigned long long>(frames), elapsed_ms,
                elapsed_ms * 1'000'000.0 / static_cast<double>(frames),
                percentile(0.50) / 1000.0, percentile(0.95) / 1000.0, percentile(0.99) / 1000.0);
    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);
    DUWN_ASSERT(duwn::GlobalMetrics().video_dropped_frames.load() == dropped_before);
}
