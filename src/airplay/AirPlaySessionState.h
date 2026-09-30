#pragma once
// AirPlaySessionState.h — Authoritative lifecycle state machine for AirPlay sessions.

#include <string_view>

namespace duwn::airplay {

enum class AirPlaySessionState : int {
    Idle          = 0, // Server listening on 7000/7100, mDNS broadcasting, no client connected
    Connecting    = 1, // RTSP handshake initiated (OPTIONS / ANNOUNCE received)
    Connected     = 2, // RTSP SETUP completed, transport parameters negotiated, client metadata received
    Streaming     = 3, // Active RTP media flow (video and/or audio packets arriving within timeout)
    Paused        = 4, // Stream active but client paused (e.g. video playback paused, screen static)
    Disconnecting = 5, // TEARDOWN received or ping timeout expired
    Error         = 6  // Port conflict, socket error, or sidecar crash
};

constexpr std::wstring_view AirPlaySessionStateToString(AirPlaySessionState state) noexcept {
    switch (state) {
    case AirPlaySessionState::Idle:          return L"Idle";
    case AirPlaySessionState::Connecting:    return L"Connecting";
    case AirPlaySessionState::Connected:     return L"Connected";
    case AirPlaySessionState::Streaming:     return L"Streaming";
    case AirPlaySessionState::Paused:        return L"Paused";
    case AirPlaySessionState::Disconnecting: return L"Disconnecting";
    case AirPlaySessionState::Error:         return L"Error";
    }
    return L"Unknown";
}

constexpr std::string_view AirPlaySessionStateToStringA(AirPlaySessionState state) noexcept {
    switch (state) {
    case AirPlaySessionState::Idle:          return "Idle";
    case AirPlaySessionState::Connecting:    return "Connecting";
    case AirPlaySessionState::Connected:     return "Connected";
    case AirPlaySessionState::Streaming:     return "Streaming";
    case AirPlaySessionState::Paused:        return "Paused";
    case AirPlaySessionState::Disconnecting: return "Disconnecting";
    case AirPlaySessionState::Error:         return "Error";
    }
    return "Unknown";
}

} // namespace duwn::airplay
