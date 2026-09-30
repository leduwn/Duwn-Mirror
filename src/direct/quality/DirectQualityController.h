#pragma once
// DirectQualityController.h — Adaptive Quality + Latency Controller for Duwn Direct Mode.
// Priority:
// 1. Prevent latency queue growth (bounded 0/1 frame buffer invariant).
// 2. Maintain smooth FPS (60 FPS preferred, especially for gaming).
// 3. Preserve visual quality floor derived from Phase 6A (never drop bitrate into blur).
// 4. Maximize spatial quality when bandwidth allows.

#include "direct/DirectProtocol.h"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace duwn::direct::quality {

// [UNVALIDATED_PROVISIONAL] Resolution Quality Profile derived from Phase 6A synthetic distortion model.
// Status: PROVISIONAL pending physical iOS/Mac hardware and display verification.
// Must not be treated as authoritative device-independent physical truth.
struct ResolutionTierProfile {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t floor_bitrate_bps{6'000'000};     // [UNVALIDATED_PROVISIONAL] LOWEST_ACCEPTABLE_QUALITY_POINT
    uint32_t balanced_bitrate_bps{12'000'000}; // [UNVALIDATED_PROVISIONAL] BEST_BALANCED_POINT
    uint32_t high_bitrate_bps{20'000'000};     // [UNVALIDATED_PROVISIONAL] HIGHEST_QUALITY_LOW_LATENCY_POINT

    bool operator==(const ResolutionTierProfile& o) const noexcept {
        return width == o.width && height == o.height;
    }
};

// [UNVALIDATED_PROVISIONAL] Synthetic Resolution Ladder
inline const ResolutionTierProfile kTier1440p{
    2560, 1440,
    12'000'000, // [UNVALIDATED_PROVISIONAL] 12.0 Mbps floor (synthetic bpp = 0.054)
    16'000'000, // [UNVALIDATED_PROVISIONAL] 16.0 Mbps balanced
    25'000'000  // [UNVALIDATED_PROVISIONAL] 25.0 Mbps high
};

inline const ResolutionTierProfile kTier1080p{
    1920, 1080,
    6'000'000,  // [UNVALIDATED_PROVISIONAL] 6.0 Mbps floor (synthetic bpp = 0.048)
    12'000'000, // [UNVALIDATED_PROVISIONAL] 12.0 Mbps balanced
    20'000'000  // [UNVALIDATED_PROVISIONAL] 20.0 Mbps high
};

inline const ResolutionTierProfile kTier720p{
    1280, 720,
    3'000'000,  // [UNVALIDATED_PROVISIONAL] 3.0 Mbps floor (synthetic bpp = 0.054)
    5'000'000,  // [UNVALIDATED_PROVISIONAL] 5.0 Mbps balanced
    8'000'000   // [UNVALIDATED_PROVISIONAL] 8.0 Mbps high
};

// Input telemetry metrics to the adaptive controller
struct NetworkTelemetryMetrics {
    uint32_t actual_send_bitrate_bps{0};
    uint32_t available_throughput_estimate_bps{0};
    double rtt_ms{0.0};
    double jitter_ms{0.0};
    double packet_loss_fraction{0.0}; // 0.0 to 1.0 (e.g. 0.02 = 2%)
    double encoder_time_ms{0.0};
    uint64_t frame_supersede_count{0};
    uint64_t receiver_incomplete_frames{0};
    uint32_t decoder_queue_depth{0};
};

// Output adaptation decision produced by controller
struct AdaptationDecision {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{60};
    uint32_t target_bitrate_bps{12'000'000};
    bool resolution_changed{false};
    bool fps_changed{false};
    bool bitrate_changed{false};
    bool keyframe_requested{false};
    std::string reason;
};

// Adaptive Quality Controller configuration
struct ControllerConfig {
    ResolutionTierProfile native_source_resolution{kTier1080p};
    uint32_t initial_fps{60};
    uint32_t min_fps{30};
    uint32_t evaluation_interval_ms{100};
    uint32_t step_up_hold_cycles{15};         // 1.5s clean network before additive bitrate step-up
    uint32_t resolution_step_up_hold_cycles{30}; // 3.0s clean headroom before upscaling resolution
    uint32_t resolution_cooldown_cycles{20};   // 2.0s cooldown after resolution change to prevent flapping
};

class DirectQualityController {
public:
    explicit DirectQualityController(const ControllerConfig& config = ControllerConfig{});

    // Evaluates current network & hardware metrics and computes next adaptation decision
    AdaptationDecision Evaluate(const NetworkTelemetryMetrics& metrics);

    // Getters for current state
    uint32_t GetCurrentWidth() const noexcept { return m_current_profile.width; }
    uint32_t GetCurrentHeight() const noexcept { return m_current_profile.height; }
    uint32_t GetCurrentFps() const noexcept { return m_current_fps; }
    uint32_t GetCurrentBitrate() const noexcept { return m_current_bitrate_bps; }
    uint64_t GetResolutionChangeCount() const noexcept { return m_resolution_changes; }
    uint64_t GetFpsChangeCount() const noexcept { return m_fps_changes; }
    uint64_t GetConsecutiveCleanCycles() const noexcept { return m_consecutive_clean_cycles; }

    // Resets controller state to initial configuration
    void Reset(const ControllerConfig& config);

private:
    // Helper methods
    ResolutionTierProfile GetNextLowerResolution(const ResolutionTierProfile& current) const noexcept;
    ResolutionTierProfile GetNextHigherResolution(const ResolutionTierProfile& current) const noexcept;
    bool IsCongested(const NetworkTelemetryMetrics& metrics) const noexcept;
    bool IsClean(const NetworkTelemetryMetrics& metrics) const noexcept;

    ControllerConfig m_config;
    ResolutionTierProfile m_current_profile;
    uint32_t m_current_fps{60};
    uint32_t m_current_bitrate_bps{12'000'000};

    uint32_t m_consecutive_clean_cycles{0};
    uint32_t m_resolution_cooldown_counter{0};
    uint64_t m_resolution_changes{0};
    uint64_t m_fps_changes{0};

    // Tracking last reported counter values to detect new events
    uint64_t m_last_supersede_count{0};
    uint64_t m_last_incomplete_frames{0};
};

} // namespace duwn::direct::quality
