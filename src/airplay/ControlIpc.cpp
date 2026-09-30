#include "ControlIpc.h"
#include <format>
#include <cstring>
#include <charconv>

namespace duwn::airplay {

namespace {

// Lightweight JSON field extraction helpers
std::string ExtractStringField(std::string_view json, std::string_view key) {
    std::string needle = std::format("\"{}\":", key);
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = std::format("\"{}\" :", key);
        pos = json.find(needle);
        if (pos == std::string_view::npos) return {};
    }
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    if (pos < json.length() && json[pos] == '"') {
        ++pos;
        size_t end = json.find('"', pos);
        if (end != std::string_view::npos) {
            return std::string(json.substr(pos, end - pos));
        }
    }
    return {};
}

int64_t ExtractIntField(std::string_view json, std::string_view key, int64_t def = 0) {
    std::string needle = std::format("\"{}\":", key);
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = std::format("\"{}\" :", key);
        pos = json.find(needle);
        if (pos == std::string_view::npos) return def;
    }
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    size_t end = pos;
    while (end < json.length() && (json[end] == '-' || (json[end] >= '0' && json[end] <= '9'))) {
        ++end;
    }
    if (end > pos) {
        int64_t val = def;
        auto res = std::from_chars(json.data() + pos, json.data() + end, val);
        if (res.ec == std::errc()) return val;
    }
    return def;
}

double ExtractDoubleField(std::string_view json, std::string_view key, double def = 0.0) {
    std::string needle = std::format("\"{}\":", key);
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = std::format("\"{}\" :", key);
        pos = json.find(needle);
        if (pos == std::string_view::npos) return def;
    }
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    size_t end = pos;
    while (end < json.length() && (json[end] == '-' || json[end] == '.' || (json[end] >= '0' && json[end] <= '9'))) {
        ++end;
    }
    if (end > pos) {
        std::string s(json.substr(pos, end - pos));
        try {
            return std::stod(s);
        } catch (...) {}
    }
    return def;
}

} // namespace

std::string ControlIpcCodec::ToJson(const ControlMessage& msg) noexcept {
    switch (msg.type) {
    case ControlMessageType::SessionStart:
        return std::format(
            "{{\"type\":\"SESSION_START\",\"device_name\":\"{}\",\"model\":\"{}\",\"os_name\":\"{}\","
            "\"os_version\":\"{}\",\"source_version\":\"{}\",\"device_id\":\"{}\",\"user_agent\":\"{}\","
            "\"client_ip\":\"{}\",\"client_port\":{}}}",
            msg.device_name, msg.model, msg.os_name, msg.os_version,
            msg.source_version, msg.device_id, msg.user_agent,
            msg.client_ip, msg.client_port
        );

    case ControlMessageType::SessionStop:
        return std::format(
            "{{\"type\":\"SESSION_STOP\",\"reason\":\"{}\"}}",
            msg.stop_reason
        );

    case ControlMessageType::StreamMetadata:
        return std::format(
            "{{\"type\":\"STREAM_METADATA\",\"width\":{},\"height\":{},\"fps\":{:.2f},"
            "\"codec\":\"{}\",\"audio_format\":\"{}\",\"audio_sample_rate\":{},\"audio_channels\":{}}}",
            msg.width, msg.height, msg.fps, msg.codec, msg.audio_format,
            msg.audio_sample_rate, msg.audio_channels
        );

    case ControlMessageType::Heartbeat:
        return std::format(
            "{{\"type\":\"HEARTBEAT\",\"timestamp_ms\":{},\"video_packets\":{},"
            "\"audio_packets\":{},\"client_fps\":{:.2f}}}",
            msg.timestamp_ms, msg.video_packets, msg.audio_packets, msg.client_fps
        );

    case ControlMessageType::Error:
        return std::format(
            "{{\"type\":\"ERROR\",\"code\":{},\"message\":\"{}\"}}",
            msg.error_code, msg.error_message
        );

    default:
        return "{\"type\":\"UNKNOWN\"}";
    }
}

