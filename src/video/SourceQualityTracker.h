#pragma once
// SourceQualityTracker.h — Distinguishes requested receiver envelope from
// actual source quality delivered by Apple iOS/iPadOS AirPlay senders.
// Provides quality effectiveness classification and structured telemetry.

#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
#include <format>

namespace duwn::video {

enum class QualityEffectiveness {
    Unknown = 0,
    DeliveredAsRequested = 1,
    SourceLimited = 2,
    PartiallyDelivered = 3
};

inline const char* QualityEffectivenessToString(QualityEffectiveness q) noexcept {
    switch (q) {
    case QualityEffectiveness::DeliveredAsRequested: return "DELIVERED_AS_REQUESTED";
    case QualityEffectiveness::SourceLimited:        return "SOURCE_LIMITED";
    case QualityEffectiveness::PartiallyDelivered:   return "PARTIALLY_DELIVERED";
    case QualityEffectiveness::Unknown:
    default:                                         return "UNKNOWN";
    }
}

// Concept A: Requested receiver envelope passed to UxPlay (-s WxH@fps -fps <fps>)
struct RequestedReceiverEnvelope {
    uint32_t width{1920};
    uint32_t height{1920};
    uint32_t fps{60};
    std::string preset_name{"Auto"};
    bool is_original{false};

    uint32_t LongEdge() const noexcept { return std::max(width, height); }
};

// Concept B: Actual source aperture negotiated and decoded from RTP/NAL stream
struct ActualSourceAperture {
    uint32_t coded_width{0};
    uint32_t coded_height{0};
    uint32_t visible_width{0};
    uint32_t visible_height{0};
    double fps{0.0};
    double bitrate_mbps{0.0};
    std::string codec{"H264"};

    uint32_t LongEdge() const noexcept { return std::max(visible_width, visible_height); }
    bool IsLandscape() const noexcept { return visible_width >= visible_height; }
    bool IsValid() const noexcept { return visible_width > 0 && visible_height > 0; }
};

// Concept C: RenderTarget geometry and scaling policy for Output/Preview
struct RenderTargetInfo {
    uint32_t output_width{0};
    uint32_t output_height{0};
    uint32_t preview_width{0};
    uint32_t preview_height{0};
    std::string scaling_policy{"Match Source (1:1 Passthrough)"};
};

// In-memory observation of device and session capabilities
struct DeviceSessionObservation {
    std::wstring device_model{L"—"};       // e.g. L"iPhone11,2"
    std::wstring marketing_name{L"—"};     // e.g. L"iPhone XS"
    std::wstring user_agent{L"—"};        // e.g. L"AirPlay/860.7.1"
    std::wstring transport{L"Wireless"};  // L"Wireless" or L"Wired"
    uint32_t max_observed_landscape_long_edge{0};
    uint32_t max_observed_portrait_long_edge{0};
    uint32_t max_observed_long_edge{0};
    uint32_t observed_gain_over_fhd{0}; // Extra pixels beyond 1920 long edge
};

class SourceQualityTracker {
public:
    static constexpr uint32_t kDefaultStableFrames = 60; // ~1-2 seconds at 30-60 FPS

    explicit SourceQualityTracker(uint32_t min_stable_frames = kDefaultStableFrames) noexcept
        : m_min_stable_frames(min_stable_frames) {}

    // Feed latest frame geometry and stream metrics
    void OnFrame(uint32_t coded_w, uint32_t coded_h,
                 uint32_t vis_w, uint32_t vis_h,
                 double fps, double bitrate_mbps,
                 const std::string& codec) noexcept;

    // Configure requested receiver envelope
    void SetRequestedEnvelope(const RequestedReceiverEnvelope& env) noexcept;

    // Update connected device metadata
    void SetClientInfo(const std::wstring& model,
                       const std::wstring& marketing,
                       const std::wstring& ua,
                       const std::wstring& transport) noexcept;

    // Reset state upon session disconnect
    void ResetSession() noexcept;

    // Accessors
    QualityEffectiveness GetEffectiveness() const noexcept { return m_effectiveness; }
    bool IsStable() const noexcept { return m_is_stable; }
    const RequestedReceiverEnvelope& GetRequested() const noexcept { return m_requested; }
    const ActualSourceAperture& GetActual() const noexcept { return m_stable_aperture; }
    const ActualSourceAperture& GetCurrent() const noexcept { return m_current_aperture; }
    const DeviceSessionObservation& GetObservation() const noexcept { return m_observation; }

    // Static classification evaluator (pure, thread-safe, unit-testable)
    static QualityEffectiveness Classify(
        const RequestedReceiverEnvelope& req,
        const ActualSourceAperture& actual,
        const DeviceSessionObservation& obs,
        bool is_stable) noexcept;

    // Formats the exact [SOURCE QUALITY] structured log block
    std::string FormatTelemetryBlock() const;

private:
    void Reevaluate() noexcept;

    uint32_t m_min_stable_frames{kDefaultStableFrames};
    RequestedReceiverEnvelope m_requested{};
    ActualSourceAperture m_current_aperture{};
    ActualSourceAperture m_candidate_aperture{};
    ActualSourceAperture m_stable_aperture{};
    DeviceSessionObservation m_observation{};
    uint32_t m_stable_frame_count{0};
    bool m_is_stable{false};
    QualityEffectiveness m_effectiveness{QualityEffectiveness::Unknown};
};

} // namespace duwn::video
