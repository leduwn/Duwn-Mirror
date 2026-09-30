#pragma once

#include <cstdint>
#include <string>

namespace duwn::audio {

enum class AudioSessionClass {
    Startup,
    NormalSteady,
    DiscontinuityRecovery,
    EndpointChange,
    Reconnect
};

const char* AudioSessionClassName(AudioSessionClass cls) noexcept;

struct AudioTargetDecision {
    int64_t timestamp_ns{0};
    double current_ring_ms{0.0};
    double previous_target_ms{0.0};
    double new_target_ms{0.0};
    double source_packet_duration_ms{0.0};
    double rtp_jitter_estimate_ms{0.0};
    double largest_recent_gap_ms{0.0};
    double wasapi_engine_period_ms{0.0};
    double wasapi_padding_ms{0.0};
    uint64_t recent_underruns{0};
    uint64_t recent_overruns{0};
    AudioSessionClass session_class{AudioSessionClass::Startup};
    double servo_ppm{0.0};
    double base_margin_ms{0.0};
    double jitter_margin_ms{0.0};
    double endpoint_margin_ms{0.0};
    double recovery_margin_ms{0.0};
    double clamp_min_ms{0.0};
    double clamp_max_ms{0.0};
    double final_target_ms{0.0};
    const char* reason{"initial"};
};

class AudioBufferController {
public:
    explicit AudioBufferController(uint32_t sample_rate = 48000) noexcept;

    void Configure(uint32_t packet_frames, uint32_t engine_period_frames) noexcept;
    void ObserveJitter(double jitter_ms) noexcept;
    void OnUnderrun() noexcept;
    void AdvanceStable(double seconds) noexcept;
    void Reset() noexcept;

    void SetSessionClass(AudioSessionClass cls) noexcept { m_session_class = cls; }
    AudioSessionClass GetSessionClass() const noexcept { return m_session_class; }

    uint32_t TargetFrames() const noexcept;
    uint32_t MinimumFrames() const noexcept { return m_min_frames; }
    uint32_t MaximumFrames() const noexcept { return m_max_frames; }
    double TargetMs() const noexcept;

    const AudioTargetDecision& LastDecision() const noexcept { return m_last_decision; }

    // Development-only target override query
    static double GetDevTargetOverrideMs() noexcept;

    // Direct setter for dev/testing
    void SetDevTargetOverride(double ms) noexcept { m_dev_target_override_ms = ms; }
    double DevTargetOverride() const noexcept { return m_dev_target_override_ms; }

private:
    uint32_t ClampTarget(uint32_t frames) const noexcept;
    void RecordDecision(const char* reason, uint32_t prev_frames, uint32_t new_frames, double jitter_ms = 0.0) noexcept;

    uint32_t m_sample_rate;
    uint32_t m_packet_frames{144};
    uint32_t m_period_frames{480};
    uint32_t m_min_frames{960};
    uint32_t m_max_frames{1920};
    uint32_t m_target_frames{960};
    double m_stable_seconds{0.0};
    double m_dev_target_override_ms{0.0};
    AudioSessionClass m_session_class{AudioSessionClass::Startup};
    AudioTargetDecision m_last_decision{};
};

} // namespace duwn::audio
