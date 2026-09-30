#include "BleBeaconPublisher.h"
#include "common/logging/Logger.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>

#include <mutex>
#include <format>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ws2_32.lib")

namespace duwn::network {

struct BleBeaconPublisher::Impl {
    std::mutex mutex;
    std::string status_reason{"Not started"};
    BleBeaconConfig current_config{};
    winrt::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementPublisher publisher{nullptr};
    winrt::event_token status_token{};
};

BleBeaconPublisher::BleBeaconPublisher() noexcept
    : m_impl(std::make_unique<Impl>())
{
}

BleBeaconPublisher::~BleBeaconPublisher() {
    Stop();
}

std::string BleBeaconPublisher::GetStatusReason() const noexcept {
    if (!m_impl) return "Uninitialized";
    std::lock_guard lock(m_impl->mutex);
    return m_impl->status_reason;
}

bool BleBeaconPublisher::Start(const BleBeaconConfig& config) noexcept {
    if (!config.enable_beacon) {
        std::lock_guard lock(m_impl->mutex);
        m_impl->status_reason = "Disabled by configuration";
        return false;
    }

    {
        std::lock_guard lock(m_impl->mutex);
        if (m_advertising.load(std::memory_order_relaxed)) {
            if (m_impl->current_config.ipv4_address == config.ipv4_address &&
                m_impl->current_config.airplay_port == config.airplay_port) {
                return true;
            }
        }
    }

    // Stop existing advertisement if IP or port changed
    Stop();

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
        using namespace winrt::Windows::Storage::Streams;

        BluetoothLEAdvertisementPublisher pub;

        // Apple Inc. Bluetooth SIG Assigned Company Identifier: 0x004C
        BluetoothLEManufacturerData apple_data;
        apple_data.CompanyId(0x004C);

        // Apple AirPlay Screen Mirroring Discovery Beacon Payload (UxPlay reference specification):
        // [0x09] = Apple Data Unit Type 9 (AirPlay Service Discovery)
        // [0x08] = Data Unit Length (8 bytes follow: flags, seed, IPv4[4], Port[2])
        // [0x13] = Flags (0001 0011 binary: AirPlay Screen Mirroring target)
        // [0x30] = Seed (0x30)
        // IPv4   = 4 bytes network byte order
        // Port   = 2 bytes big-endian
        DataWriter writer;
        writer.WriteByte(0x09);
        writer.WriteByte(0x08);
        writer.WriteByte(0x13);
        writer.WriteByte(0x30);

        in_addr addr{};
        if (config.ipv4_address.empty() || ::inet_pton(AF_INET, config.ipv4_address.c_str(), &addr) != 1) {
            addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
        writer.WriteByte(ip_bytes[0]);
        writer.WriteByte(ip_bytes[1]);
        writer.WriteByte(ip_bytes[2]);
        writer.WriteByte(ip_bytes[3]);

        writer.WriteByte(static_cast<uint8_t>((config.airplay_port >> 8) & 0xFF));
        writer.WriteByte(static_cast<uint8_t>(config.airplay_port & 0xFF));

        apple_data.Data(writer.DetachBuffer());

        pub.Advertisement().ManufacturerData().Append(apple_data);

        m_impl->status_token = pub.StatusChanged([this, config](
            const BluetoothLEAdvertisementPublisher& /*sender*/,
            const BluetoothLEAdvertisementPublisherStatusChangedEventArgs& args) {
            std::lock_guard lock(m_impl->mutex);
            auto status = args.Status();
            if (status == BluetoothLEAdvertisementPublisherStatus::Started) {
                m_advertising.store(true, std::memory_order_release);
                m_supported.store(true, std::memory_order_release);
                m_impl->current_config = config;
                m_impl->status_reason = std::format("Broadcasting Apple AirPlay BLE Discovery Beacon ({}:{})",
                                                    config.ipv4_address, config.airplay_port);
                DUWN_LOG_INFOF("BLE", "WinRT BLE advertisement status: Started (0x004C AirPlay Target Type 0x09 active on {}:{})",
                               config.ipv4_address, config.airplay_port);
            } else if (status == BluetoothLEAdvertisementPublisherStatus::Aborted) {
                m_advertising.store(false, std::memory_order_release);
                auto err = args.Error();
                m_impl->status_reason = std::format("Advertisement aborted (error={})", static_cast<int>(err));
                DUWN_LOG_WARNF("BLE", "WinRT BLE advertisement aborted (error={}); Bluetooth radio may be unavailable or disabled", static_cast<int>(err));
            } else if (status == BluetoothLEAdvertisementPublisherStatus::Stopped) {
                m_advertising.store(false, std::memory_order_release);
                m_impl->status_reason = "Advertisement stopped";
            }
        });

        pub.Start();

        std::lock_guard lock(m_impl->mutex);
        m_impl->publisher = pub;
        m_impl->current_config = config;
        m_supported.store(true, std::memory_order_release);
        m_impl->status_reason = "Start requested; awaiting radio confirmation";

        DUWN_LOG_INFOF("BLE", "Native WinRT BLE Advertisement started (AirPlay Target: {}:{})",
                       config.ipv4_address, config.airplay_port);
        return true;
    } catch (const winrt::hresult_error& ex) {
        std::lock_guard lock(m_impl->mutex);
        m_supported.store(false, std::memory_order_release);
        m_advertising.store(false, std::memory_order_release);
        std::string msg = winrt::to_string(ex.message());
        m_impl->status_reason = std::format("WinRT Bluetooth not supported or disabled: {} (HRESULT={:#x})", msg, static_cast<uint32_t>(ex.code()));
        DUWN_LOG_WARNF("BLE", "Bluetooth LE publisher unavailable: {} (HRESULT={:#x})", msg, static_cast<uint32_t>(ex.code()));
        return false;
    } catch (const std::exception& e) {
        std::lock_guard lock(m_impl->mutex);
        m_supported.store(false, std::memory_order_release);
        m_advertising.store(false, std::memory_order_release);
        m_impl->status_reason = std::format("Exception: {}", e.what());
        DUWN_LOG_WARNF("BLE", "Failed to start BLE advertisement: {}", e.what());
        return false;
    } catch (...) {
        std::lock_guard lock(m_impl->mutex);
        m_supported.store(false, std::memory_order_release);
        m_advertising.store(false, std::memory_order_release);
        m_impl->status_reason = "Unknown exception during WinRT BLE startup";
        DUWN_LOG_WARN("BLE", "Unknown exception during WinRT BLE startup");
        return false;
    }
}

void BleBeaconPublisher::Stop() noexcept {
    if (!m_impl) return;

    try {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->publisher) {
            if (m_impl->status_token.value != 0) {
                m_impl->publisher.StatusChanged(m_impl->status_token);
                m_impl->status_token = {};
            }
            m_impl->publisher.Stop();
            m_impl->publisher = nullptr;
        }
        m_advertising.store(false, std::memory_order_release);
        m_impl->status_reason = "Stopped";
    } catch (...) {
        // Safe no-op on shutdown
    }
}

} // namespace duwn::network
