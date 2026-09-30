#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace duwn {

// Receiver delivery policy. Source resolution/FPS and AirPlay forwarding are
// deliberately independent: dropping a decoded frame never corrupts a GOP.
enum class StreamingMode {
    SmoothLive = 0,     // Balanced, cadence-aware catchup (existing behavior).
    LowLatency = 1,     // One decoded frame: keep the newest available image.
    Compatibility = 2, // Prefer smooth cadence, with a bounded three-frame queue.
    Custom = 3,
};

inline constexpr uint32_t kCustomFreshnessChoicesMs[] = {5, 10, 16, 25, 40, 60, 100};

// Four bytes so the render/decode threads can read one consistent atomic
// snapshot while the UI changes policy without restarting the connection.
struct StreamingPolicy {
    uint8_t max_decoded_frames{3};
    uint8_t max_residence_ms{0}; // 0 = cadence-based threshold, not added delay.
    uint8_t cadence_percent{125};
    bool always_latest{false};
};
static_assert(sizeof(StreamingPolicy) == 4);
static_assert(std::is_trivially_copyable_v<StreamingPolicy>);

inline StreamingPolicy ResolveStreamingPolicy(StreamingMode mode,
                                              uint32_t custom_ms = 25,
                                              uint32_t custom_frames = 2) noexcept {
    switch (mode) {
    case StreamingMode::LowLatency: return {1, 0, 125, true};
    case StreamingMode::Compatibility: return {3, 0, 200, false};
    case StreamingMode::Custom:
        return {static_cast<uint8_t>(std::clamp(custom_frames, 1u, 3u)),
                static_cast<uint8_t>(std::clamp(custom_ms, 5u, 100u)), 125, false};
    default: return {}; // Preserve the tested balanced path and unknown values.
    }
}

inline double FreshnessThresholdMs(StreamingPolicy policy, double cadence_ms) noexcept {
    if (policy.max_residence_ms != 0) return policy.max_residence_ms;
    // Invalid/missing source cadence must not produce an unbounded backlog.
    const double cadence = cadence_ms > 0.0 && cadence_ms <= 1000.0
        ? cadence_ms : 16.6667;
    return cadence * policy.cadence_percent / 100.0;
}

inline bool ShouldPresentNewest(StreamingPolicy policy, size_t queued_frames,
                                double oldest_age_ms, double cadence_ms) noexcept {
    // A lone frame is still displayed after a quiet/static period. This budget
    // chooses which pending image to show; it cannot bound end-to-end latency.
    return queued_frames > 1 && (policy.always_latest ||
        oldest_age_ms > FreshnessThresholdMs(policy, cadence_ms));
}

} // namespace duwn