bool ControlIpcCodec::FromJson(std::string_view json_str, ControlMessage& out_msg) noexcept {
    std::string type_str = ExtractStringField(json_str, "type");
    if (type_str.empty()) return false;

    if (type_str == "SESSION_START") {
        out_msg.type = ControlMessageType::SessionStart;
        out_msg.device_name    = ExtractStringField(json_str, "device_name");
        out_msg.model          = ExtractStringField(json_str, "model");
        out_msg.os_name        = ExtractStringField(json_str, "os_name");
        if (out_msg.os_name.empty()) out_msg.os_name = "iOS";
        out_msg.os_version     = ExtractStringField(json_str, "os_version");
        out_msg.source_version = ExtractStringField(json_str, "source_version");
        out_msg.device_id      = ExtractStringField(json_str, "device_id");
        out_msg.user_agent     = ExtractStringField(json_str, "user_agent");
        out_msg.client_ip      = ExtractStringField(json_str, "client_ip");
        out_msg.client_port    = static_cast<uint16_t>(ExtractIntField(json_str, "client_port", 0));
        return true;
    }

    if (type_str == "SESSION_STOP") {
        out_msg.type = ControlMessageType::SessionStop;
        out_msg.stop_reason = ExtractStringField(json_str, "reason");
        return true;
    }

    if (type_str == "STREAM_METADATA") {
        out_msg.type = ControlMessageType::StreamMetadata;
        out_msg.width             = static_cast<uint32_t>(ExtractIntField(json_str, "width", 0));
        out_msg.height            = static_cast<uint32_t>(ExtractIntField(json_str, "height", 0));
        out_msg.fps               = ExtractDoubleField(json_str, "fps", 0.0);
        out_msg.codec             = ExtractStringField(json_str, "codec");
        out_msg.audio_format      = ExtractStringField(json_str, "audio_format");
        out_msg.audio_sample_rate = static_cast<uint32_t>(ExtractIntField(json_str, "audio_sample_rate", 0));
        out_msg.audio_channels    = static_cast<uint8_t>(ExtractIntField(json_str, "audio_channels", 0));
        return true;
    }

    if (type_str == "HEARTBEAT") {
        out_msg.type = ControlMessageType::Heartbeat;
        out_msg.timestamp_ms  = ExtractIntField(json_str, "timestamp_ms", 0);
        out_msg.video_packets = static_cast<uint64_t>(ExtractIntField(json_str, "video_packets", 0));
        out_msg.audio_packets = static_cast<uint64_t>(ExtractIntField(json_str, "audio_packets", 0));
        out_msg.client_fps    = ExtractDoubleField(json_str, "client_fps", 0.0);
        return true;
    }

    if (type_str == "ERROR") {
        out_msg.type = ControlMessageType::Error;
        out_msg.error_code    = static_cast<int32_t>(ExtractIntField(json_str, "code", 0));
        out_msg.error_message = ExtractStringField(json_str, "message");
        return true;
    }

    return false;
}

std::vector<uint8_t> ControlIpcCodec::EncodeFrame(const ControlMessage& msg) noexcept {
    std::string json = ToJson(msg);
    uint32_t len = static_cast<uint32_t>(json.length());
    std::vector<uint8_t> frame(4 + len);
    // 4-byte LE length
    frame[0] = static_cast<uint8_t>(len & 0xFF);
    frame[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
    frame[2] = static_cast<uint8_t>((len >> 16) & 0xFF);
    frame[3] = static_cast<uint8_t>((len >> 24) & 0xFF);
    std::memcpy(frame.data() + 4, json.data(), len);
    return frame;
}

bool ControlIpcCodec::DecodeFrame(const uint8_t* payload, size_t size, ControlMessage& out_msg) noexcept {
    if (!payload || size == 0) return false;
    std::string_view json(reinterpret_cast<const char*>(payload), size);
    return FromJson(json, out_msg);
}

} // namespace duwn::airplay
