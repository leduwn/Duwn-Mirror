#pragma once
// DirectProtocol.h — Duwn Direct mode protocol constants, versioning, and enums.
// Pure capability and protocol definitions; no device-model rules or platform hacks.

#include <cstdint>
#include <string_view>
#include <string>

namespace duwn::direct {

// Direct Protocol Version (Major / Minor).
// Forward-compatibility rules:
// - Major mismatch: connection rejected, fallback to AirPlay.
// - Minor forward compatibility: newer peer minor version accepted; unknown optional fields ignored.
struct DirectProtocolVersion {
    uint16_t major{1};
    uint16_t minor{0};

    constexpr bool operator==(const DirectProtocolVersion& o) const noexcept {
        return major == o.major && minor == o.minor;
    }

    constexpr bool operator!=(const DirectProtocolVersion& o) const noexcept {
        return !(*this == o);
    }

    // Major must match; minor may be >= local minor (forward-compatible).
    constexpr bool IsCompatibleWith(const DirectProtocolVersion& peer) const noexcept {
        return major == peer.major;
    }

    constexpr bool HasMajorMismatch(const DirectProtocolVersion& peer) const noexcept {
        return major != peer.major;
    }

    std::string ToString() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }
};

inline constexpr DirectProtocolVersion kCurrentDirectProtocolVersion{1, 0};

// Video codec candidates for Direct Mode.
enum class DirectVideoCodec : uint8_t {
    None = 0,
    H264 = 1,
    HEVC = 2,
    AV1  = 3
};

constexpr std::string_view DirectVideoCodecToString(DirectVideoCodec codec) noexcept {
    switch (codec) {
    case DirectVideoCodec::H264: return "H264";
    case DirectVideoCodec::HEVC: return "HEVC";
    case DirectVideoCodec::AV1:  return "AV1";
    default:                     return "None";
    }
}

// Low-latency transport protocols supported by Direct Mode.
enum class DirectTransportType : uint8_t {
    None           = 0,
    DirectDatagram = 1, // Custom UDP low-latency datagram transport
    DirectQuic     = 2, // QUIC datagram / stream hybrid
    DirectTcp      = 3  // Fallback TCP framing
};

constexpr std::string_view DirectTransportTypeToString(DirectTransportType transport) noexcept {
    switch (transport) {
    case DirectTransportType::DirectDatagram: return "DirectDatagram";
    case DirectTransportType::DirectQuic:     return "DirectQuic";
    case DirectTransportType::DirectTcp:      return "DirectTcp";
    default:                                  return "None";
    }
}

// High-level quality policy targets.
// BALANCED: minimize latency while preventing unacceptable compression blur.
enum class DirectQualityPolicy : uint8_t {
    LowestLatency = 0,
    Balanced      = 1,
    HighQuality   = 2
};

constexpr std::string_view DirectQualityPolicyToString(DirectQualityPolicy policy) noexcept {
    switch (policy) {
    case DirectQualityPolicy::LowestLatency: return "LOWEST_LATENCY";
    case DirectQualityPolicy::Balanced:      return "BALANCED";
    case DirectQualityPolicy::HighQuality:   return "HIGH_QUALITY";
    default:                                 return "UNKNOWN";
    }
}

// Latency target modes.
enum class DirectLatencyMode : uint8_t {
    UltraLowLatency = 0,
    LowLatency      = 1,
    NormalLatency   = 2
};

constexpr std::string_view DirectLatencyModeToString(DirectLatencyMode mode) noexcept {
    switch (mode) {
    case DirectLatencyMode::UltraLowLatency: return "UltraLowLatency";
    case DirectLatencyMode::LowLatency:      return "LowLatency";
    case DirectLatencyMode::NormalLatency:   return "NormalLatency";
    default:                                 return "Unknown";
    }
}

// 2D Resolution representation for Direct Mode geometry.
struct DirectResolution {
    uint32_t width{0};
    uint32_t height{0};

    constexpr bool operator==(const DirectResolution& o) const noexcept {
        return width == o.width && height == o.height;
    }

    constexpr bool operator!=(const DirectResolution& o) const noexcept {
        return !(*this == o);
    }

    constexpr bool IsEmpty() const noexcept {
        return width == 0 || height == 0;
    }

    constexpr uint64_t TotalPixels() const noexcept {
        return static_cast<uint64_t>(width) * height;
    }

    std::string ToString() const {
        return std::to_string(width) + "x" + std::to_string(height);
    }
};

// Direct Feature Gate
// Direct Mode remains development / experimental until real Apple build and physical device validation.
// AirPlay remains the production default.
struct DirectFeatureGate {
    static constexpr bool kDirectModeProductionEnabled = false;
    static constexpr bool kAirPlayIsProductionDefault = true;

    static constexpr bool IsDirectModePermitted(bool developer_override = false) noexcept {
        return kDirectModeProductionEnabled || developer_override;
    }
};

} // namespace duwn::direct
