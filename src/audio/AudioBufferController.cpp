#include "AudioBufferController.h"
#include "common/clock/MonotonicClock.h"
#include "common/metrics/Metrics.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace duwn::audio {

const char* AudioSessionClassName(AudioSessionClass cls) noexcept {
    switch (cls) {
    case AudioSessionClass::Startup: return "STARTUP";
    case AudioSessionClass::NormalSteady: return "NORMAL_STEADY";
    case AudioSessionClass::DiscontinuityRecovery: return "DISCONTINUITY_RECOVERY";
    case AudioSessionClass::EndpointChange: return "ENDPOINT_CHANGE";
    case AudioSessionClass::Reconnect: return "RECONNECT";
    default: return "UNKNOWN";
    }
}

double AudioBufferController::GetDevTargetOverrideMs() noexcept {
    wchar_t value[32]{};
    DWORD size = ::GetEnvironmentVariableW(L"DUWN_DEV_AUDIO_TARGET_MS", value,
                                            static_cast<DWORD>(std::size(value)));
    if (size > 0 && size < std::size(value)) {
        try {
            double ms = std::stod(value);
            if (ms > 0.0) return ms;
        } catch (...) {}
    }
    return 0.0;
}

AudioBufferController::AudioBufferController(uint32_t sample_rate) noexcept
    : m_sample_rate(sample_rate ? sample_rate : 48000) {
    m_dev_target_override_ms = GetDevTargetOverrideMs();
    RecordDecision("construct", 0, m_target_frames);
}

uint32_t AudioBufferController::ClampTarget(uint32_t frames) const noexcept {
    return std::clamp(frames, m_min_frames, m_max_frames);
}

uint32_t AudioBufferController::TargetFrames() const noexcept {
    if (m_dev_target_override_ms > 0.0) {
        uint32_t dev_frames = static_cast<uint32_t>(
            std::lround(m_dev_target_override_ms * static_cast<double>(m_sample_rate) / 1000.0));
        return std::max(m_period_frames, dev_frames);
    }
    return m_target_frames;
}

double AudioBufferController::TargetMs() const noexcept {
    return 1000.0 * static_cast<double>(TargetFrames()) / m_sample_rate;
}

void AudioBufferController::RecordDecision(const char* reason, uint32_t prev_frames,
                                          uint32_t new_frames, double jitter_ms) noexcept {
    auto& m = duwn::GlobalMetrics();
    m_last_decision.timestamp_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    m_last_decision.previous_target_ms = 1000.0 * prev_frames / m_sample_rate;
    m_last_decision.new_target_ms = 1000.0 * new_frames / m_sample_rate;
    m_last_decision.source_packet_duration_ms = m.audio_packet_duration_ms.load(std::memory_order_relaxed);
    m_last_decision.rtp_jitter_estimate_ms = jitter_ms;
    m_last_decision.largest_recent_gap_ms = m.audio_arrival_gap_max_ms.load(std::memory_order_relaxed);
    m_last_decision.wasapi_engine_period_ms = m.audio_engine_period_ms.load(std::memory_order_relaxed);
    m_last_decision.wasapi_padding_ms = m.audio_wasapi_padding_ms.load(std::memory_order_relaxed);
    m_last_decision.recent_underruns = m.audio_real_underruns.load(std::memory_order_relaxed);
    m_last_decision.recent_overruns = m.audio_ring_overrun_frames.load(std::memory_order_relaxed);
    m_last_decision.session_class = m_session_class;
    m_last_decision.servo_ppm = m.audio_servo_correction_ppm.load(std::memory_order_relaxed);
    m_last_decision.base_margin_ms = 1000.0 * m_min_frames / m_sample_rate;
    m_last_decision.jitter_margin_ms = jitter_ms * 3.0;
    m_last_decision.endpoint_margin_ms = 1000.0 * m_period_frames * 2 / m_sample_rate;
    m_last_decision.recovery_margin_ms = (m_session_class == AudioSessionClass::DiscontinuityRecovery)
        ? (1000.0 * m_period_frames / m_sample_rate) : 0.0;
    m_last_decision.clamp_min_ms = 1000.0 * m_min_frames / m_sample_rate;
    m_last_decision.clamp_max_ms = 1000.0 * m_max_frames / m_sample_rate;
    m_last_decision.final_target_ms = TargetMs();
    m_last_decision.reason = reason;
}

void AudioBufferController::Configure(uint32_t packet_frames,
                                      uint32_t engine_period_frames) noexcept {
    m_packet_frames = std::max(1u, packet_frames);
    m_period_frames = std::max(1u, engine_period_frames);
    m_min_frames = std::max(m_packet_frames * 2, m_period_frames * 2);
    // Emergency ceiling: 100ms at 48kHz
    m_max_frames = std::max(m_min_frames, m_sample_rate * 100 / 1000);
    uint32_t prev = m_target_frames;
    m_target_frames = ClampTarget(m_target_frames);
    if (m_target_frames < m_min_frames) m_target_frames = m_min_frames;
    m_stable_seconds = 0.0;
    RecordDecision("configure", prev, m_target_frames);
}

void AudioBufferController::ObserveJitter(double jitter_ms) noexcept {
    if (!std::isfinite(jitter_ms) || jitter_ms < 0.0) return;
    if (m_dev_target_override_ms > 0.0) return; // Frozen by dev override
    const uint32_t jitter_frames = static_cast<uint32_t>(
        std::ceil(jitter_ms * static_cast<double>(m_sample_rate) / 1000.0));
    const uint32_t required = std::max(m_min_frames, jitter_frames * 3);
    if (required > m_target_frames) {
        uint32_t prev = m_target_frames;
        m_target_frames = ClampTarget(required);
        m_stable_seconds = 0.0;
        RecordDecision("jitter_increase", prev, m_target_frames, jitter_ms);
    }
}

void AudioBufferController::OnUnderrun() noexcept {
    if (m_dev_target_override_ms > 0.0) return; // Frozen by dev override
    uint32_t prev = m_target_frames;
    m_target_frames = ClampTarget(m_target_frames + m_period_frames);
    m_stable_seconds = 0.0;
    RecordDecision("underrun_increase", prev, m_target_frames);
}

void AudioBufferController::AdvanceStable(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0 || m_target_frames <= m_min_frames) return;
    if (m_dev_target_override_ms > 0.0) return; // Frozen by dev override
    m_stable_seconds += seconds;
    // Decay after 6.0 seconds of stable stream
    if (m_stable_seconds >= 6.0) {
        uint32_t prev = m_target_frames;
        m_target_frames = std::max(m_min_frames, m_target_frames - m_packet_frames);
        m_stable_seconds = 0.0;
        RecordDecision("stable_decay", prev, m_target_frames);
    }
}

void AudioBufferController::Reset() noexcept {
    uint32_t prev = m_target_frames;
    m_target_frames = m_min_frames;
    m_stable_seconds = 0.0;
    m_dev_target_override_ms = GetDevTargetOverrideMs();
    RecordDecision("reset", prev, m_target_frames);
}

} // namespace duwn::audio
