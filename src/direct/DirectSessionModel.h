#pragma once
// DirectSessionModel.h — Duwn Direct mode session capability and configuration structures.
// Follows capability-driven negotiation; no device-model rules.

#include "DirectProtocol.h"
#include <vector>
#include <string>
#include <algorithm>

namespace duwn::direct {

// 1. Sender Capture Capabilities (e.g. ReplayKit / ScreenCaptureKit source)
struct CaptureCapabilities {
    uint32_t max_width{0};
    uint32_t max_height{0};
    uint32_t max_fps{60};
    std::vector<DirectResolution> supported_resolutions;
    bool hdr_supported{false};
    DirectResolution native_source_resolution{0, 0}; // Physical screen/source bounds

    // Telemetry only — NEVER used for negotiation decisions.
    std::string device_model_telemetry;
    std::string os_version_telemetry;
};

// 2. Sender Hardware Encoder Capabilities
struct EncoderCapabilities {
    std::vector<DirectVideoCodec> supported_codecs;
    uint32_t max_width{0};
    uint32_t max_height{0};
    uint32_t max_fps{60};
    uint32_t max_bitrate_kbps{0};
    bool hardware_accelerated{false};
    bool supports_dynamic_bitrate{false};
    bool supports_intra_refresh{false};
    bool hevc_supports_low_latency{false}; // Hardware low-latency real-time HEVC support
};

// 3. Sender Transport Capabilities
struct TransportCapabilities {
    std::vector<DirectTransportType> supported_transports;
    bool mtu_probing{false};
    uint32_t max_packet_size{1472};
    bool supports_fec{false};
};

// 4. Receiver (PC) Capabilities
struct ReceiverCapabilities {
    DirectProtocolVersion protocol_version{kCurrentDirectProtocolVersion};
    std::vector<DirectVideoCodec> supported_decoders;
    std::vector<DirectTransportType> supported_transports;
    uint32_t max_width{0};
    uint32_t max_height{0};
    uint32_t max_fps{60};
    DirectResolution display_canvas_resolution{1920, 1080}; // Render surface target
    bool hardware_decoder{false};
    bool hevc_hardware_decoder{false}; // Verified Media Foundation hardware HEVC MFT
    bool zero_copy_render{false};
    bool supports_low_latency_mode{true};
};

// 5. User / Session Preferences
struct SessionPreferences {
    DirectQualityPolicy quality_policy{DirectQualityPolicy::Balanced};
    uint32_t requested_fps{60};
    DirectResolution preferred_resolution{0, 0}; // 0,0 means automatic based on capability
    DirectVideoCodec preferred_codec{DirectVideoCodec::None}; // None means automatic
    DirectTransportType preferred_transport{DirectTransportType::None}; // None means automatic
    // [UNVALIDATED_PROVISIONAL] HEVC heuristic auto-selection flag.
    // Default false in normal sessions: prevents unmeasured synthetic heuristics from auto-selecting HEVC.
    bool allow_provisional_hevc{false};
};

// 6. Runtime Network Quality Metrics
struct RuntimeNetworkMetrics {
    double estimated_bandwidth_mbps{0.0};
    double rtt_ms{0.0};
    double packet_loss_rate{0.0};
    double jitter_ms{0.0};
};

// 7. Buffering Invariant Specification
// Direct video must never accumulate an arbitrary FIFO queue.
// Architecture supports 0 or 1 pending freshest frame. Older pending frame is superseded.
struct DirectBufferingInvariant {
    static constexpr uint32_t kMaxPendingFreshestFrames = 1;
    static constexpr bool kAllowFifoAccumulation = false;
    static constexpr bool kNewerFrameSupersedesOlder = true;

    // Evaluates whether a queue depth conforms to the Direct Mode invariant.
    static constexpr bool IsConforming(uint32_t queue_depth) noexcept {
        return queue_depth <= kMaxPendingFreshestFrames;
    }
};

// 8. Negotiated Session Plan
// Distinctly separates:
// - requested_quality (policy enum)
// - actual_source_resolution (physical capture dimensions)
// - encoded_resolution (negotiated bitstream wire dimensions)
// - render_resolution (target swapchain / canvas dimensions)
struct SessionPlan {
    bool negotiated{false};
    std::string failure_reason;
    bool fallback_to_airplay{false};

    DirectProtocolVersion negotiated_version{0, 0};
    DirectVideoCodec selected_codec{DirectVideoCodec::None};
    DirectTransportType selected_transport{DirectTransportType::None};

    // Four distinct resolution concepts — NEVER conflated
    DirectQualityPolicy requested_quality{DirectQualityPolicy::Balanced};
    DirectResolution actual_source_resolution{0, 0};
    DirectResolution encoded_resolution{0, 0};
    DirectResolution render_resolution{0, 0};

    uint32_t negotiated_fps{0};
    uint32_t target_bitrate_kbps{0};
    DirectLatencyMode applied_latency_mode{DirectLatencyMode::LowLatency};
};

} // namespace duwn::direct
