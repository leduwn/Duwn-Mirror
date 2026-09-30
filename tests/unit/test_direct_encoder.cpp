#include "direct/encoder/DirectVideoEncoder.h"
#include "MockVideoEncoder.h"
#include <vector>
#include <cmath>

using namespace duwn::direct;
using namespace duwn::direct::test;

// 1. DirectEncoderConfig Invariants
DUWN_TEST(DirectEncoder_ConfigInvariants) {
    DirectEncoderConfig cfg;
    DUWN_ASSERT(cfg.real_time == true);
    DUWN_ASSERT(cfg.allow_frame_reordering == false); // Strictly NO B-frames
    DUWN_ASSERT(cfg.enable_hardware_acceleration == true);
    DUWN_ASSERT(cfg.max_frame_delay_count == 0); // 0 lookahead delay
    DUWN_ASSERT(cfg.width == 1920);
    DUWN_ASSERT(cfg.height == 1080);
    DUWN_ASSERT(cfg.fps == 60);
    DUWN_ASSERT(cfg.bitrate_target_bps == 12'000'000);
    DUWN_ASSERT(cfg.bitrate_ceiling_bps == 18'000'000);
}

// 2. DirectEncoderInputSlot: 0 or 1 Latest-Frame Invariant
DUWN_TEST(DirectEncoder_InputSlotZeroOrOnePendingInvariant) {
    DirectEncoderInputSlot slot;
    DUWN_ASSERT(!slot.HasFrame());
    DUWN_ASSERT(slot.GetTotalInputFrames() == 0);
    DUWN_ASSERT(slot.GetSupersededCount() == 0);

    RawVideoFrameInput f1;
    f1.frame_sequence = 1;
    f1.source_timestamp_ns = 100'000'000;

    RawVideoFrameInput f2;
    f2.frame_sequence = 2;
    f2.source_timestamp_ns = 116'666'666;

    // Put f1
    slot.Put(std::move(f1));
    DUWN_ASSERT(slot.HasFrame());
    DUWN_ASSERT(slot.GetTotalInputFrames() == 1);
    DUWN_ASSERT(slot.GetSupersededCount() == 0);

    // Put f2 without taking f1 -> f1 is superseded
    slot.Put(std::move(f2));
    DUWN_ASSERT(slot.HasFrame());
    DUWN_ASSERT(slot.GetTotalInputFrames() == 2);
    DUWN_ASSERT(slot.GetSupersededCount() == 1);

    // Take must return f2 (freshest frame)
    RawVideoFrameInput extracted;
    DUWN_ASSERT(slot.Take(extracted));
    DUWN_ASSERT(extracted.frame_sequence == 2);
    DUWN_ASSERT(!slot.HasFrame());
}

// 3. No B-Frame Reordering Invariant
DUWN_TEST(DirectEncoder_NoBFrameReordering) {
    MockVideoEncoder encoder;
    DirectEncoderConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.fps = 60;
    DUWN_ASSERT(encoder.Initialize(cfg));

    std::vector<uint64_t> emitted_seqs;
    std::vector<uint64_t> emitted_pts;

    encoder.SetOutputCallback([&](EncodedVideoFrame&& frame) {
        emitted_seqs.push_back(frame.frame_id);
        emitted_pts.push_back(frame.source_timestamp_ns);
    });

    for (uint64_t seq = 1; seq <= 60; ++seq) {
        RawVideoFrameInput input;
        input.frame_sequence = seq;
        input.source_timestamp_ns = seq * 16'666'666ULL;
        encoder.EncodeImmediate(input);
    }

    DUWN_ASSERT(emitted_seqs.size() == 60);
    DUWN_ASSERT(emitted_pts.size() == 60);

    // Strict monotonically increasing sequence and PTS verification
    for (size_t i = 1; i < emitted_seqs.size(); ++i) {
        DUWN_ASSERT(emitted_seqs[i] == emitted_seqs[i - 1] + 1);
        DUWN_ASSERT(emitted_pts[i] > emitted_pts[i - 1]);
    }
}

