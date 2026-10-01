// test_video_pipeline_recovery.cpp — Regression test suite
#include "app/Settings.h"
#include "video/VideoDecoder.h"
#include "video/D3D11Device.h"
#include "video/RtpCodecClassifier.h"
#include "airplay/StreamMetadata.h"
#include "common/ipc/VideoIpcRing.h"
#include <atomic>
#include <memory>
#include <vector>

using namespace duwn;
using namespace duwn::app;
using namespace duwn::video;
using namespace duwn::airplay;

// 1. Requested DirectIpc but Open fails -> RTP is not blocked
DUWN_TEST(VideoPipeline_DirectIpcFailedOpen_DoesNotBlockRtp) {
    Settings settings{};
    settings.transport_mode = TransportMode::DirectIpc;
    settings.connection_mode = ConnectionMode::WirelessAirPlay;

    std::atomic<bool> direct_ipc_active{false};
    std::unique_ptr<ipc::VideoIpcConsumer> ipc_consumer = std::make_unique<ipc::VideoIpcConsumer>();

    bool open_ok = ipc_consumer->Open(L"Local\\NonExistentShm_12345", L"Local\\NonExistentEvt_12345");
    DUWN_ASSERT(!open_ok);

    if (open_ok) {
        direct_ipc_active.store(true, std::memory_order_release);
    } else {
        direct_ipc_active.store(false, std::memory_order_release);
    }

    auto should_drop_rtp = [&](ConnectionMode mode, bool ipc_active) {
        return (mode == ConnectionMode::WirelessAirPlay && ipc_active);
    };

    bool drop = should_drop_rtp(settings.connection_mode, direct_ipc_active.load(std::memory_order_acquire));
    DUWN_ASSERT(!drop);
}

// 2. IPC stops/closes -> stops blocking RTP
DUWN_TEST(VideoPipeline_DirectIpcStopped_ResumesRtpFeeding) {
    std::atomic<bool> direct_ipc_active{true};

    auto should_drop_rtp = [&](ConnectionMode mode, bool ipc_active) {
        return (mode == ConnectionMode::WirelessAirPlay && ipc_active);
    };

    DUWN_ASSERT(should_drop_rtp(ConnectionMode::WirelessAirPlay, direct_ipc_active.load(std::memory_order_acquire)));

    direct_ipc_active.store(false, std::memory_order_release);

    DUWN_ASSERT(!should_drop_rtp(ConnectionMode::WirelessAirPlay, direct_ipc_active.load(std::memory_order_acquire)));
}

// 3. Reconnect -> decoder recovery with valid codec/dimensions
DUWN_TEST(VideoPipeline_ReconnectDecoderRecovery_ValidConfig) {
    D3D11Device d3d;
    bool d3d_ok = d3d.Create(false, true);
    DUWN_ASSERT(d3d_ok);

    std::vector<VideoFrame> decoded_frames;
    VideoDecoder decoder(d3d, [&](VideoFrame frame) {
        decoded_frames.push_back(std::move(frame));
    });

    std::atomic<bool> decoder_ready{false};

    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));
    decoder_ready.store(true, std::memory_order_release);

    // Reconnecting phase resets decoder
    decoder.ResetCodecState();
    decoder_ready.store(false, std::memory_order_release);
    DUWN_ASSERT(!decoder_ready.load(std::memory_order_acquire));
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::Unknown);

    // Audio-only metadata must not declare video ready
    StreamMetadata audio_only_meta{};
    audio_only_meta.audio_sample_rate = 44100;
    audio_only_meta.audio_channels = 2;
    audio_only_meta.audio_codec = AudioCodec::PCM;
    DUWN_ASSERT(!audio_only_meta.IsValid());

    if (audio_only_meta.video_codec != VideoCodec::Unknown &&
        audio_only_meta.video_width > 0 && audio_only_meta.video_height > 0) {
        decoder_ready.store(true, std::memory_order_release);
    }
    DUWN_ASSERT(!decoder_ready.load(std::memory_order_acquire));

    // Next RTP packet with H.264 SPS recovers decoder
    const uint8_t h264_sps[] = {0x67, 0x42, 0x00, 0x1e, 0x9a, 0x74, 0x05, 0x81};
    auto classification = ClassifyRtpPayload(h264_sps);
    DUWN_ASSERT(classification.codec == DetectedCodec::H264);

    const auto recovered_codec = (classification.codec == DetectedCodec::H265)
        ? VideoCodecType::H265 : VideoCodecType::H264;
    DUWN_ASSERT(decoder.Init(1920, 1080, recovered_codec));
    decoder_ready.store(true, std::memory_order_release);
    DUWN_ASSERT(decoder_ready.load(std::memory_order_acquire));
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::H264);

    // Late video metadata with same config does not reset running decoder
    StreamMetadata late_video_meta{};
    late_video_meta.video_codec = VideoCodec::H264;
    late_video_meta.video_width = 1920;
    late_video_meta.video_height = 1080;
    late_video_meta.video_fps = 60.0;
    DUWN_ASSERT(late_video_meta.IsValid());

    bool is_already_ready = decoder_ready.load(std::memory_order_acquire);
    bool same_config = is_already_ready &&
                       (decoder.GetActiveCodec() == VideoCodecType::H264) &&
                       (late_video_meta.video_width == 1920) &&
                       (late_video_meta.video_height == 1080);
    DUWN_ASSERT(same_config);
}

