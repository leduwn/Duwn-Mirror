#include "common/telemetry/LatencyTelemetry.h"
#include "video/DecoderInputTiming.h"
#include "video/HevcRtpAssembler.h"

DUWN_TEST(DecoderTiming_DelayedReorderedAndUnknownOutput) {
    duwn::video::DecoderTimingTracker tracker;
    tracker.Push({100, 10, 20, 30});
    tracker.Push({200, 40, 50, 60});
    auto first = tracker.Take(100);
    DUWN_ASSERT(first.rtp_arrival_qpc == 10 && first.process_input_qpc == 30);
    DUWN_ASSERT(tracker.Take(100).process_input_qpc == 0);
    DUWN_ASSERT(tracker.Take(999).process_input_qpc == 0);
    DUWN_ASSERT(tracker.Take(200).au_received_qpc == 50);
    tracker.Push({300, 70, 80, 90});
    tracker.Push({400, 100, 110, 120});
    DUWN_ASSERT(tracker.Take(400).process_input_qpc == 120);
    DUWN_ASSERT(tracker.Take(300).process_input_qpc == 90);
}

DUWN_TEST(DecoderTiming_AmbiguousBoundedAndSessionReset) {
    duwn::video::DecoderTimingTracker tracker;
    tracker.Push({100, 10, 20, 30});
    tracker.Push({100, 40, 50, 60});
    DUWN_ASSERT(tracker.Take(100).process_input_qpc == 0);
    for (int64_t i = 1; i <= 65; ++i) tracker.Push({i, i, i, i});
    DUWN_ASSERT(tracker.Take(1).process_input_qpc == 0);
    DUWN_ASSERT(tracker.Take(65).process_input_qpc == 65);
    tracker.Reset();
    DUWN_ASSERT(tracker.Take(64).process_input_qpc == 0);
}

DUWN_TEST(FrameTiming_IntervalsPercentilesAndGaps) {
    using namespace duwn::telemetry;
    auto& timing = LatencyTelemetry::Get();
    timing.SetFrameTimingEnabled(true);
    const int64_t ms = duwn::clock::MonotonicClock::Frequency() / 1000;
    int64_t qpc = duwn::clock::MonotonicClock::NowQpcTicks();
    timing.RecordFrameInterval(FrameTimingMetric::AuComplete, qpc);
    for (int64_t gap : {16, 26, 34, 51}) {
        qpc += gap * ms;
        timing.RecordFrameInterval(FrameTimingMetric::AuComplete, qpc);
    }
    const auto stats = timing.CollectFrameTiming()[static_cast<size_t>(FrameTimingMetric::AuComplete)];
    DUWN_ASSERT(stats.count == 4 && stats.retained == 4);
    DUWN_ASSERT(std::abs(stats.p50 - 34) < 0.01);
    DUWN_ASSERT(std::abs(stats.p95 - 51) < 0.01 && std::abs(stats.p99 - 51) < 0.01);
    DUWN_ASSERT(std::abs(stats.mean - 31.75) < 0.01);
    DUWN_ASSERT(stats.gaps25 == 3 && stats.gaps33 == 2 && stats.gaps50 == 1);
    DUWN_ASSERT(stats.over1 == 4);
    qpc += 16 * ms;
    timing.RecordFrameInterval(FrameTimingMetric::AuComplete, qpc);
    DUWN_ASSERT(timing.CollectFrameTiming()[1].count == 1); // Windows keep the last event anchor.
    timing.SetFrameTimingEnabled(false);
}

DUWN_TEST(FrameTiming_Phase1BMetricsAreBoundedAndOptIn) {
    using namespace duwn::telemetry;
    auto& timing = LatencyTelemetry::Get();
    timing.SetFrameTimingEnabled(true);
    const int64_t ms = duwn::clock::MonotonicClock::Frequency() / 1000;
    const int64_t base = duwn::clock::MonotonicClock::NowQpcTicks();
    timing.RecordFrameInterval(FrameTimingMetric::SocketRead, base);
    timing.RecordFrameInterval(FrameTimingMetric::SocketRead, base + 2 * ms);
    timing.RecordFrameDuration(FrameTimingMetric::ReceiverCallback, base, base + ms);
    timing.RecordFrameDuration(FrameTimingMetric::DecoderMutexWait, base, base + 2 * ms);
    timing.RecordFrameDuration(FrameTimingMetric::DecoderMutexHold, base, base + 3 * ms);
    timing.RecordFrameDuration(FrameTimingMetric::ProcessInput, base, base + 4 * ms);
    timing.RecordFrameDuration(FrameTimingMetric::ProcessOutput, base, base + 5 * ms);
    const auto stats = timing.CollectFrameTiming();
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::SocketRead)].count == 1);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::ReceiverCallback)].count == 1);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::DecoderMutexWait)].over1 == 1);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::DecoderMutexHold)].count == 1);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::ProcessInput)].count == 1);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::ProcessOutput)].count == 1);
    timing.SetFrameTimingEnabled(false);
}

