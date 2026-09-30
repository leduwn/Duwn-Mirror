#pragma once
// AirPlayClientInfo.h — Structured client device identity and metadata
// parsed from RTSP handshake and SETUP binary plist.

#include <string>
#include <cstdint>

namespace duwn::airplay {

struct AirPlayClientInfo {
    std::wstring device_name;          // User-assigned name (e.g., L"iPhone của Minh", L"John's iPad")
    std::wstring model;                // Apple identifier (e.g., L"iPhone15,2", L"iPad13,18")
    std::wstring model_marketing_name; // Friendly commercial name (e.g., L"iPhone 14 Pro")
    std::wstring os_name{L"iOS"};      // "iOS", "iPadOS", "macOS"
    std::wstring os_version;           // OS version string (e.g., L"17.4.1", L"18.0")
    std::wstring source_version;       // AirPlay source version (e.g., L"660.8.1")
    std::wstring device_id;            // MAC address or unique device identifier
    std::wstring user_agent;           // RTSP User-Agent (e.g., L"AirPlay/660.8.1")
    std::wstring peer_address;         // Client IP address (e.g., L"192.168.1.105")
    uint16_t     peer_port{0};
    bool         is_exact_model_match{false};

    bool IsEmpty() const noexcept {
        return device_name.empty() && model.empty() && peer_address.empty();
    }

    void Clear() noexcept {
        device_name.clear();
        model.clear();
        model_marketing_name.clear();
        os_name = L"iOS";
        os_version.clear();
        source_version.clear();
        device_id.clear();
        user_agent.clear();
        peer_address.clear();
        peer_port = 0;
        is_exact_model_match = false;
    }
};

} // namespace duwn::airplay