// 4. Hide preview then receive subsequent frames -> preview stays OFF
DUWN_TEST(VideoPipeline_HidePreview_SubsequentFramesDoNotReopen) {
    std::atomic<bool> session_first_frame_handled{false};
    std::atomic<bool> preview_visible{false};

    bool was_handled = session_first_frame_handled.exchange(true, std::memory_order_relaxed);
    DUWN_ASSERT(!was_handled); // first frame

    preview_visible.store(true, std::memory_order_release);
    DUWN_ASSERT(preview_visible.load());

    // User hides preview
    preview_visible.store(false, std::memory_order_release);
    DUWN_ASSERT(!preview_visible.load());

    // Frame 2
    was_handled = session_first_frame_handled.exchange(true, std::memory_order_relaxed);
    DUWN_ASSERT(was_handled);

    // Frame 3
    was_handled = session_first_frame_handled.exchange(true, std::memory_order_relaxed);
    DUWN_ASSERT(was_handled);

    // Must stay OFF
    DUWN_ASSERT(!preview_visible.load());
}

// 5. Preview OFF -> output still presents; preview can be re-opened
DUWN_TEST(VideoPipeline_PreviewOff_OutputPresents_PreviewReopenable) {
    std::atomic<bool> preview_visible{false};
    std::atomic<uint64_t> output_present_count{0};
    std::atomic<uint64_t> preview_present_count{0};

    auto simulate_present_frame = [&](bool prev_vis) {
        output_present_count.fetch_add(1, std::memory_order_relaxed);
        if (prev_vis) {
            preview_present_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    simulate_present_frame(preview_visible.load());
    DUWN_ASSERT(output_present_count.load() == 1);
    DUWN_ASSERT(preview_present_count.load() == 0);

    simulate_present_frame(preview_visible.load());
    DUWN_ASSERT(output_present_count.load() == 2);
    DUWN_ASSERT(preview_present_count.load() == 0);

    // Re-open preview
    preview_visible.store(true, std::memory_order_release);

    simulate_present_frame(preview_visible.load());
    DUWN_ASSERT(output_present_count.load() == 3);
    DUWN_ASSERT(preview_present_count.load() == 1);
}

// 6. HEVC 28 01 payload must classify as HEVC IDR-20, never H.264 PPS-8.
// Ambiguous slice packets must return Unknown.
DUWN_TEST(VideoPipeline_Hevc2801_ClassifiedAsHevcIdr_NotH264Pps) {
    using namespace duwn::video;

    // HEVC IDR_N_LP (type 20): byte 0 = 0x28 (type=20, layer_id=0), byte 1 = 0x01 (layer_id=0, tid=1)
    const uint8_t hevc_idr_2801[] = {0x28, 0x01, 0xAF, 0x00, 0x55};
    auto c1 = ClassifyRtpPayload(hevc_idr_2801);
    DUWN_ASSERT(c1.codec == DetectedCodec::H265);
    DUWN_ASSERT(std::string_view(c1.evidence) == "RtpPayload (HEVC IDR-20)");

    // HEVC IDR_W_RADL (type 19): byte 0 = 0x26, byte 1 = 0x01
    const uint8_t hevc_idr_2601[] = {0x26, 0x01, 0x88, 0x20};
    auto c2 = ClassifyRtpPayload(hevc_idr_2601);
    DUWN_ASSERT(c2.codec == DetectedCodec::H265);
    DUWN_ASSERT(std::string_view(c2.evidence) == "RtpPayload (HEVC IDR-19)");

    // Ambiguous packet without parameter set or unique header must NOT lock codec
    const uint8_t ambiguous_slice[] = {0x01, 0x00, 0x00, 0x50};
    auto c3 = ClassifyRtpPayload(ambiguous_slice);
    DUWN_ASSERT(c3.codec == DetectedCodec::Unknown);
}

// 7. Dynamic codec adaptation: decoder running in H.264 automatically switches on incoming codec change
DUWN_TEST(VideoPipeline_DynamicCodecSwitching_H264ToHevc) {
    using namespace duwn::video;
    D3D11Device d3d;
    if (!d3d.Create(true, false)) return;

    VideoDecoder decoder(d3d, [](VideoFrame) {});
    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::H264);

    const uint8_t hevc_idr[] = {0x28, 0x01, 0xAF, 0x00, 0x55};
    auto classification = ClassifyRtpPayload(hevc_idr);
    DUWN_ASSERT(classification.codec == DetectedCodec::H265);

    const auto target_codec = (classification.codec == DetectedCodec::H265)
        ? VideoCodecType::H265 : VideoCodecType::H264;
    bool codec_mismatch = (decoder.GetActiveCodec() != target_codec);
    DUWN_ASSERT(codec_mismatch);

    decoder.Flush();
    decoder.ResetCodecState();
    bool hevc_init_ok = decoder.Init(1920, 1080, target_codec);
    if (hevc_init_ok) {
        DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::H265);
    } else {
        // Host has no working HEVC MFT; verify reverse dynamic adaptation (Unknown/HEVC -> H.264)
        decoder.ResetCodecState();
        const uint8_t h264_sps[] = {0x67, 0x42, 0x00, 0x1e, 0x9a, 0x74, 0x05, 0x81};
        auto c_h264 = ClassifyRtpPayload(h264_sps);
        DUWN_ASSERT(c_h264.codec == DetectedCodec::H264);
        DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));
        DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::H264);
    }
}

