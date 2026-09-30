#include "video/FrameScheduler.h"

namespace streaming_scheduler_tests {
inline duwn::video::VideoFrame Frame(uint64_t sequence, int64_t age_ms = 0) {
    duwn::video::VideoFrame frame{};
    frame.sequence_number = sequence;
    frame.format_generation = 1;
    frame.width = frame.visible_width = 2560;
    frame.height = frame.visible_height = 1184;
    frame.pts_ns = 1'000'000'000LL + static_cast<int64_t>(sequence) * 16'666'667LL;
    frame.queue_push_qpc = duwn::clock::MonotonicClock::NowQpcTicks()
        - duwn::clock::MonotonicClock::Frequency() * age_ms / 1000;
    return frame;
}
}

DUWN_TEST(StreamingScheduler_FastestBoundsBurstAndPreservesGeometry) {
    duwn::video::SchedulerConfig cfg{};
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::LowLatency);
    duwn::video::FrameScheduler scheduler(cfg, [](duwn::video::VideoFrame&) {});
    const auto before = duwn::GlobalMetrics().video_dropped_frames.load();
    for (uint64_t i = 1; i <= 1000; ++i) {
        scheduler.PushFrame(streaming_scheduler_tests::Frame(i));
        DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);
    }
    duwn::video::VideoFrame out{};
    uint64_t skipped = 0;
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, skipped));
    DUWN_ASSERT(out.sequence_number == 1000);
    DUWN_ASSERT(out.visible_width == 2560 && out.visible_height == 1184);
    DUWN_ASSERT(duwn::GlobalMetrics().video_dropped_frames.load() - before == 999);
    DUWN_ASSERT(scheduler.DecodedQueueSize() == 0);
}

DUWN_TEST(StreamingScheduler_LiveFastestChangeCatchesUpWithoutNewInput) {
    duwn::video::FrameScheduler scheduler({}, [](duwn::video::VideoFrame&) {});
    for (uint64_t i = 1; i <= 3; ++i) scheduler.PushFrame(streaming_scheduler_tests::Frame(i));
    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::LowLatency));
    duwn::video::VideoFrame out{};
    uint64_t skipped = 0;
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, skipped));
    DUWN_ASSERT(out.sequence_number == 3);
    DUWN_ASSERT(skipped == 2 && scheduler.DecodedQueueSize() == 0);
}

DUWN_TEST(StreamingScheduler_LiveQueueShrinkDropsAllExcessFrames) {
    duwn::video::SchedulerConfig cfg{};
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 100, 3);
    duwn::video::FrameScheduler scheduler(cfg, [](duwn::video::VideoFrame&) {});
    for (uint64_t i = 1; i <= 3; ++i) scheduler.PushFrame(streaming_scheduler_tests::Frame(i));
    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 100, 1));
    const auto before = duwn::GlobalMetrics().video_dropped_frames.load();
    scheduler.PushFrame(streaming_scheduler_tests::Frame(4));
    DUWN_ASSERT(scheduler.DecodedQueueSize() == 1);
    DUWN_ASSERT(duwn::GlobalMetrics().video_dropped_frames.load() - before == 3);
    duwn::video::VideoFrame out{};
    DUWN_ASSERT(scheduler.PopDecodedFrameForTest(out));
    DUWN_ASSERT(out.sequence_number == 4);
}

DUWN_TEST(StreamingScheduler_CustomAgeBudgetControlsCatchup) {
    duwn::video::SchedulerConfig cfg{};
    cfg.streaming_policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 25, 3);
    duwn::video::FrameScheduler scheduler(cfg, [](duwn::video::VideoFrame&) {});
    scheduler.PushFrame(streaming_scheduler_tests::Frame(1, 40));
    scheduler.PushFrame(streaming_scheduler_tests::Frame(2));
    duwn::video::VideoFrame out{};
    uint64_t skipped = 0;
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, skipped));
    DUWN_ASSERT(out.sequence_number == 2 && skipped == 1);

    scheduler.SetStreamingPolicy(duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 100, 3));
    scheduler.PushFrame(streaming_scheduler_tests::Frame(3, 40));
    scheduler.PushFrame(streaming_scheduler_tests::Frame(4));
    DUWN_ASSERT(scheduler.PopLatestValidFrame(out, skipped));
    DUWN_ASSERT(out.sequence_number == 3 && skipped == 0);
}
