#pragma once

#include <cstdint>
#include <string>

namespace duwn::wired {

enum class WiredPhase {
    UsbNotConnected,
    AppleRuntimeMissing,
    AppleServiceStopped,
    WaitingForUsbNetwork,
    UsbNetworkReady
};

struct WiredSnapshot {
    WiredPhase phase{WiredPhase::UsbNotConnected};
    uint32_t usb_interface_count{0};
    bool apple_runtime_present{false};
    bool apple_service_running{false};
    std::wstring device_name;
    std::wstring network_ipv4;
    std::wstring network_guid;
    uint32_t network_if_index{0};
    uint64_t network_luid{0};
    uint8_t network_prefix{0};
    bool network_up{false};
    std::wstring rtsp_local_endpoint;
    std::wstring rtsp_peer_endpoint;
    // Windows PnP detection cannot establish an iOS lockdown trust state.
    // Trust, model, version, battery and transport speed remain unknown until
    // a real Apple device bridge is available.
};

WiredPhase ClassifyWiredPhase(uint32_t usb_interface_count,
                              bool runtime_present, bool service_running) noexcept;

class WiredDeviceManager {
public:
    WiredSnapshot Probe(uint32_t airplay_pid = 0) const noexcept;
};

} // namespace duwn::wired
