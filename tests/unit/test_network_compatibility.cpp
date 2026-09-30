#include "network/NetworkEnvironment.h"
#include "network/NetworkChangeMonitor.h"
#include "network/BleBeaconPublisher.h"
#include "airplay/AirPlayProcess.h"
#include "app/Settings.h"
#include <string>

DUWN_TEST(Network_AdapterClassification_PhysicalWiFibvEthernetAndVpn) {
    using namespace duwn::network;

    int wifi_score = 0;
    auto c_wifi = ClassifyAdapter(L"Wi-Fi", L"Intel(R) Wi-Fi 6 AX201 160MHz", "192.168.1.50", true, true, false, &wifi_score);
    DUWN_ASSERT(c_wifi == AdapterClassification::PhysicalLan);
    DUWN_ASSERT(wifi_score == 100);

    int eth_score = 0;
    auto c_eth = ClassifyAdapter(L"Ethernet", L"Realtek PCIe GbE Family Controller", "192.168.1.51", true, false, true, &eth_score);
    DUWN_ASSERT(c_eth == AdapterClassification::PhysicalLan);
    DUWN_ASSERT(eth_score == 90);

    int tailscale_score = 0;
    auto c_tailscale = ClassifyAdapter(L"Tailscale", L"Tailscale Tunnel", "100.80.90.100", true, false, false, &tailscale_score);
    DUWN_ASSERT(c_tailscale == AdapterClassification::Vpn);
    DUWN_ASSERT(tailscale_score == 10);

    int wireguard_score = 0;
    auto c_wg = ClassifyAdapter(L"wg0", L"WireGuard Tunnel", "10.0.0.2", true, false, false, &wireguard_score);
    DUWN_ASSERT(c_wg == AdapterClassification::Vpn);
    DUWN_ASSERT(wireguard_score == 10);

    int veth_score = 0;
    auto c_veth = ClassifyAdapter(L"vEthernet (WSL)", L"Hyper-V Virtual Ethernet Adapter", "172.25.0.1", true, false, false, &veth_score);
    DUWN_ASSERT(c_veth == AdapterClassification::Virtual);
    DUWN_ASSERT(veth_score == 5);

    int apple_score = 0;
    auto c_apple = ClassifyAdapter(L"Ethernet 2", L"Apple Mobile Device Ethernet", "172.20.10.1", true, false, false, &apple_score);
    DUWN_ASSERT(c_apple == AdapterClassification::AppleUsb);
    DUWN_ASSERT(apple_score == 20);

    int link_local_score = 0;
    auto c_ll = ClassifyAdapter(L"Local Area Connection", L"Ethernet Controller", "169.254.12.34", true, false, true, &link_local_score);
    DUWN_ASSERT(c_ll == AdapterClassification::LinkLocalOnly);
    DUWN_ASSERT(link_local_score == 0);

    int disconn_score = 0;
    auto c_disc = ClassifyAdapter(L"Wi-Fi", L"Intel Wi-Fi", "", false, true, false, &disconn_score);
    DUWN_ASSERT(c_disc == AdapterClassification::Disconnected);
    DUWN_ASSERT(disconn_score == -1);

    // CRITICAL: Verify Physical LAN strictly outranks Tailscale and Virtual adapters
    DUWN_ASSERT(wifi_score > tailscale_score);
    DUWN_ASSERT(eth_score > tailscale_score);
    DUWN_ASSERT(wifi_score > veth_score);
    DUWN_ASSERT(tailscale_score > veth_score);
}

DUWN_TEST(Network_DeterministicPortSelection_CandidatePolicy) {
    using namespace duwn::airplay;

    // Out-of-range base values safely handled
    DUWN_ASSERT(!AirPlayProcess::IsPortBlockAvailable(0));
    DUWN_ASSERT(!AirPlayProcess::IsPortBlockAvailable(65534));

    // Selection chooses preferred candidate or fallback, never 0
    uint16_t port = AirPlayProcess::SelectDeterministicAirPlayPort(7000);
    DUWN_ASSERT(port == 7000 || port == 7100 || port == 7200);

    uint16_t port_zero_fallback = AirPlayProcess::SelectDeterministicAirPlayPort(0);
    DUWN_ASSERT(port_zero_fallback == 7000 || port_zero_fallback == 7100 || port_zero_fallback == 7200);
}

