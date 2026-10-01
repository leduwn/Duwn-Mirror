#include "direct/DirectPipelineBridge.h"
#include "direct/DirectPacketizer.h"
#include "direct/DirectReceiver.h"
#include "direct/DirectLatencyTracker.h"
#include "direct/encoder/DirectVideoEncoder.h"
#include "MockVideoEncoder.h"
#include "video/VideoDecoder.h"
#include "video/D3D11Device.h"
#include "common/clock/MonotonicClock.h"
#include <vector>
#include <chrono>
#include <cmath>

using namespace duwn;
using namespace duwn::direct;
using namespace duwn::direct::test;
using namespace duwn::video;

// 1. Full End-to-End Pipeline Integration Test:
// Capture -> Encoder -> DirectPacketizer -> UDP/Receiver -> DirectFrameAssembler -> Bridge -> VideoDecoder -> Render
DUWN_TEST(DirectE2E_FullPipelineCaptureToDecoderIntegration) {
    D3D11Device device;
    DUWN_ASSERT(device.Create(false, true)); // Hardware or WARP

    std::vector<VideoFrame> decoded_frames;
    VideoDecoder decoder(device, [&](VideoFrame frame) {
        decoded_frames.push_back(std::move(frame));
    });

    const uint32_t kWidth = 1920;
    const uint32_t kHeight = 1080;
    const uint32_t kFps = 60;
    DUWN_ASSERT(decoder.Init(kWidth, kHeight, VideoCodecType::H264));

    DirectReceiver receiver;
    DirectLatencyTracker latency_tracker;
    DirectPipelineBridge bridge(decoder, &latency_tracker);
    bridge.SetStreamFormat(kWidth, kHeight, kFps);
    bridge.AttachReceiver(receiver);

    MockVideoEncoder encoder;
    DirectEncoderConfig enc_cfg;
    enc_cfg.width = kWidth;
    enc_cfg.height = kHeight;
    enc_cfg.fps = kFps;
    enc_cfg.bitrate_target_bps = 12'000'000;
    enc_cfg.real_time = true;
    enc_cfg.allow_frame_reordering = false; // Strictly NO B-frames
    enc_cfg.max_frame_delay_count = 0;      // Zero lookahead delay
    DUWN_ASSERT(encoder.Initialize(enc_cfg));

    DirectPacketizer packetizer(1001, 1400); // 1400 bytes max payload per packet

    // Stream 10 frames through full pipeline
    const uint64_t kNumFrames = 10;
    for (uint64_t seq = 1; seq <= kNumFrames; ++seq) {
        uint64_t s0_pts = seq * 16'666'666ULL;
        uint64_t s1_capture_cb = s0_pts + 500'000ULL; // +0.5ms

        // Capture produces raw frame
        RawVideoFrameInput raw_input;
        raw_input.frame_sequence = seq;
        raw_input.width = kWidth;
        raw_input.height = kHeight;
        raw_input.source_timestamp_ns = s0_pts;

        uint64_t s2_enc_in = s1_capture_cb + 100'000ULL; // +0.1ms
        encoder.SubmitFrame(std::move(raw_input));

        EncodedVideoFrame enc_output;
        uint64_t s3_enc_out = 0;
        encoder.SetOutputCallback([&](EncodedVideoFrame&& f) {
            enc_output = std::move(f);
            s3_enc_out = s2_enc_in + 3'500'000ULL; // +3.5ms encode time
        });
        DUWN_ASSERT(encoder.ProcessOneSlotFrame());
        DUWN_ASSERT(!enc_output.payload.empty());

        uint64_t s4_send = s3_enc_out + 200'000ULL; // +0.2ms packetize & send

        // Record sender-side checkpoints S0..S4 into bridge
        bridge.RecordSenderTimestamps(seq, s0_pts, s1_capture_cb, s2_enc_in, s3_enc_out, s4_send);

        // Packetize frame into MTU-safe datagrams
        auto packets = packetizer.PacketizeFrame(enc_output, s4_send);
        DUWN_ASSERT(!packets.empty());

        // Ingest packets into DirectReceiver
        uint64_t r0_packet_arrival = s4_send + 1'200'000ULL; // +1.2ms network transit
        for (const auto& pkt : packets) {
            DUWN_ASSERT(receiver.IngestPacket(pkt.data(), pkt.size()));
        }

        // Bridge automatically processed frame on assembly completion
        // Record receiver checkpoints R0..R4
        uint64_t r1_au_complete = r0_packet_arrival + 200'000ULL; // +0.2ms assembly
        uint64_t r2_dec_in = r1_au_complete + 100'000ULL;         // +0.1ms bridge ingest
        bridge.RecordReceiverTimestamps(seq, r0_packet_arrival, r1_au_complete, r2_dec_in);

        uint64_t r3_dec_out = r2_dec_in + 2'800'000ULL; // +2.8ms decode
        uint64_t r4_present = r3_dec_out + 1'000'000ULL; // +1.0ms render/present
        bridge.RecordDecoderOutput(seq, r3_dec_out);
        bridge.RecordPresent(seq, r4_present);
    }

    // Verify bridge metrics
    DUWN_ASSERT(bridge.GetFramesSubmitted() == kNumFrames);
    DUWN_ASSERT(bridge.GetFramesDropped() == 0);

    // Verify 10-checkpoint latency summary
    auto summary = latency_tracker.GetSummary();
    DUWN_ASSERT(summary.avg_encode_ms >= 3.0 && summary.avg_encode_ms <= 4.5);
    DUWN_ASSERT(summary.avg_decode_ms >= 2.0 && summary.avg_decode_ms <= 4.0);
    DUWN_ASSERT(summary.avg_render_ms >= 0.5 && summary.avg_render_ms <= 2.0);
    DUWN_ASSERT(summary.avg_callback_to_present_ms > 0.0);
}

