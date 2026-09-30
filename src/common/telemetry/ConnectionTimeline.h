#pragma once
// ConnectionTimeline.h — Unified monotonic connection timeline (C0 to C15).
// Uses steady MonotonicClock (QPC-backed). Never mixes wall-clock for durations.

#include <cstdint>
#include <string_view>
#include <string>
#include <array>
#include <mutex>

namespace duwn::telemetry {

enum class ConnectionMilestone : int {
    C0_ProcessStart = 0,             // C0: application process start
    C1_NetworkDiscoveryComplete,    // C1: network discovery complete
    C2_MediaInfrastructureReady,    // C2: D3D/media infrastructure ready
    C3_UxPlaySpawned,               // C3: UxPlay process spawned
    C4_UxPlaySocketsInitialized,    // C4: UxPlay server sockets initialized
    C4A_MdnsPublicationInitiated,   // C4A: mDNS/BLE advertisement publication initiated
    C4B_AdvertisementActiveInternal,// C4B: local advertisement subsystem verified active internally
    C5_MediaSessionReady,           // C5: application ready for client media session
    C5_AdvertisingReady = C5_MediaSessionReady, // Legacy alias
    C6_ControlConnectionAccepted,   // C6: iPhone control connection accepted
    C7_SessionSetupComplete,        // C7: AirPlay session setup complete
    C8_FirstVideoRtp,               // C8: first video RTP packet at Duwn
    C9_FirstCompleteAu,             // C9: first complete video AU
    C10_DecoderConfigured,          // C10: decoder configured for actual stream
    C11_FirstDecodedTexture,        // C11: first decoded texture
    C12_FirstPreviewPresent,        // C12: first Preview present
    C13_FirstOutputPresent,         // C13: first Output present if active
    C14_FirstAudioRtp,              // C14: first audio RTP
    C15_FirstWasapiWrite,           // C15: first real audio samples written to WASAPI
    _COUNT
};

constexpr const char* ConnectionMilestoneCode(ConnectionMilestone m) noexcept {
    switch (m) {
    case ConnectionMilestone::C0_ProcessStart:           return "C0";
    case ConnectionMilestone::C1_NetworkDiscoveryComplete: return "C1";
    case ConnectionMilestone::C2_MediaInfrastructureReady: return "C2";
    case ConnectionMilestone::C3_UxPlaySpawned:         return "C3";
    case ConnectionMilestone::C4_UxPlaySocketsInitialized: return "C4";
    case ConnectionMilestone::C4A_MdnsPublicationInitiated: return "C4A";
    case ConnectionMilestone::C4B_AdvertisementActiveInternal: return "C4B";
    case ConnectionMilestone::C5_MediaSessionReady:     return "C5";
    case ConnectionMilestone::C6_ControlConnectionAccepted: return "C6";
    case ConnectionMilestone::C7_SessionSetupComplete:  return "C7";
    case ConnectionMilestone::C8_FirstVideoRtp:         return "C8";
    case ConnectionMilestone::C9_FirstCompleteAu:       return "C9";
    case ConnectionMilestone::C10_DecoderConfigured:    return "C10";
    case ConnectionMilestone::C11_FirstDecodedTexture:  return "C11";
    case ConnectionMilestone::C12_FirstPreviewPresent:  return "C12";
    case ConnectionMilestone::C13_FirstOutputPresent:   return "C13";
    case ConnectionMilestone::C14_FirstAudioRtp:        return "C14";
    case ConnectionMilestone::C15_FirstWasapiWrite:     return "C15";
    default:                                            return "C?";
    }
}

constexpr const char* ConnectionMilestoneName(ConnectionMilestone m) noexcept {
    switch (m) {
    case ConnectionMilestone::C0_ProcessStart:           return "Process Start";
    case ConnectionMilestone::C1_NetworkDiscoveryComplete: return "Network Discovery Complete";
    case ConnectionMilestone::C2_MediaInfrastructureReady: return "Media Infrastructure Ready";
    case ConnectionMilestone::C3_UxPlaySpawned:         return "UxPlay Process Spawned";
    case ConnectionMilestone::C4_UxPlaySocketsInitialized: return "UxPlay Sockets Initialized";
    case ConnectionMilestone::C4A_MdnsPublicationInitiated: return "mDNS Publication Initiated";
    case ConnectionMilestone::C4B_AdvertisementActiveInternal: return "Advertisement Active (Internal)";
    case ConnectionMilestone::C5_MediaSessionReady:     return "Application Ready for Client Media Session";
    case ConnectionMilestone::C6_ControlConnectionAccepted: return "Control Connection Accepted";
    case ConnectionMilestone::C7_SessionSetupComplete:  return "Session Setup Complete";
    case ConnectionMilestone::C8_FirstVideoRtp:         return "First Video RTP Packet";
    case ConnectionMilestone::C9_FirstCompleteAu:       return "First Complete Video AU";
    case ConnectionMilestone::C10_DecoderConfigured:    return "Decoder Configured for Stream";
    case ConnectionMilestone::C11_FirstDecodedTexture:  return "First Decoded Texture";
    case ConnectionMilestone::C12_FirstPreviewPresent:  return "First Preview Present";
    case ConnectionMilestone::C13_FirstOutputPresent:   return "First Output Present";
    case ConnectionMilestone::C14_FirstAudioRtp:        return "First Audio RTP Packet";
    case ConnectionMilestone::C15_FirstWasapiWrite:     return "First Real Audio WASAPI Write";
    default:                                            return "Unknown Milestone";
    }
}

class ConnectionTimeline {
public:
    static ConnectionTimeline& Get() noexcept;

    // Record milestone occurrence timestamp (monotonic nanoseconds)
    void Record(ConnectionMilestone m, std::string_view detail = {}) noexcept;

    // Returns monotonic timestamp in nanoseconds, or 0 if not reached
    int64_t GetTimestampNs(ConnectionMilestone m) const noexcept;

    // Returns elapsed milliseconds between milestones, or -1.0 if either is unrecorded
    double GetElapsedMs(ConnectionMilestone from, ConnectionMilestone to) const noexcept;

    // User-facing startup metrics: DISCOVERY_READY_INTERNAL (C0 -> C4B) and MEDIA_SESSION_READY (C0 -> C5)
    double GetDiscoveryReadyInternalMs() const noexcept {
        return GetElapsedMs(ConnectionMilestone::C0_ProcessStart, ConnectionMilestone::C4B_AdvertisementActiveInternal);
    }
    double GetMediaSessionReadyMs() const noexcept {
        return GetElapsedMs(ConnectionMilestone::C0_ProcessStart, ConnectionMilestone::C5_MediaSessionReady);
    }

    // Reset stream session milestones (C6-C15) on disconnect / reconnect
    void ResetSession() noexcept;

    // Reset all milestones (full re-initialization)
    void ResetAll() noexcept;

    // Formats summary report string
    std::string FormatReport() const;

private:
    ConnectionTimeline() = default;

    mutable std::mutex m_mutex;
    std::array<int64_t, static_cast<size_t>(ConnectionMilestone::_COUNT)> m_timestamps_ns{};
    int64_t m_last_milestone_ns{0};
};

} // namespace duwn::telemetry