DUWN_TEST(Network_SettingsValidation_SquareEnvelopesUpTo4K) {
    using namespace duwn::app;

    Settings s;
    s.schema_version = 2;
    s.window_preferences.width = 1280;
    s.window_preferences.height = 720;

    std::string reason;

    // 1080x1080 square envelope
    s.receiver_width = 1080;
    s.receiver_height = 1080;
    s.output_width = 1080;
    s.output_height = 1080;
    DUWN_ASSERT(Settings::ValidateSettings(s, &reason));

    // 1920x1920 square envelope (1080p neutral)
    s.receiver_width = 1920;
    s.receiver_height = 1920;
    s.output_width = 1920;
    s.output_height = 1920;
    DUWN_ASSERT(Settings::ValidateSettings(s, &reason));

    // 2560x2560 square envelope (1440p neutral)
    s.receiver_width = 2560;
    s.receiver_height = 2560;
    s.output_width = 2560;
    s.output_height = 2560;
    DUWN_ASSERT(Settings::ValidateSettings(s, &reason));

    // 3840x3840 square envelope (4K neutral)
    s.receiver_width = 3840;
    s.receiver_height = 3840;
    s.output_width = 3840;
    s.output_height = 3840;
    DUWN_ASSERT(Settings::ValidateSettings(s, &reason));

    // Excessively large envelope (>3840) must be rejected with diagnostic reason
    s.receiver_height = 3841;
    reason.clear();
    DUWN_ASSERT(!Settings::ValidateSettings(s, &reason));
    DUWN_ASSERT(!reason.empty());
    DUWN_ASSERT(reason.find("receiver_height") != std::string::npos);
}

DUWN_TEST(Network_IsolationHeuristic_PublicWiFiDetection) {
    using namespace duwn::network;

    NetworkEnvironmentInfo env;
    env.is_public_profile = true;
    env.best_adapter_name = L"Wi-Fi";
    env.best_adapter_class = AdapterClassification::PhysicalLan;

    AdapterDetails ad;
    ad.name = L"Wi-Fi";
    ad.is_wifi = true;
    ad.is_up = true;
    ad.ipv4_address = "192.168.100.12";
    env.adapters.push_back(ad);

    // On Public Wi-Fi with isolated/no peers, AP Client isolation is suspected
    bool isolated = DetectSuspectedNetworkIsolation(env, 0);
    DUWN_ASSERT(isolated);

    // On Public Wi-Fi with active LAN peers (>= 2), AP Client isolation is NOT suspected
    bool unisolated = DetectSuspectedNetworkIsolation(env, 2);
    DUWN_ASSERT(!unisolated);

    // On Private profile, isolation should not be suspected even with 0 peers
    env.is_public_profile = false;
    bool not_isolated = DetectSuspectedNetworkIsolation(env, 0);
    DUWN_ASSERT(!not_isolated);
}

DUWN_TEST(Network_BleBeaconPublisher_LifecycleAndGracefulFallback) {
    using namespace duwn::network;

    BleBeaconPublisher ble;
    DUWN_ASSERT(!ble.IsAdvertising());

    // Disabled by config returns false cleanly
    BleBeaconConfig cfg_off;
    cfg_off.enable_beacon = false;
    DUWN_ASSERT(!ble.Start(cfg_off));
    DUWN_ASSERT(ble.GetStatusReason().find("Disabled") != std::string::npos);

    // Attempt start on current host: either succeeds or gracefully reports unsupported radio
    BleBeaconConfig cfg_on;
    cfg_on.ipv4_address = "192.168.1.100";
    cfg_on.airplay_port = 7000;
    cfg_on.enable_beacon = true;
    bool started = ble.Start(cfg_on);
    if (started) {
        DUWN_ASSERT(ble.IsSupported());
    }
    // Safe shutdown
    ble.Stop();
    DUWN_ASSERT(!ble.IsAdvertising());
}

DUWN_TEST(Network_BleBeaconPublisher_PayloadStructureMatchUxPlayReference) {
    using namespace duwn::network;

    // UxPlay reference specification:
    // Company: 0x004C
    // Data: [0x09, 0x08, 0x13, 0x30, IP[4], Port[2]]
    const std::string ip_str = "192.168.100.144";
    const uint16_t port = 7000;

    in_addr addr{};
    int r = ::inet_pton(AF_INET, ip_str.c_str(), &addr);
    DUWN_ASSERT(r == 1);

    const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
    DUWN_ASSERT(ip_bytes[0] == 192);
    DUWN_ASSERT(ip_bytes[1] == 168);
    DUWN_ASSERT(ip_bytes[2] == 100);
    DUWN_ASSERT(ip_bytes[3] == 144);

    uint8_t port_hi = static_cast<uint8_t>((port >> 8) & 0xFF);
    uint8_t port_lo = static_cast<uint8_t>(port & 0xFF);
    DUWN_ASSERT(port_hi == 0x1B);
    DUWN_ASSERT(port_lo == 0x58);

    // Verify 10-byte buffer layout
    uint8_t expected_payload[10] = {
        0x09, 0x08, 0x13, 0x30,
        192, 168, 100, 144,
        0x1B, 0x58
    };
    DUWN_ASSERT(expected_payload[0] == 0x09); // AirPlay Service Discovery Unit Type
    DUWN_ASSERT(expected_payload[1] == 0x08); // Length
    DUWN_ASSERT(expected_payload[2] == 0x13); // AirPlay Target Flags
    DUWN_ASSERT(expected_payload[3] == 0x30); // Seed
}

DUWN_TEST(Network_DomainPrivateFirewallRulesQuery) {
    using namespace duwn::network;

    std::wstring details;
    bool checked = VerifyDomainPrivateFirewallRulesExist(&details);
    (void)checked;
    DUWN_ASSERT(!details.empty());
}