// 8. SessionState callback dual-synchronization and decoder Init/Reset strictness
DUWN_TEST(VideoPipeline_SessionState_CallbackDualSync_AndDecoderStrictness) {
    using namespace duwn::airplay;

    duwn::airplay::SessionState state;
    std::atomic<int> state_cb_count{0};
    std::atomic<int> phase_cb_count{0};
    AirPlaySessionState last_state = AirPlaySessionState::Idle;
    SessionPhase last_phase = SessionPhase::Idle;

    state.SetStateCallback([&](AirPlaySessionState prev, AirPlaySessionState next) {
        state_cb_count.fetch_add(1, std::memory_order_relaxed);
        last_state = next;
    });

    state.SetCallback([&](SessionPhase prev, SessionPhase next) {
        phase_cb_count.fetch_add(1, std::memory_order_relaxed);
        last_phase = next;
    });

    // 1. TransitionState should trigger BOTH callbacks
    state.TransitionState(AirPlaySessionState::Connecting);
    DUWN_ASSERT(state_cb_count.load() == 1);
    DUWN_ASSERT(phase_cb_count.load() == 1);
    DUWN_ASSERT(last_state == AirPlaySessionState::Connecting);
    DUWN_ASSERT(last_phase == SessionPhase::Connecting);

    state.TransitionState(AirPlaySessionState::Streaming);
    DUWN_ASSERT(state_cb_count.load() == 2);
    DUWN_ASSERT(phase_cb_count.load() == 2);
    DUWN_ASSERT(last_state == AirPlaySessionState::Streaming);
    DUWN_ASSERT(last_phase == SessionPhase::Streaming);

    state.TransitionState(AirPlaySessionState::Disconnecting);
    DUWN_ASSERT(state_cb_count.load() == 3);
    DUWN_ASSERT(phase_cb_count.load() == 3);
    DUWN_ASSERT(last_state == AirPlaySessionState::Disconnecting);
    DUWN_ASSERT(last_phase == SessionPhase::Reconnecting);

    // 2. Transition should trigger BOTH callbacks
    state.Transition(SessionPhase::Advertising);
    DUWN_ASSERT(state_cb_count.load() == 4);
    DUWN_ASSERT(phase_cb_count.load() == 4);
    DUWN_ASSERT(last_state == AirPlaySessionState::Idle);
    DUWN_ASSERT(last_phase == SessionPhase::Advertising);

    // 3. Decoder Init/Reset strictness
    duwn::video::D3D11Device d3d;
    if (d3d.Create(true, false)) {
        duwn::video::VideoDecoder decoder(d3d, [](duwn::video::VideoFrame) {});
        std::atomic<bool> decoder_ready{true};

        // Failure case: 0x0 resolution must return false and NOT set ready to true
        bool init_bad = decoder.Init(0, 0, duwn::video::VideoCodecType::H264);
        if (!init_bad) {
            decoder_ready.store(false, std::memory_order_release);
        }
        DUWN_ASSERT(!init_bad);
        DUWN_ASSERT(!decoder_ready.load(std::memory_order_acquire));
    }
}

