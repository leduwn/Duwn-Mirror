#pragma once
// DirectPacket.h — Binary packet header and serialization for Duwn Direct Mode.
// Validates lengths, fragment ranges, and rejects malformed packets safely.

#include "DirectProtocol.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace duwn::direct {

// Stream IDs
enum class DirectStreamId : uint16_t {
    Unknown  = 0,
    Video    = 1,
    Audio    = 2,
    Control  = 3,
    Metadata = 4
};

// Payload Types
enum class DirectPayloadType : uint8_t {
    Unknown     = 0,
    VideoSlice  = 1,
    VideoConfig = 2, // SPS/PPS/VPS
    AudioChunk  = 3,
    ControlMsg  = 4
};

// Packet Flags
namespace DirectPacketFlags {
    inline constexpr uint16_t None       = 0x0000;
    inline constexpr uint16_t KeyFrame   = 0x0001; // Frame contains IDR / keyframe
    inline constexpr uint16_t LastPacket = 0x0002; // Final packet of the frame
    inline constexpr uint16_t EndOfFrame = LastPacket; // Alias for final packet
    inline constexpr uint16_t Config     = 0x0004; // Parameter set / config payload
    inline constexpr uint16_t Resent     = 0x0008; // Retransmitted packet
}

#pragma pack(push, 1)
// Direct wire packet header (36 bytes fixed size)
struct DirectPacketHeader {
    uint16_t protocol_major{1};
    uint16_t protocol_minor{0};
    uint32_t session_id{0};
    uint16_t stream_id{static_cast<uint16_t>(DirectStreamId::Video)};
    uint16_t flags{DirectPacketFlags::None};
    uint32_t frame_id{0};
    uint32_t packet_index{0};
    uint32_t packet_count{1};
    uint64_t source_timestamp_ns{0};
    uint8_t  payload_type{static_cast<uint8_t>(DirectPayloadType::VideoSlice)};
    uint8_t  reserved[3]{0, 0, 0};

    static constexpr size_t kHeaderSize = 36;
    static constexpr uint32_t kMaxPacketCount = 4096; // Max fragments per frame (sanity limit)
    static constexpr size_t kMaxPayloadSize = 65507;  // Max UDP payload size

    // Checks header semantic validity.
    bool IsValid(uint16_t expected_major = kCurrentDirectProtocolVersion.major) const noexcept {
        if (protocol_major != expected_major) return false;
        if (packet_count == 0 || packet_count > kMaxPacketCount) return false;
        if (packet_index >= packet_count) return false;
        if (stream_id == 0) return false;
        return true;
    }

    // Serializes header to destination buffer.
    bool Serialize(uint8_t* dst, size_t max_len = kHeaderSize) const noexcept {
        if (!dst || max_len < kHeaderSize) return false;
        std::memcpy(dst, this, kHeaderSize);
        return true;
    }

    // Deserializes header from source buffer.
    static bool Deserialize(const uint8_t* src, size_t len, DirectPacketHeader& out_header) noexcept {
        if (!src || len < kHeaderSize) return false;
        std::memcpy(&out_header, src, kHeaderSize);
        return out_header.IsValid();
    }
};
#pragma pack(pop)

static_assert(sizeof(DirectPacketHeader) == 36, "DirectPacketHeader must be exactly 36 bytes");

} // namespace duwn::direct
