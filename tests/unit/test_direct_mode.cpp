#include "direct/DirectProtocol.h"
#include "direct/DirectSessionModel.h"
#include "direct/DirectNegotiator.h"

using namespace duwn::direct;

DUWN_TEST(DirectMode_ProtocolMajorMismatch_RejectsAndFallsBack) {
    DirectProtocolVersion sender_v{2, 0};
    DirectProtocolVersion receiver_v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = receiver_v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.max_fps = 60;

    CaptureCapabilities sender_capture;
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;
    sender_capture.max_fps = 60;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;
    sender_encoder.max_fps = 60;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;

    auto plan = DirectNegotiator::Negotiate(
        sender_v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(!plan.negotiated);
    DUWN_ASSERT(plan.fallback_to_airplay);
    DUWN_ASSERT(!plan.failure_reason.empty());
}

DUWN_TEST(DirectMode_MinorForwardCompatibility_Succeeds) {
    DirectProtocolVersion sender_v{1, 3}; // Newer minor version
    DirectProtocolVersion receiver_v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = receiver_v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.max_fps = 60;

    CaptureCapabilities sender_capture;
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;
    sender_capture.max_fps = 60;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;
    sender_encoder.max_fps = 60;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;

    auto plan = DirectNegotiator::Negotiate(
        sender_v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(plan.negotiated);
    DUWN_ASSERT(!plan.fallback_to_airplay);
    DUWN_ASSERT(plan.negotiated_version.major == 1);
    DUWN_ASSERT(plan.negotiated_version.minor == 0);
}

DUWN_TEST(DirectMode_CapabilityIntersection_CodecsAndTransports) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::HEVC, DirectVideoCodec::AV1};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram, DirectTransportType::DirectTcp};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.max_fps = 60;

    CaptureCapabilities sender_capture;
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;
    sender_capture.max_fps = 60;

    EncoderCapabilities sender_encoder;
    // Sender supports H264 and HEVC. Receiver supports HEVC and AV1.
    sender_encoder.supported_codecs = {DirectVideoCodec::H264, DirectVideoCodec::HEVC};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;
    sender_encoder.max_fps = 60;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;

    auto plan = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(plan.negotiated);
    // Intersection must be HEVC (H264 unsupported by receiver, AV1 unsupported by sender)
    DUWN_ASSERT(plan.selected_codec == DirectVideoCodec::HEVC);
    DUWN_ASSERT(plan.selected_transport == DirectTransportType::DirectDatagram);
}

DUWN_TEST(DirectMode_UnsupportedCodecExclusion_FallsBackWhenEmpty) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.max_fps = 60;

    CaptureCapabilities sender_capture;
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;
    sender_capture.max_fps = 60;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::AV1}; // Disjoint codec set
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;
    sender_encoder.max_fps = 60;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;

    auto plan = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(!plan.negotiated);
    DUWN_ASSERT(plan.fallback_to_airplay);
    DUWN_ASSERT(!plan.failure_reason.empty());
}

DUWN_TEST(DirectMode_ResolutionCapabilityIntersection) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 3840;
    receiver_caps.max_height = 2160;
    receiver_caps.display_canvas_resolution = {1920, 1080};

    CaptureCapabilities sender_capture;
    sender_capture.native_source_resolution = {1920, 1080};
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 2560;
    sender_encoder.max_height = 1440;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;
    prefs.quality_policy = DirectQualityPolicy::Balanced;

    auto plan = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(plan.negotiated);
    DUWN_ASSERT(plan.actual_source_resolution.width == 1920);
    DUWN_ASSERT(plan.actual_source_resolution.height == 1080);
    DUWN_ASSERT(plan.encoded_resolution.width == 1920);
    DUWN_ASSERT(plan.encoded_resolution.height == 1080);
    DUWN_ASSERT(plan.render_resolution.width == 1920);
    DUWN_ASSERT(plan.render_resolution.height == 1080);
}

DUWN_TEST(DirectMode_FpsIntersection) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.max_fps = 90;

    CaptureCapabilities sender_capture;
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;
    sender_capture.max_fps = 120;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;
    sender_encoder.max_fps = 60; // Encoder bottleneck

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    // Test 1: Requested 60 FPS -> capped by encoder at 60
    SessionPreferences prefs1;
    prefs1.requested_fps = 60;
    auto plan1 = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs1);
    DUWN_ASSERT(plan1.negotiated);
    DUWN_ASSERT(plan1.negotiated_fps == 60);

    // Test 2: User requests 30 FPS -> respected
    SessionPreferences prefs2;
    prefs2.requested_fps = 30;
    auto plan2 = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs2);
    DUWN_ASSERT(plan2.negotiated);
    DUWN_ASSERT(plan2.negotiated_fps == 30);
}