DUWN_TEST(FrameTiming_InvalidClockMissingStagesBoundedAndDisabled) {
    using namespace duwn::telemetry;
    auto& timing = LatencyTelemetry::Get();
    timing.SetFrameTimingEnabled(true);
    const int64_t ms = duwn::clock::MonotonicClock::Frequency() / 1000;
    const int64_t base = duwn::clock::MonotonicClock::NowQpcTicks();
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, base + ms * 100);
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, base + ms * 90);
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, 0);
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, base + ms * 106);
    timing.RecordFrameDuration(FrameTimingMetric::ReceiveToReturn, 0, ms * 100);
    timing.RecordFrameDuration(FrameTimingMetric::ReceiveToReturn, ms * 100, ms * 90);
    auto stats = timing.CollectFrameTiming();
    DUWN_ASSERT(stats[0].count == 1 && std::abs(stats[0].p50 - 16) < 0.01);
    DUWN_ASSERT(stats[static_cast<size_t>(FrameTimingMetric::ReceiveToReturn)].count == 0);
    for (int64_t i = 1; i <= 300; ++i)
        timing.RecordFrameDuration(FrameTimingMetric::QueueDwell, base, base + ms * i);
    stats = timing.CollectFrameTiming();
    const auto& dwell = stats[static_cast<size_t>(FrameTimingMetric::QueueDwell)];
    DUWN_ASSERT(dwell.count == 300 && dwell.retained == 256);
    DUWN_ASSERT(std::abs(dwell.mean - 150.5) < 0.01 && std::abs(dwell.max - 300) < 0.01);
    timing.Reset();
    timing.RecordFrameDuration(FrameTimingMetric::QueueDwell, base, base + ms);
    DUWN_ASSERT(timing.CollectFrameTiming()[static_cast<size_t>(FrameTimingMetric::QueueDwell)].count == 0);
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, duwn::clock::MonotonicClock::NowQpcTicks());
    DUWN_ASSERT(timing.CollectFrameTiming()[0].count == 0);
    timing.SetFrameTimingEnabled(false);
    timing.RecordFrameInterval(FrameTimingMetric::RtpIngest, ms * 1016);
    timing.RecordFrameDuration(FrameTimingMetric::QueueDwell, ms, ms * 2);
    stats = timing.CollectFrameTiming();
    for (const auto& metric : stats) DUWN_ASSERT(metric.count == 0);
}

DUWN_TEST(FrameTiming_HevcTimestampBoundaryAndFlush) {
    auto& timing = duwn::telemetry::LatencyTelemetry::Get();
    timing.SetFrameTimingEnabled(true);
    std::vector<duwn::video::EncodedAccessUnit> units;
    duwn::video::HevcRtpAssembler assembler([&](auto au) { units.push_back(std::move(au)); });
    const uint8_t nal[] = {2, 1, 0x80};
    const auto first_arrival = duwn::clock::MonotonicClock::NowQpcTicks();
    assembler.FeedRtp(nal, sizeof(nal), 100, 1000, false, 1, first_arrival);
    const auto second_arrival = duwn::clock::MonotonicClock::NowQpcTicks();
    assembler.FeedRtp(nal, sizeof(nal), 200, 2000, true, 2, second_arrival);
    DUWN_ASSERT(units.size() == 2);
    DUWN_ASSERT(units[0].rtp_arrival_qpc > 0 && units[1].rtp_arrival_qpc > 0);
    DUWN_ASSERT(units[0].rtp_arrival_qpc == first_arrival && units[1].rtp_arrival_qpc == second_arrival);
    DUWN_ASSERT(units[1].rtp_arrival_qpc >= units[0].rtp_arrival_qpc);
    DUWN_ASSERT(units[1].rtp_arrival_qpc <= units[0].au_received_qpc);
    assembler.FeedRtp(nal, sizeof(nal), 200, 2000, true, 3);
    DUWN_ASSERT(timing.CollectFrameTiming()[0].count == 1); // One arrival per RTP timestamp, not per NAL/marker.
    assembler.FeedRtp(nal, sizeof(nal), 300, 3000, false, 3);
    assembler.Flush();
    const auto after_flush = duwn::clock::MonotonicClock::NowQpcTicks();
    assembler.FeedRtp(nal, sizeof(nal), 400, 4000, true, 4);
    DUWN_ASSERT(units.back().rtp_arrival_qpc >= after_flush);
    timing.SetFrameTimingEnabled(false);
}
