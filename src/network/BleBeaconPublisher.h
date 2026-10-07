#pragma once
// BleBeaconPublisher.h — Native Windows WinRT Bluetooth LE advertisement publisher
// for Apple AirPlay Screen Mirroring discovery fallback (zero external Python dependency).

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>

namespace duwn::network {

struct BleBeaconConfig {
    std::string ipv4_address{};
    uint16_t airplay_port{7001}; // Apple AirPlay Type 0x09 Discovery Beacon advertises RTSP control endpoint (base + 1)
    bool     enable_beacon{true};
};

class BleBeaconPublisher {
public:
    BleBeaconPublisher() noexcept;
    ~BleBeaconPublisher();

    BleBeaconPublisher(const BleBeaconPublisher&) = delete;
    BleBeaconPublisher& operator=(const BleBeaconPublisher&) = delete;

    // Starts BLE advertisement if Bluetooth hardware and radio are available.
    // Gracefully degrades (no-op with warning log) if Bluetooth is absent or disabled.
    bool Start(const BleBeaconConfig& config) noexcept;
    void Stop() noexcept;

    bool IsAdvertising() const noexcept { return m_advertising.load(std::memory_order_relaxed); }
    bool IsSupported() const noexcept { return m_supported.load(std::memory_order_relaxed); }
    std::string GetStatusReason() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::atomic_bool      m_advertising{false};
    std::atomic_bool      m_supported{false};
};

} // namespace duwn::network