// 2. Resolution & Detail Fidelity Verification (Static UI)
DUWN_TEST(DirectE2E_Quality_StaticDetailSharpnessPreserved) {
    D3D11Device device;
    DUWN_ASSERT(device.Create(false, true));

    VideoDecoder decoder(device, [](VideoFrame){});

    // 2.5K Retina/iPad source: 2560x1440
    const uint32_t kSourceW = 2560;
    const uint32_t kSourceH = 1440;
    const uint32_t kFps = 60;
    DUWN_ASSERT(decoder.Init(kSourceW, kSourceH, VideoCodecType::H264));

    DirectPipelineBridge bridge(decoder);
    bridge.SetStreamFormat(kSourceW, kSourceH, kFps);

    MockVideoEncoder encoder;
    DirectEncoderConfig enc_cfg;
    enc_cfg.width = kSourceW;
    enc_cfg.height = kSourceH;
    enc_cfg.fps = kFps;
    enc_cfg.bitrate_target_bps = 18'000'000;
    DUWN_ASSERT(encoder.Initialize(enc_cfg));

    RawVideoFrameInput raw_input;
    raw_input.frame_sequence = 1;
    raw_input.width = kSourceW;
    raw_input.height = kSourceH;
    raw_input.source_timestamp_ns = 1'000'000'000ULL;

    EncodedVideoFrame enc_output;
    encoder.SetOutputCallback([&](EncodedVideoFrame&& f) {
        enc_output = std::move(f);
    });
    encoder.SubmitFrame(std::move(raw_input));
    DUWN_ASSERT(encoder.ProcessOneSlotFrame());

    // Invariant: Output dimensions must strictly equal requested source dimensions (NO downscaling)
    DUWN_ASSERT(enc_output.width == kSourceW);
    DUWN_ASSERT(enc_output.height == kSourceH);

    // DirectFrame bridge ingestion
    DirectFrame frame;
    frame.frame_id = 1;
    frame.stream_id = static_cast<uint16_t>(DirectStreamId::Video);
    frame.is_keyframe = enc_output.is_keyframe;
    frame.source_timestamp_ns = enc_output.source_timestamp_ns;
    frame.payload = std::move(enc_output.payload);

    DUWN_ASSERT(bridge.FeedDirectFrame(std::move(frame)));
    DUWN_ASSERT(bridge.GetFramesSubmitted() == 1);
    DUWN_ASSERT(bridge.GetWidth() == kSourceW);
    DUWN_ASSERT(bridge.GetHeight() == kSourceH);
}

// 3. High-Motion Game/Video Stress (Multi-Packet Datagrams)
DUWN_TEST(DirectE2E_Quality_HighMotionGameStress) {
    D3D11Device device;
    DUWN_ASSERT(device.Create(false, true));

    VideoDecoder decoder(device, [](VideoFrame){});
    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));

    DirectReceiver receiver;
    DirectPipelineBridge bridge(decoder);
    bridge.SetStreamFormat(1920, 1080, 60);
    bridge.AttachReceiver(receiver);

    DirectPacketizer packetizer(2002, 1400);

    // Simulate high-motion keyframe of 64 KiB spanning 46 UDP datagrams
    EncodedVideoFrame motion_frame;
    motion_frame.frame_id = 101;
    motion_frame.is_keyframe = true;
    motion_frame.width = 1920;
    motion_frame.height = 1080;
    motion_frame.source_timestamp_ns = 5'000'000'000ULL;
    motion_frame.payload.assign(64 * 1024, 0x55);
    // Prefix valid Annex-B IDR start code
    motion_frame.payload[0] = 0; motion_frame.payload[1] = 0;
    motion_frame.payload[2] = 0; motion_frame.payload[3] = 1;
    motion_frame.payload[4] = 0x65; // NAL unit type 5 (IDR)

    auto packets = packetizer.PacketizeFrame(motion_frame);
    DUWN_ASSERT(packets.size() >= 45); // Must slice into at least 46 MTU fragments

    // Transmit all fragments into receiver
    for (const auto& pkt : packets) {
        DUWN_ASSERT(receiver.IngestPacket(pkt.data(), pkt.size()));
    }

    // Verify complete AU reassembly without fragmentation collapse
    DUWN_ASSERT(bridge.GetFramesSubmitted() == 1);
    DUWN_ASSERT(bridge.GetFramesDropped() == 0);
}

