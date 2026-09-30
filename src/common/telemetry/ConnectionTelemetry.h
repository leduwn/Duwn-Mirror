#pragma once
// ConnectionTelemetry.h — Tracks and logs the 14 AirPlay connection lifecycle phases.
// Lives in duwn-common so network, airplay, video, and app layers can record phases.

#include <cstdint>
#include <string_view>
#include <array>
#include <atomic>
#include <mutex>

namespace duwn::airplay {

enum class ConnectionPhase : int {
    SidecarStarted       = 1,
    MdnsAdvertised       = 2,
    TcpClientConnected   = 3,
    RtspStarted          = 4,
    PairingStarted       = 5,
    PairingComplete      = 6,
    VideoSetupReceived   = 7,
    AudioSetupReceived   = 8,
    VideoRtpStarted      = 9,
    AudioRtpStarted      = 10,
    FirstH264Au          = 11,
    DecoderCreated       = 12,
    FirstDecodedFrame    = 13,
    FirstPresentedFrame  = 14,
    _COUNT               = 15
};

constexpr const char* ConnectionPhaseName(ConnectionPhase p) noexcept {
    switch (p) {
    case ConnectionPhase::SidecarStarted:      return "Sidecar Started";
    case ConnectionPhase::MdnsAdvertised:      return "mDNS Advertised";
    case ConnectionPhase::TcpClientConnected:  return "TCP Client Connected";
    case ConnectionPhase::RtspStarted:         return "RTSP Started";
    case ConnectionPhase::PairingStarted:      return "Pairing Started";
    case ConnectionPhase::PairingComplete:     return "Pairing Complete";
    case ConnectionPhase::VideoSetupReceived:  return "Video Setup Received";
    case ConnectionPhase::AudioSetupReceived:  return "Audio Setup Received";
    case ConnectionPhase::VideoRtpStarted:     return "Video RTP Started";
    case ConnectionPhase::AudioRtpStarted:     return "Audio RTP Started";
    case ConnectionPhase::FirstH264Au:         return "First H.264 Access Unit";
    case ConnectionPhase::DecoderCreated:      return "Decoder Created";
    case ConnectionPhase::FirstDecodedFrame:   return "First Decoded Frame";
    case ConnectionPhase::FirstPresentedFrame: return "First Presented Frame";
    default:                                   return "Unknown";
    }
}

class ConnectionTelemetry {
public:
    static ConnectionTelemetry& Get() noexcept;

    // Record the timestamp of a phase transition and emit structured log.
    void RecordPhase(ConnectionPhase phase, std::string_view detail = {}) noexcept;

    // Returns timestamp in nanoseconds of the phase, or 0 if not yet reached.
    int64_t GetPhaseTimestampNs(ConnectionPhase phase) const noexcept;

    // Returns true if all 14 phases have succeeded for this connection.
    bool IsFullyConnected() const noexcept;

    // Resets timestamps for a new session.
    void Reset() noexcept;

private:
    ConnectionTelemetry() = default;

    mutable std::mutex m_mutex;
    std::array<int64_t, static_cast<size_t>(ConnectionPhase::_COUNT)> m_phase_timestamps_ns{};
    int64_t m_session_start_ns{0};
    int64_t m_last_phase_ns{0};
    ConnectionPhase m_current_phase{ConnectionPhase::SidecarStarted};
};

} // namespace duwn::airplay
