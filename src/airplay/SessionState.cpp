#include "SessionState.h"
#include "common/logging/Logger.h"
#include <algorithm>

namespace duwn::airplay {

void SessionState::TransitionState(AirPlaySessionState next) noexcept {
    int prev_int = m_state.exchange(static_cast<int>(next), std::memory_order_acq_rel);
    auto prev    = static_cast<AirPlaySessionState>(prev_int);
    if (prev == next) return;

    DUWN_LOG_INFOF("SessionState", "AirPlaySessionState: {} -> {}",
                   AirPlaySessionStateToStringA(prev), AirPlaySessionStateToStringA(next));

    // Map to legacy phase
    SessionPhase leg = SessionPhase::Idle;
    switch (next) {
    case AirPlaySessionState::Idle:          leg = SessionPhase::Idle; break;
    case AirPlaySessionState::Connecting:    leg = SessionPhase::Connecting; break;
    case AirPlaySessionState::Connected:     leg = SessionPhase::Streaming; break;
    case AirPlaySessionState::Streaming:     leg = SessionPhase::Streaming; break;
    case AirPlaySessionState::Paused:        leg = SessionPhase::Streaming; break;
    case AirPlaySessionState::Disconnecting: leg = SessionPhase::Reconnecting; break;
    case AirPlaySessionState::Error:         leg = SessionPhase::AdvertisingFailed; break;
    }
    m_phase.store(static_cast<int>(leg), std::memory_order_release);

    if (m_state_callback) m_state_callback(prev, next);
}

void SessionState::Transition(SessionPhase next) noexcept {
    int prev_int = m_phase.exchange(static_cast<int>(next), std::memory_order_acq_rel);
    auto prev    = static_cast<SessionPhase>(prev_int);
    if (prev == next) return;

    DUWN_LOG_INFOF("SessionState", "Phase: {} → {}", PhaseString(prev), PhaseString(next));

    // Map to AirPlaySessionState
    AirPlaySessionState st = AirPlaySessionState::Idle;
    switch (next) {
    case SessionPhase::Idle:              st = AirPlaySessionState::Idle; break;
    case SessionPhase::Advertising:       st = AirPlaySessionState::Idle; break;
    case SessionPhase::Connecting:        st = AirPlaySessionState::Connecting; break;
    case SessionPhase::Streaming:         st = AirPlaySessionState::Streaming; break;
    case SessionPhase::Reconnecting:      st = AirPlaySessionState::Disconnecting; break;
    case SessionPhase::Stopping:          st = AirPlaySessionState::Disconnecting; break;
    case SessionPhase::SidecarMissing:    st = AirPlaySessionState::Error; break;
    case SessionPhase::AdvertisingFailed: st = AirPlaySessionState::Error; break;
    }
    m_state.store(static_cast<int>(st), std::memory_order_release);

    if (m_phase_callback) m_phase_callback(prev, next);
}

void SessionState::SetClientInfo(const AirPlayClientInfo& info) noexcept {
    {
        std::lock_guard lock(m_client_mutex);
        m_client_info = info;
    }
    if (m_client_callback) {
        m_client_callback(info);
    }
}

AirPlayClientInfo SessionState::GetClientInfo() const noexcept {
    std::lock_guard lock(m_client_mutex);
    return m_client_info;
}

void SessionState::ClearClientInfo() noexcept {
    {
        std::lock_guard lock(m_client_mutex);
        m_client_info.Clear();
    }
    if (m_client_callback) {
        m_client_callback(AirPlayClientInfo{});
    }
}

void SessionState::EvaluateSessionLiveness(int64_t now_ns) noexcept {
    auto cur = CurrentState();
    if (cur != AirPlaySessionState::Streaming && cur != AirPlaySessionState::Paused && cur != AirPlaySessionState::Connected) {
        return;
    }

    int64_t last_ctrl  = m_last_control_ns.load(std::memory_order_relaxed);
    int64_t last_vid   = m_last_video_ns.load(std::memory_order_relaxed);
    int64_t last_aud   = m_last_audio_ns.load(std::memory_order_relaxed);
    int64_t last_fps   = m_last_fps_ns.load(std::memory_order_relaxed);

    constexpr int64_t k1_5s_ns = 1'500'000'000LL;
    constexpr int64_t k2s_ns   = 2'000'000'000LL;
    constexpr int64_t k5s_ns   = 5'000'000'000LL;
    constexpr int64_t k6s_ns   = 6'000'000'000LL;

    bool control_active = (last_ctrl > 0 && (now_ns - last_ctrl) < k5s_ns);
    bool audio_active   = (last_aud > 0 && (now_ns - last_aud) < k2s_ns);
    bool fps_active     = (last_fps > 0 && (now_ns - last_fps) < k5s_ns);
    bool video_active   = (last_vid > 0 && (now_ns - last_vid) < k1_5s_ns);

    // If current is Streaming but video stopped arriving (> 1.5s):
    if (cur == AirPlaySessionState::Streaming) {
        if (!video_active && (control_active || audio_active || fps_active)) {
            // Static screen — pause instead of disconnect!
            TransitionState(AirPlaySessionState::Paused);
            return;
        }
    } else if (cur == AirPlaySessionState::Paused) {
        if (video_active) {
            // Video resumed
            TransitionState(AirPlaySessionState::Streaming);
            return;
        }
    }

    // Full disconnect check: if ALL activity ceases for > 6s
    int64_t latest_any = std::max({last_ctrl, last_vid, last_aud, last_fps});
    if (latest_any > 0 && (now_ns - latest_any) >= k6s_ns) {
        DUWN_LOG_WARN("SessionState", "All session activity timed out (> 6s) — disconnecting");
        TransitionState(AirPlaySessionState::Disconnecting);
    }
}

} // namespace duwn::airplay