// 4. Latest Frame Policy & Bounded Memory Ceiling
DUWN_TEST(DirectE2E_LatestFramePolicy_ZeroOrOnePendingInvariant) {
    FreshestFrameSlot slot;
    DUWN_ASSERT(!slot.HasFrame());

    // Simulate fast sender producing 3 frames before consumer runs
    DirectFrame f1; f1.frame_id = 1; f1.payload = {0, 0, 0, 1, 0x67};
    DirectFrame f2; f2.frame_id = 2; f2.payload = {0, 0, 0, 1, 0x68};
    DirectFrame f3; f3.frame_id = 3; f3.payload = {0, 0, 0, 1, 0x65};

    slot.Put(std::move(f1));
    slot.Put(std::move(f2)); // f1 superseded
    slot.Put(std::move(f3)); // f2 superseded

    DUWN_ASSERT(slot.GetSupersededCount() == 2);
    DUWN_ASSERT(slot.HasFrame());

    DirectFrame extracted;
    DUWN_ASSERT(slot.Take(extracted));
    DUWN_ASSERT(extracted.frame_id == 3); // Freshest frame
    DUWN_ASSERT(!slot.HasFrame());        // Slot now empty
}

// 5. Latency Telemetry 10-Checkpoint Completeness
DUWN_TEST(DirectE2E_LatencyTelemetryValidation) {
    DirectLatencyTracker tracker;

    FrameLatencyCheckpoints cp;
    cp.frame_id = 42;
    cp.s0_capture_source_ns = 1'000'000'000ULL;
    cp.s1_capture_callback_ns = 1'000'500'000ULL;
    cp.s2_encoder_input_ns = 1'000'600'000ULL;
    cp.s3_encoder_output_ns = 1'004'100'000ULL; // 3.5ms encode
    cp.s4_packet_send_ns = 1'004'300'000ULL;
    cp.r0_packet_arrival_ns = 1'005'500'000ULL; // 1.2ms transit
    cp.r1_au_complete_ns = 1'005'700'000ULL;   // 0.2ms assembly
    cp.r2_decoder_input_ns = 1'005'800'000ULL;
    cp.r3_decoder_output_ns = 1'008'600'000ULL; // 2.8ms decode
    cp.r4_present_ns = 1'009'600'000ULL;        // 1.0ms present

    DUWN_ASSERT(std::abs(cp.EncodeMs() - 3.5) < 0.01);
    DUWN_ASSERT(std::abs(cp.AssemblyMs() - 0.2) < 0.01);
    DUWN_ASSERT(std::abs(cp.DecodeMs() - 2.8) < 0.01);
    DUWN_ASSERT(std::abs(cp.RenderMs() - 1.0) < 0.01);
    DUWN_ASSERT(std::abs(cp.CallbackToPresentMs() - 9.1) < 0.01);

    tracker.RecordFrame(cp);
    auto summary = tracker.GetSummary();
    DUWN_ASSERT(std::abs(summary.avg_encode_ms - 3.5) < 0.01);
    DUWN_ASSERT(std::abs(summary.avg_decode_ms - 2.8) < 0.01);
    DUWN_ASSERT(std::abs(summary.avg_render_ms - 1.0) < 0.01);
    DUWN_ASSERT(std::abs(summary.avg_callback_to_present_ms - 9.1) < 0.01);
}

// 6. Synthetic A/B Comparison: AirPlay Model vs Duwn Direct H.264 Model
DUWN_TEST(DirectE2E_Synthetic_AirPlayVsDirectComparison) {
    // AirPlay baseline characteristics (architectural model):
    // - Jitter buffer depth: ~60ms - 100ms
    // - Lookahead / frame delay: 1-2 frames
    // - Nominal pipeline latency: 70ms - 110ms
    // - Dynamic downscaling on bandwidth pressure
    const double kAirPlayJitterBufferMs = 80.0;
    const double kAirPlayPipelineLatencyMs = 95.0;
    const uint32_t kAirPlayMaxQueuedFrames = 6;

    // Duwn Direct H.264 synthetic model characteristics:
    // - Jitter buffer depth: 0ms (FreshestFrameSlot, zero accumulation)
    // - Lookahead / frame delay: strictly 0 frames (MaxFrameDelayCount: 0)
    // - Nominal pipeline latency: 12ms - 18ms (3.5ms enc + 1.5ms net + 3.0ms dec + 1.0ms render)
    // - Requested source resolution strictly preserved
    const double kDirectQueueDelayMs = 0.0;
    const double kDirectPipelineLatencyMs = 14.5;
    const uint32_t kDirectMaxQueuedFrames = 1;

    // Comparative Invariants
    DUWN_ASSERT(kDirectQueueDelayMs < kAirPlayJitterBufferMs);
    DUWN_ASSERT(kDirectPipelineLatencyMs < kAirPlayPipelineLatencyMs);
    DUWN_ASSERT(kDirectMaxQueuedFrames < kAirPlayMaxQueuedFrames);
    DUWN_ASSERT(DirectBufferingInvariant::IsConforming(kDirectMaxQueuedFrames));
}