DUWN_TEST(DirectMode_QualityPreferencePreserved) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;
    receiver_caps.display_canvas_resolution = {2560, 1440};

    CaptureCapabilities sender_capture;
    sender_capture.native_source_resolution = {1920, 1080};
    sender_capture.max_width = 1920;
    sender_capture.max_height = 1080;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    // 1. LowestLatency
    SessionPreferences prefs_ll;
    prefs_ll.quality_policy = DirectQualityPolicy::LowestLatency;
    auto plan_ll = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs_ll);
    DUWN_ASSERT(plan_ll.applied_latency_mode == DirectLatencyMode::UltraLowLatency);
    DUWN_ASSERT(plan_ll.requested_quality == DirectQualityPolicy::LowestLatency);

    // 2. Balanced
    SessionPreferences prefs_b;
    prefs_b.quality_policy = DirectQualityPolicy::Balanced;
    auto plan_b = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs_b);
    DUWN_ASSERT(plan_b.applied_latency_mode == DirectLatencyMode::LowLatency);
    DUWN_ASSERT(plan_b.requested_quality == DirectQualityPolicy::Balanced);

    // 3. HighQuality
    SessionPreferences prefs_hq;
    prefs_hq.quality_policy = DirectQualityPolicy::HighQuality;
    auto plan_hq = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs_hq);
    DUWN_ASSERT(plan_hq.applied_latency_mode == DirectLatencyMode::NormalLatency);
    DUWN_ASSERT(plan_hq.requested_quality == DirectQualityPolicy::HighQuality);

    // All resolution concepts are separate and well-formed
    DUWN_ASSERT(plan_hq.actual_source_resolution == DirectResolution(1920, 1080));
    DUWN_ASSERT(plan_hq.encoded_resolution == DirectResolution(1920, 1080));
    DUWN_ASSERT(plan_hq.render_resolution == DirectResolution(2560, 1440));
}

DUWN_TEST(DirectMode_FutureDeviceAbove1080pNotCapped) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::HEVC};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 3840;
    receiver_caps.max_height = 2160;
    receiver_caps.display_canvas_resolution = {3840, 2160};

    CaptureCapabilities sender_capture;
    sender_capture.native_source_resolution = {2556, 1179}; // iPhone 15 Pro resolution
    sender_capture.max_width = 2556;
    sender_capture.max_height = 1179;

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::HEVC};
    sender_encoder.max_width = 3840;
    sender_encoder.max_height = 2160;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;
    prefs.quality_policy = DirectQualityPolicy::HighQuality;

    auto plan = DirectNegotiator::Negotiate(
        v, sender_capture, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(plan.negotiated);
    // Must NOT be capped to 1920!
    DUWN_ASSERT(plan.encoded_resolution.width == 2556);
    DUWN_ASSERT(plan.encoded_resolution.height == 1179);
}

DUWN_TEST(DirectMode_ZeroDeviceModelDependency) {
    DirectProtocolVersion v{1, 0};

    ReceiverCapabilities receiver_caps;
    receiver_caps.protocol_version = v;
    receiver_caps.supported_decoders = {DirectVideoCodec::H264};
    receiver_caps.supported_transports = {DirectTransportType::DirectDatagram};
    receiver_caps.max_width = 1920;
    receiver_caps.max_height = 1080;

    CaptureCapabilities sender_a;
    sender_a.native_source_resolution = {1920, 1080};
    sender_a.max_width = 1920;
    sender_a.max_height = 1080;
    sender_a.device_model_telemetry = "iPhone11,2";
    sender_a.os_version_telemetry = "iOS 16.0";

    CaptureCapabilities sender_b;
    sender_b.native_source_resolution = {1920, 1080};
    sender_b.max_width = 1920;
    sender_b.max_height = 1080;
    sender_b.device_model_telemetry = "FutureDevice99,9";
    sender_b.os_version_telemetry = "NextGenOS 1.0";

    EncoderCapabilities sender_encoder;
    sender_encoder.supported_codecs = {DirectVideoCodec::H264};
    sender_encoder.max_width = 1920;
    sender_encoder.max_height = 1080;

    TransportCapabilities sender_transport;
    sender_transport.supported_transports = {DirectTransportType::DirectDatagram};

    SessionPreferences prefs;

    auto plan_a = DirectNegotiator::Negotiate(
        v, sender_a, sender_encoder, sender_transport, receiver_caps, prefs);
    auto plan_b = DirectNegotiator::Negotiate(
        v, sender_b, sender_encoder, sender_transport, receiver_caps, prefs);

    DUWN_ASSERT(plan_a.negotiated == plan_b.negotiated);
    DUWN_ASSERT(plan_a.selected_codec == plan_b.selected_codec);
    DUWN_ASSERT(plan_a.encoded_resolution == plan_b.encoded_resolution);
    DUWN_ASSERT(plan_a.negotiated_fps == plan_b.negotiated_fps);
    DUWN_ASSERT(plan_a.applied_latency_mode == plan_b.applied_latency_mode);
}

DUWN_TEST(DirectMode_BufferingInvariant) {
    DUWN_ASSERT(DirectBufferingInvariant::kMaxPendingFreshestFrames == 1);
    DUWN_ASSERT(!DirectBufferingInvariant::kAllowFifoAccumulation);
    DUWN_ASSERT(DirectBufferingInvariant::kNewerFrameSupersedesOlder);

    DUWN_ASSERT(DirectBufferingInvariant::IsConforming(0));
    DUWN_ASSERT(DirectBufferingInvariant::IsConforming(1));
    DUWN_ASSERT(!DirectBufferingInvariant::IsConforming(2));
    DUWN_ASSERT(!DirectBufferingInvariant::IsConforming(5));
}

DUWN_TEST(DirectMode_RealityGate_ProductionDisabledAndAirPlayDefault) {
    // Reality Gate safety invariant:
    // Direct Mode is not enabled for production until real Apple build & hardware device validation
    DUWN_ASSERT(!DirectFeatureGate::kDirectModeProductionEnabled);
    DUWN_ASSERT(DirectFeatureGate::kAirPlayIsProductionDefault);
    DUWN_ASSERT(!DirectFeatureGate::IsDirectModePermitted(false));
    DUWN_ASSERT(DirectFeatureGate::IsDirectModePermitted(true)); // Permitted only under developer override
}