// 4. Quality Rule: Preserves Requested Resolution
DUWN_TEST(DirectEncoder_PreservesRequestedResolutionWithoutCheating) {
    MockVideoEncoder encoder;
    DirectEncoderConfig cfg;
    cfg.width = 2560; // 2.5K capture resolution
    cfg.height = 1440;
    cfg.fps = 60;
    cfg.bitrate_target_bps = 20'000'000;
    DUWN_ASSERT(encoder.Initialize(cfg));

    uint32_t output_width = 0;
    uint32_t output_height = 0;

    encoder.SetOutputCallback([&](EncodedVideoFrame&& frame) {
        output_width = frame.width;
        output_height = frame.height;
    });

    RawVideoFrameInput input;
    input.frame_sequence = 1;
    input.width = 2560;
    input.height = 1440;
    input.source_timestamp_ns = 1'000'000'000ULL;
    encoder.EncodeImmediate(input);

    // Must never downscale to 1080p merely to fake lower latency
    DUWN_ASSERT(output_width == 2560);
    DUWN_ASSERT(output_height == 1440);
}

// 5. Dynamic Reconfiguration
DUWN_TEST(DirectEncoder_DynamicReconfiguration) {
    MockVideoEncoder encoder;
    DirectEncoderConfig cfg;
    cfg.bitrate_target_bps = 10'000'000;
    cfg.fps = 30;
    DUWN_ASSERT(encoder.Initialize(cfg));

    DUWN_ASSERT(encoder.Reconfigure(15'000'000, 60));
    auto active_cfg = encoder.GetConfig();
    DUWN_ASSERT(active_cfg.bitrate_target_bps == 15'000'000);
    DUWN_ASSERT(active_cfg.fps == 60);
}

// 6. Two-Minute 60 FPS Endurance Simulation (7,200 Frames)
DUWN_TEST(DirectEncoder_TwoMinute60FpsEnduranceSimulation) {
    MockVideoEncoder encoder;
    DirectEncoderConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.fps = 60;
    cfg.bitrate_target_bps = 12'000'000;
    cfg.bitrate_ceiling_bps = 18'000'000;
    cfg.keyframe_interval_seconds = 2.0f; // Keyframe every 120 frames
    DUWN_ASSERT(encoder.Initialize(cfg));

    const uint64_t kTotalFrames = 7200; // Exactly 2 minutes at 60 FPS
    uint64_t last_pts = 0;
    uint64_t frames_received = 0;

    encoder.SetOutputCallback([&](EncodedVideoFrame&& frame) {
        frames_received++;
        DUWN_ASSERT(frame.source_timestamp_ns > last_pts);
        last_pts = frame.source_timestamp_ns;
    });

    for (uint64_t seq = 1; seq <= kTotalFrames; ++seq) {
        RawVideoFrameInput input;
        input.frame_sequence = seq;
        input.source_timestamp_ns = seq * 16'666'666ULL;
        input.width = 1920;
        input.height = 1080;

        // Submit to 0/1 input slot
        encoder.SubmitFrame(std::move(input));

        // Invariant: At no point may the input slot hold > 1 frame
        DUWN_ASSERT(encoder.GetInputSlot().HasFrame());

        // Process frame from slot
        DUWN_ASSERT(encoder.ProcessOneSlotFrame());

        // Invariant: After processing, slot must be empty
        DUWN_ASSERT(!encoder.GetInputSlot().HasFrame());
    }

    DUWN_ASSERT(frames_received == kTotalFrames);

    auto metrics = encoder.GetMetrics();
    DUWN_ASSERT(metrics.encoded_frames_count == kTotalFrames);
    DUWN_ASSERT(metrics.superseded_input_frames == 0); // No drops when consumer matches 60fps
    DUWN_ASSERT(metrics.keyframe_count == (kTotalFrames / 120)); // Exactly 60 keyframes in 2 minutes
    DUWN_ASSERT(metrics.total_encoded_bytes > 0);

    // Verify encode latency bounds:
    // Simulated nominal encode is 3.5ms (keyframe 5.0ms)
    DUWN_ASSERT(metrics.p50_encode_latency_ms >= 3.0 && metrics.p50_encode_latency_ms <= 4.0);
    DUWN_ASSERT(metrics.p95_encode_latency_ms >= 3.0 && metrics.p95_encode_latency_ms <= 5.5);
    DUWN_ASSERT(metrics.p99_encode_latency_ms >= 3.0 && metrics.p99_encode_latency_ms <= 5.5);

    // Crucial: No progressive latency growth over the 2-minute run
    DUWN_ASSERT(metrics.max_encode_latency_ms <= 10.0);
}
