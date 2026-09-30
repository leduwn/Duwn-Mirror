#pragma once
// SessionState.h — Observable state machine for the AirPlay session lifecycle.
// Implements authoritative AirPlaySessionState and multi-source activity tracking.

#include "AirPlaySessionState.h"
#include "AirPlayClientInfo.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace duwn::airplay {

// Retain legacy SessionPhase for backward compatibility where needed,
// with 1:1 mapping to AirPlaySessionState.
enum class SessionPhase : int {
    Idle               = 0,
    Advertising        = 1,
    Connecting         = 2,
    Streaming          = 3,
    Reconnecting       = 4,
    Stopping           = 5,
    SidecarMissing     = 6,
    AdvertisingFailed  = 7,
};

constexpr const char* PhaseString(SessionPhase p) noexcept {
    switch (p) {
        case SessionPhase::Idle:              return "Idle";
        case SessionPhase::Advertising:       return "Advertising";
        case SessionPhase::Connecting:        return "Connecting";
        case SessionPhase::Streaming:         return "Streaming";
        case SessionPhase::Reconnecting:      return "Reconnecting";
        case SessionPhase::Stopping:          return "Stopping";
        case SessionPhase::SidecarMissing:    return "SidecarMissing";
        case SessionPhase::AdvertisingFailed: return "AdvertisingFailed";
    }
    return "Unknown";
}

// Callback types
using PhaseCallback = std::function<void(SessionPhase prev, SessionPhase next)>;
using StateCallback = std::function<void(AirPlaySessionState prev, AirPlaySessionState next)>;
using ClientInfoCallback = std::function<void(const AirPlayClientInfo&)>;

class SessionState {
public:
    SessionState() noexcept = default;

    AirPlaySessionState CurrentState() const noexcept {
        return static_cast<AirPlaySessionState>(m_state.load(std::memory_order_acquire));
    }

    SessionPhase Current() const noexcept {
        return static_cast<SessionPhase>(m_phase.load(std::memory_order_acquire));
    }

    void Transition(SessionPhase next) noexcept;
    void TransitionState(AirPlaySessionState next) noexcept;

    void SetCallback(PhaseCallback cb) noexcept { m_phase_callback = std::move(cb); }
    void SetStateCallback(StateCallback cb) noexcept { m_state_callback = std::move(cb); }
    void SetClientInfoCallback(ClientInfoCallback cb) noexcept { m_client_callback = std::move(cb); }

    // Client metadata
    void SetClientInfo(const AirPlayClientInfo& info) noexcept;
    AirPlayClientInfo GetClientInfo() const noexcept;
    void ClearClientInfo() noexcept;

    // Independent activity timestamps (Item 15)
    void RecordControlActivity(int64_t now_ns) noexcept {
        m_last_control_ns.store(now_ns, std::memory_order_relaxed);
    }
    void RecordVideoPacket(int64_t now_ns) noexcept {
        m_last_video_ns.store(now_ns, std::memory_order_relaxed);
    }
    void RecordAudioPacket(int64_t now_ns) noexcept {
        m_last_audio_ns.store(now_ns, std::memory_order_relaxed);
    }
    void RecordClientFps(int64_t now_ns) noexcept {
        m_last_fps_ns.store(now_ns, std::memory_order_relaxed);
    }

    int64_t LastControlNs() const noexcept { return m_last_control_ns.load(std::memory_order_relaxed); }
    int64_t LastVideoNs() const noexcept   { return m_last_video_ns.load(std::memory_order_relaxed); }
    int64_t LastAudioNs() const noexcept   { return m_last_audio_ns.load(std::memory_order_relaxed); }
    int64_t LastFpsNs() const noexcept     { return m_last_fps_ns.load(std::memory_order_relaxed); }

    // Evaluates static screen vs true disconnect to prevent false disconnects
    void EvaluateSessionLiveness(int64_t now_ns) noexcept;

private:
    std::atomic<int>   m_state{static_cast<int>(AirPlaySessionState::Idle)};
    std::atomic<int>   m_phase{static_cast<int>(SessionPhase::Idle)};

    std::atomic<int64_t> m_last_control_ns{0};
    std::atomic<int64_t> m_last_video_ns{0};
    std::atomic<int64_t> m_last_audio_ns{0};
    std::atomic<int64_t> m_last_fps_ns{0};

    PhaseCallback      m_phase_callback;
    StateCallback      m_state_callback;
    ClientInfoCallback m_client_callback;

    mutable std::mutex m_client_mutex;
    AirPlayClientInfo  m_client_info;
};

} // namespace duwn::airplay
