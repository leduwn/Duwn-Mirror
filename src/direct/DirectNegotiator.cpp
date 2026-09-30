#include "DirectNegotiator.h"
#include <algorithm>

namespace duwn::direct {

std::vector<DirectVideoCodec> DirectNegotiator::IntersectCodecs(
    const std::vector<DirectVideoCodec>& sender_codecs,
    const std::vector<DirectVideoCodec>& receiver_codecs) noexcept {
    std::vector<DirectVideoCodec> intersection;
    for (auto c : sender_codecs) {
        if (c == DirectVideoCodec::None) continue;
        if (std::find(receiver_codecs.begin(), receiver_codecs.end(), c) != receiver_codecs.end()) {
            if (std::find(intersection.begin(), intersection.end(), c) == intersection.end()) {
                intersection.push_back(c);
            }
        }
    }
    return intersection;
}

std::vector<DirectTransportType> DirectNegotiator::IntersectTransports(
    const std::vector<DirectTransportType>& sender_transports,
    const std::vector<DirectTransportType>& receiver_transports) noexcept {
    std::vector<DirectTransportType> intersection;
    for (auto t : sender_transports) {
        if (t == DirectTransportType::None) continue;
        if (std::find(receiver_transports.begin(), receiver_transports.end(), t) != receiver_transports.end()) {
            if (std::find(intersection.begin(), intersection.end(), t) == intersection.end()) {
                intersection.push_back(t);
            }
        }
    }
    return intersection;
}

SessionPlan DirectNegotiator::Negotiate(
    const DirectProtocolVersion& sender_protocol_version,
    const CaptureCapabilities& sender_capture,
    const EncoderCapabilities& sender_encoder,
    const TransportCapabilities& sender_transport,
    const ReceiverCapabilities& receiver_caps,
    const SessionPreferences& prefs,
    const RuntimeNetworkMetrics& net_metrics) noexcept {

    SessionPlan plan;
    plan.requested_quality = prefs.quality_policy;

    // 1. Protocol Version Validation (Major must match, minor forward-compatible)
    if (receiver_caps.protocol_version.HasMajorMismatch(sender_protocol_version)) {
        plan.negotiated = false;
        plan.fallback_to_airplay = true;
        plan.failure_reason = "Protocol major mismatch: sender " + sender_protocol_version.ToString() +
                              " vs receiver " + receiver_caps.protocol_version.ToString();
        return plan;
    }

    // Set negotiated version to the lowest compatible minor version
    plan.negotiated_version.major = receiver_caps.protocol_version.major;
    plan.negotiated_version.minor = std::min(sender_protocol_version.minor, receiver_caps.protocol_version.minor);

    // 2. Codec Intersection & Qualification
    auto available_codecs = IntersectCodecs(sender_encoder.supported_codecs, receiver_caps.supported_decoders);
    if (available_codecs.empty()) {
        plan.negotiated = false;
        plan.fallback_to_airplay = true;
        plan.failure_reason = "No compatible video codec";
        return plan;
    }

    // Phase 7 Policy: H264 remains the compatibility baseline.
    // [UNVALIDATED_PROVISIONAL] HEVC qualification heuristics (derived from synthetic model;
    // not yet measured on physical Apple hardware):
    // 1. Sender hardware encoder supports required low-latency mode
    // 2. Receiver hardware decoder supports HEVC
    // 3. Provisional HEVC path offers useful quality/bitrate benefit (>=1440p, HighQuality, or <12 Mbps)
    // 4. Latency does not regress beyond provisional threshold (RTT <= 30ms)
    // Do not select HEVC merely because device model is new.
    bool hevc_in_intersection = (std::find(available_codecs.begin(), available_codecs.end(), DirectVideoCodec::HEVC) != available_codecs.end());
    bool h264_in_intersection = (std::find(available_codecs.begin(), available_codecs.end(), DirectVideoCodec::H264) != available_codecs.end());

    bool sender_hevc_hw_ready = sender_encoder.hardware_accelerated && sender_encoder.hevc_supports_low_latency;
    bool receiver_hevc_hw_ready = receiver_caps.hardware_decoder && receiver_caps.hevc_hardware_decoder;
    bool hevc_hardware_qualified = sender_hevc_hw_ready && receiver_hevc_hw_ready;

    // [UNVALIDATED_PROVISIONAL] Benefit thresholds from synthetic Phase 6A/7 models:
    bool resolution_benefit = (sender_capture.max_width >= 2560 && sender_capture.max_height >= 1440) ||
                              (receiver_caps.display_canvas_resolution.width >= 2560);
    bool quality_benefit = (prefs.quality_policy == DirectQualityPolicy::HighQuality);
    bool bandwidth_benefit = (net_metrics.estimated_bandwidth_mbps > 0.0 && net_metrics.estimated_bandwidth_mbps < 12.0);
    bool offers_useful_benefit = resolution_benefit || quality_benefit || bandwidth_benefit;

    // [UNVALIDATED_PROVISIONAL] RTT threshold from simulation:
    bool latency_acceptable = (net_metrics.rtt_ms == 0.0 || net_metrics.rtt_ms <= 30.0);

    bool hevc_qualified = hevc_in_intersection && hevc_hardware_qualified && offers_useful_benefit && latency_acceptable;

    if (prefs.preferred_codec == DirectVideoCodec::HEVC && hevc_in_intersection) {
        if (hevc_hardware_qualified) {
            plan.selected_codec = DirectVideoCodec::HEVC;
        } else if (h264_in_intersection) {
            // User requested HEVC, but hardware low-latency not supported -> fallback to H264 baseline
            plan.selected_codec = DirectVideoCodec::H264;
        } else {
            plan.selected_codec = DirectVideoCodec::HEVC;
        }
    } else if (prefs.preferred_codec == DirectVideoCodec::H264 && h264_in_intersection) {
        plan.selected_codec = DirectVideoCodec::H264;
    } else if (prefs.preferred_codec != DirectVideoCodec::None &&
               std::find(available_codecs.begin(), available_codecs.end(), prefs.preferred_codec) != available_codecs.end()) {
        plan.selected_codec = prefs.preferred_codec;
    } else {
        // Auto-selection:
        // [UNVALIDATED_PROVISIONAL] HEVC auto-selection is gated behind allow_provisional_hevc.
        // In normal production sessions (default false), H.264 remains the baseline.
        if (prefs.allow_provisional_hevc && hevc_qualified) {
            plan.selected_codec = DirectVideoCodec::HEVC;
        } else if (h264_in_intersection) {
            // H264 remains the compatibility baseline
            plan.selected_codec = DirectVideoCodec::H264;
        } else {
            // H264 not available in intersection, use available codec
            plan.selected_codec = available_codecs.front();
        }
    }

    // 3. Transport Intersection
    auto available_transports = IntersectTransports(sender_transport.supported_transports, receiver_caps.supported_transports);
    if (available_transports.empty()) {
        plan.negotiated = false;
        plan.fallback_to_airplay = true;
        plan.failure_reason = "No compatible transport protocol";
        return plan;
    }

    if (prefs.preferred_transport != DirectTransportType::None &&
        std::find(available_transports.begin(), available_transports.end(), prefs.preferred_transport) != available_transports.end()) {
        plan.selected_transport = prefs.preferred_transport;
    } else {
        // Priority: DirectDatagram (lowest latency) -> DirectQuic -> DirectTcp
        if (std::find(available_transports.begin(), available_transports.end(), DirectTransportType::DirectDatagram) != available_transports.end()) {
            plan.selected_transport = DirectTransportType::DirectDatagram;
        } else if (std::find(available_transports.begin(), available_transports.end(), DirectTransportType::DirectQuic) != available_transports.end()) {
            plan.selected_transport = DirectTransportType::DirectQuic;
        } else {
            plan.selected_transport = available_transports.front();
        }
    }

    // 4. Resolution Intersection (Represented separately: actual source, encoded, render)
    // Actual source resolution is physical capture source
    if (!sender_capture.native_source_resolution.IsEmpty()) {
        plan.actual_source_resolution = sender_capture.native_source_resolution;
    } else {
        plan.actual_source_resolution = {sender_capture.max_width, sender_capture.max_height};
    }

    // Render resolution is receiver display canvas
    if (!receiver_caps.display_canvas_resolution.IsEmpty()) {
        plan.render_resolution = receiver_caps.display_canvas_resolution;
    } else {
        plan.render_resolution = {1920, 1080};
    }

    // Capability intersection for encoded resolution
    // Notice: Do NOT cap hypothetical future devices at 1920.
    uint32_t cap_w = std::min({sender_capture.max_width, sender_encoder.max_width, receiver_caps.max_width});
    uint32_t cap_h = std::min({sender_capture.max_height, sender_encoder.max_height, receiver_caps.max_height});

    if (cap_w == 0 || cap_h == 0) {
        plan.negotiated = false;
        plan.fallback_to_airplay = true;
        plan.failure_reason = "Resolution capability intersection is zero";
        return plan;
    }

    if (!prefs.preferred_resolution.IsEmpty() &&
        prefs.preferred_resolution.width <= cap_w &&
        prefs.preferred_resolution.height <= cap_h) {
        plan.encoded_resolution = prefs.preferred_resolution;
    } else {
        // Derive from quality policy
        switch (prefs.quality_policy) {
        case DirectQualityPolicy::LowestLatency:
            // Cap at 1080p for ultra-low latency, or lower if source is smaller
            plan.encoded_resolution.width  = std::min(cap_w, 1920u);
            plan.encoded_resolution.height = std::min(cap_h, 1080u);
            break;
        case DirectQualityPolicy::Balanced:
            // Use native source bounds up to capability ceiling
            if (!plan.actual_source_resolution.IsEmpty()) {
                plan.encoded_resolution.width  = std::min(cap_w, plan.actual_source_resolution.width);
                plan.encoded_resolution.height = std::min(cap_h, plan.actual_source_resolution.height);
            } else {
                plan.encoded_resolution = {cap_w, cap_h};
            }
            break;
        case DirectQualityPolicy::HighQuality:
            // Full capability ceiling without downscaling
            plan.encoded_resolution = {cap_w, cap_h};
            break;
        }
    }

    // 5. FPS Intersection
    uint32_t max_allowed_fps = std::min({sender_capture.max_fps, sender_encoder.max_fps, receiver_caps.max_fps});
    if (prefs.requested_fps > 0) {
        plan.negotiated_fps = std::min(max_allowed_fps, prefs.requested_fps);
    } else {
        plan.negotiated_fps = max_allowed_fps;
    }
    if (plan.negotiated_fps == 0) {
        plan.negotiated_fps = 60;
    }

    // 6. Quality Policy and Latency Mode
    switch (prefs.quality_policy) {
    case DirectQualityPolicy::LowestLatency:
        plan.applied_latency_mode = DirectLatencyMode::UltraLowLatency;
        break;
    case DirectQualityPolicy::Balanced:
        plan.applied_latency_mode = DirectLatencyMode::LowLatency;
        break;
    case DirectQualityPolicy::HighQuality:
        plan.applied_latency_mode = DirectLatencyMode::NormalLatency;
        break;
    }

    // Estimated initial bitrate based on resolution and policy
    uint64_t pixels = plan.encoded_resolution.TotalPixels();
    if (pixels <= 1920u * 1080u) {
        plan.target_bitrate_kbps = (prefs.quality_policy == DirectQualityPolicy::LowestLatency) ? 12000 : 20000;
    } else if (pixels <= 2560u * 1440u) {
        plan.target_bitrate_kbps = (prefs.quality_policy == DirectQualityPolicy::LowestLatency) ? 18000 : 30000;
    } else {
        plan.target_bitrate_kbps = (prefs.quality_policy == DirectQualityPolicy::LowestLatency) ? 25000 : 45000;
    }

    // If network metrics are provided and report high packet loss, reduce bitrate
    if (net_metrics.packet_loss_rate > 0.05 && plan.target_bitrate_kbps > 10000) {
        plan.target_bitrate_kbps = static_cast<uint32_t>(plan.target_bitrate_kbps * 0.7);
    }

    plan.negotiated = true;
    plan.fallback_to_airplay = false;
    plan.failure_reason.clear();
    return plan;
}

} // namespace duwn::direct
