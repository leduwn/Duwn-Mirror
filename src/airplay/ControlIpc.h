#pragma once
// ControlIpc.h — Dedicated low-rate control channel protocol between DUWN core and AirPlay sidecar.
// Wire format: 4-byte little-endian length prefix followed by UTF-8 JSON.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace duwn::airplay {

enum class ControlMessageType {
    Unknown,
    SessionStart,
    SessionStop,
    StreamMetadata,
    Heartbeat,
    Error
};

struct ControlMessage {
    ControlMessageType type{ControlMessageType::Unknown};

    // SESSION_START payload
    std::string device_name;
    std::string model;
    std::string os_name{"iOS"};
    std::string os_version;
    std::string source_version;
    std::string device_id;
    std::string user_agent;
    std::string client_ip;
    uint16_t    client_port{0};

    // SESSION_STOP payload
    std::string stop_reason; // "teardown", "timeout", "client_dropped"

    // STREAM_METADATA payload
    uint32_t    width{0};
    uint32_t    height{0};
    double      fps{0.0};
    std::string codec;       // "h264", "h265"
    std::string audio_format;// "aac-eld", "aac", "alac", "pcm"
    uint32_t    audio_sample_rate{0};
    uint8_t     audio_channels{0};

    // HEARTBEAT payload
    int64_t     timestamp_ms{0};
    uint64_t    video_packets{0};
    uint64_t    audio_packets{0};
    double      client_fps{0.0};

    // ERROR payload
    int32_t     error_code{0};
    std::string error_message;
};

class ControlIpcCodec {
public:
    // Serializes ControlMessage to UTF-8 JSON string
    static std::string ToJson(const ControlMessage& msg) noexcept;

    // Deserializes UTF-8 JSON string to ControlMessage
    static bool FromJson(std::string_view json_str, ControlMessage& out_msg) noexcept;

    // Encodes message with 4-byte LE length prefix
    static std::vector<uint8_t> EncodeFrame(const ControlMessage& msg) noexcept;

    // Decodes frame payload (excluding 4-byte length prefix) into ControlMessage
    static bool DecodeFrame(const uint8_t* payload, size_t size, ControlMessage& out_msg) noexcept;
};

} // namespace duwn::airplay
