#include "NetworkEnvironment.h"
#include "common/logging/Logger.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netlistmgr.h>
#include <netfw.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <algorithm>
#include <format>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

namespace duwn::network {

using Microsoft::WRL::ComPtr;

static std::wstring ToLower(std::wstring_view sv) {
    std::wstring s(sv);
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

AdapterClassification ClassifyAdapter(
    std::wstring_view name,
    std::wstring_view desc,
    std::string_view ipv4,
    bool is_up,
    bool is_wifi,
    bool is_ethernet,
    int* out_score) noexcept
{
    if (!is_up || ipv4.empty()) {
        if (out_score) *out_score = -1;
        return AdapterClassification::Disconnected;
    }

    if (ipv4 == "127.0.0.1" || ipv4.rfind("127.", 0) == 0) {
        if (out_score) *out_score = -100;
        return AdapterClassification::Loopback;
    }

    auto low_n = ToLower(name);
    auto low_d = ToLower(desc);

    // Apple Mobile Device Ethernet (USB wired mode)
    if (low_n.find(L"apple mobile device ethernet") != std::wstring::npos ||
        low_d.find(L"apple mobile device ethernet") != std::wstring::npos ||
        low_n.find(L"apple mobile") != std::wstring::npos ||
        low_d.find(L"apple mobile") != std::wstring::npos) {
        if (out_score) *out_score = 20; // 20 in wireless mode (prioritized separately in wired mode)
        return AdapterClassification::AppleUsb;
    }

    // Link-local only (169.254.x.x)
    if (ipv4.rfind("169.254.", 0) == 0) {
        if (out_score) *out_score = 0;
        return AdapterClassification::LinkLocalOnly;
    }

    // VPN adapters (Tailscale, WireGuard, OpenVPN, ZeroTier, etc.)
    const wchar_t* const kVpnKeywords[] = {
        L"tailscale", L"wireguard", L"openvpn", L"zerotier",
        L"wintun", L"tap-", L"tun", L"vpn"
    };
    for (const auto* kw : kVpnKeywords) {
        if (low_n.find(kw) != std::wstring::npos || low_d.find(kw) != std::wstring::npos) {
            if (out_score) *out_score = 10;
            return AdapterClassification::Vpn;
        }
    }

    // Virtual / VM / Container adapters
    const wchar_t* const kVirtualKeywords[] = {
        L"vethernet", L"hyper-v", L"wsl", L"vmware",
        L"virtualbox", L"virtual", L"host-only", L"npcap",
        L"pcap", L"teredo", L"isatap", L"pseudo", L"loopback"
    };
    for (const auto* kw : kVirtualKeywords) {
        if (low_n.find(kw) != std::wstring::npos || low_d.find(kw) != std::wstring::npos) {
            if (out_score) *out_score = 5;
            return AdapterClassification::Virtual;
        }
    }

    // Physical LAN adapters
    if (is_wifi) {
        if (out_score) *out_score = 100;
        return AdapterClassification::PhysicalLan;
    }
    if (is_ethernet) {
        if (out_score) *out_score = 90;
        return AdapterClassification::PhysicalLan;
    }

    if (out_score) *out_score = 70;
    return AdapterClassification::PhysicalLan;
}

bool DetectSuspectedNetworkIsolation(const NetworkEnvironmentInfo& env, int mock_peer_count) noexcept {
    if (!env.is_public_profile || env.best_adapter_class != AdapterClassification::PhysicalLan) {
        return false;
    }

    bool is_wifi = false;
    for (const auto& ad : env.adapters) {
        if (ad.name == env.best_adapter_name && ad.is_wifi) {
            is_wifi = true;
            break;
        }
    }
    if (!is_wifi) return false;

    int unicast_peer_count = 0;
    if (mock_peer_count >= 0) {
        unicast_peer_count = mock_peer_count;
    } else {
        PMIB_IPNET_TABLE2 net_table = nullptr;
        if (::GetIpNetTable2(AF_INET, &net_table) == NO_ERROR && net_table) {
            for (ULONG i = 0; i < net_table->NumEntries; ++i) {
                const auto& row = net_table->Table[i];
                if (row.State == NlnsReachable || row.State == NlnsStale) {
                    uint32_t ip = ntohl(row.Address.Ipv4.sin_addr.s_addr);
                    uint8_t first_octet = static_cast<uint8_t>((ip >> 24) & 0xFF);
                    uint8_t last_octet = static_cast<uint8_t>(ip & 0xFF);
                    if (first_octet != 127 && first_octet < 224 && last_octet != 255 && last_octet != 0) {
                        unicast_peer_count++;
                    }
                }
            }
            ::FreeMibTable(net_table);
        }
    }

    // If at least 2 unicast devices are in ARP cache (e.g. gateway + LAN peer),
    // client-to-client layer 2 traffic is active and unisolated.
    if (unicast_peer_count >= 2) {
        return false;
    }

    // Isolated or zero peers on Public Wi-Fi
    return true;
}

NetworkEnvironmentInfo NetworkEnvironmentInfo::Probe() noexcept {
    NetworkEnvironmentInfo info;

    // 1. Probe Windows Firewall Network Profile via INetworkListManager
    {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool need_uninit = SUCCEEDED(hr);
        {
            ComPtr<INetworkListManager> nlm;
            hr = ::CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&nlm));
            if (SUCCEEDED(hr) && nlm) {
                ComPtr<IEnumNetworks> enum_net;
                if (SUCCEEDED(nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &enum_net)) && enum_net) {
                    ComPtr<INetwork> net;
                    ULONG fetched = 0;
                    while (enum_net->Next(1, &net, &fetched) == S_OK && fetched == 1) {
                        NLM_NETWORK_CATEGORY cat;
                        if (SUCCEEDED(net->GetCategory(&cat))) {
                            if (cat == NLM_NETWORK_CATEGORY_PUBLIC) {
                                info.primary_profile = NetworkProfileCategory::Public;
                                info.is_public_profile = true;
                                break; // One public connected profile is enough to flag risk
                            } else if (cat == NLM_NETWORK_CATEGORY_PRIVATE) {
                                if (info.primary_profile != NetworkProfileCategory::Public) {
                                    info.primary_profile = NetworkProfileCategory::Private;
                                }
                            } else if (cat == NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED) {
                                if (info.primary_profile == NetworkProfileCategory::Unknown) {
                                    info.primary_profile = NetworkProfileCategory::Domain;
                                }
                            }
                        }
                        net.Reset();
                    }
                }
            }
        }
        if (need_uninit) {
            ::CoUninitialize();
        }
    }

    // 2. Enumerate Network Adapters via GetAdaptersAddresses
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG buf_len = 16384;
    std::vector<uint8_t> buffer(buf_len);
    IP_ADAPTER_ADDRESSES* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    DWORD ret = ::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buf_len);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ret = ::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &buf_len);
    }

    if (ret == NO_ERROR) {
        for (auto* curr = addrs; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            AdapterDetails ad;
            ad.name = curr->FriendlyName ? curr->FriendlyName : L"";
            ad.description = curr->Description ? curr->Description : L"";
            ad.is_up = (curr->OperStatus == IfOperStatusUp);
            ad.is_wifi = (curr->IfType == IF_TYPE_IEEE80211);
            ad.is_ethernet = (curr->IfType == IF_TYPE_ETHERNET_CSMACD);

            // Extract primary IPv4
            for (auto* unicast = curr->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                if (unicast->Address.lpSockaddr && unicast->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                    char ip_buf[INET_ADDRSTRLEN] = {0};
                    if (::inet_ntop(AF_INET, &sin->sin_addr, ip_buf, sizeof(ip_buf))) {
                        ad.ipv4_address = ip_buf;
                        break;
                    }
                }
            }

            int score = 0;
            ad.classification = ClassifyAdapter(ad.name, ad.description, ad.ipv4_address,
                                                ad.is_up, ad.is_wifi, ad.is_ethernet, &score);
            ad.priority_score = score;
            ad.is_physical = (ad.classification == AdapterClassification::PhysicalLan);

            if (!ad.ipv4_address.empty()) {
                if (ad.is_up) {
                    if (ad.classification == AdapterClassification::PhysicalLan) info.active_physical_count++;
                    else if (ad.classification == AdapterClassification::Vpn) info.active_vpn_count++;
                    else if (ad.classification == AdapterClassification::Virtual) info.active_virtual_count++;
                }
                info.adapters.push_back(ad);
            }
        }
    }

    // 3. Select Best Adapter (Physical Wi-Fi 100 > Physical Ethernet 90 > Apple USB 20 > VPN 10 > Virtual 5)
    const AdapterDetails* best = nullptr;
    int best_score = -1;

    for (const auto& ad : info.adapters) {
        if (!ad.is_up || ad.ipv4_address.empty()) continue;

        if (ad.priority_score > best_score) {
            best_score = ad.priority_score;
            best = &ad;
        }
    }

    if (best) {
        info.best_adapter_ip = best->ipv4_address;
        info.best_adapter_name = best->name;
        info.best_adapter_class = best->classification;
        info.best_adapter_score = best_score;
    }

    info.network_isolation_suspected = DetectSuspectedNetworkIsolation(info);

    std::wstring pub_details;
    if (VerifyPublicFirewallRulesExist(&pub_details)) {
        info.public_rules_localsubnet = (pub_details.find(L"LocalSubnet") != std::wstring::npos);
    }

    return info;
}

void NetworkEnvironmentInfo::LogEnvironment() const noexcept {
    DUWN_LOG_INFO("Network", "=== [NETWORK COMPATIBILITY SNAPSHOT] ===");

    const char* profile_str = "Unknown";
    switch (primary_profile) {
    case NetworkProfileCategory::Public:  profile_str = "PUBLIC (Warning: Inbound traffic restricted)"; break;
    case NetworkProfileCategory::Private: profile_str = "PRIVATE (Optimal)"; break;
    case NetworkProfileCategory::Domain:  profile_str = "DOMAIN"; break;
    default:                              profile_str = "UNKNOWN"; break;
    }
    DUWN_LOG_INFOF("Network", "Primary Firewall Profile: {}", profile_str);
    DUWN_LOG_INFOF("Network", "Public Profile Risk Flag: {}", is_public_profile ? "ACTIVE_RISK" : "NONE");
    DUWN_LOG_INFOF("Network", "LocalSubnet Scoping: {}",
                   public_rules_localsubnet ? "ENFORCED (Least-Privilege Subnet Only)" :
                   (is_public_profile ? "NOT_CONFIGURED" : "STANDARD_PRIVATE_ANY"));
    DUWN_LOG_INFOF("Network", "Network Isolation Suspected: {}",
                   network_isolation_suspected ? "YES (AP Client Isolation Risk)" : "NO");
    DUWN_LOG_INFOF("Network", "Active Adapters: Physical={}, VPN={}, Virtual={}",
                   active_physical_count, active_vpn_count, active_virtual_count);

    for (const auto& ad : adapters) {
        DUWN_LOG_INFOF("Network", "  • [{}] {} (Class: {}, IP: {}, Score: {}, Up: {})",
            ad.is_physical ? "PHYSICAL" : "NON_PHYSICAL",
            WideToUtf8(ad.name),
            AdapterClassificationToString(ad.classification),
            ad.ipv4_address,
            ad.priority_score,
            ad.is_up ? "YES" : "NO");
    }

    if (!best_adapter_ip.empty()) {
        DUWN_LOG_INFOF("Network", "Selected Primary AirPlay Adapter: {} (IP: {}, Class: {}, Score: {})",
            WideToUtf8(best_adapter_name), best_adapter_ip,
            AdapterClassificationToString(best_adapter_class), best_adapter_score);
    } else {
        DUWN_LOG_WARN("Network", "No suitable active network adapter identified.");
    }
    DUWN_LOG_INFO("Network", "Deterministic Port Set: TCP 7000..7002, UDP 7000..7002, mDNS UDP 5353");

    if (network_isolation_suspected) {
        DUWN_LOG_WARN("Network", "========================================================================");
        DUWN_LOG_WARN("Network", "NETWORK COMPATIBILITY DIAGNOSTIC: NETWORK_ISOLATION_SUSPECTED=TRUE");
        DUWN_LOG_WARN("Network", "Wi-Fi router/AP may have 'Client Isolation' or 'AP Isolation' enabled.");
        DUWN_LOG_WARN("Network", "Wireless discovery and connection may be blocked at layer 2.");
        DUWN_LOG_WARN("Network", "RECOMMENDATION: Connect via Apple USB Cable Mode or use Private Wi-Fi.");
        DUWN_LOG_WARN("Network", "========================================================================");
    }
    DUWN_LOG_INFO("Network", "========================================");
}

static bool GetInstalledExes(std::wstring& core_exe, std::wstring& sidecar_exe) noexcept {
    wchar_t exe_path[MAX_PATH] = {0};
    if (::GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        return false;
    }
    core_exe = exe_path;
    size_t last_slash = core_exe.find_last_of(L"\\/");
    if (last_slash != std::wstring::npos) {
        sidecar_exe = core_exe.substr(0, last_slash) + L"\\duwn-airplay\\uxplay.exe";
    } else {
        sidecar_exe = L"duwn-airplay\\uxplay.exe";
    }
    return true;
}

static ComPtr<INetFwPolicy2> GetFirewallPolicy() noexcept {
    ComPtr<INetFwPolicy2> policy;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(NetFwPolicy2),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(policy.GetAddressOf()));
    if (FAILED(hr) || !policy) {
        ::CoCreateInstance(
            __uuidof(NetFwPolicy2),
            nullptr,
            CLSCTX_ALL,
            __uuidof(INetFwPolicy2),
            reinterpret_cast<void**>(policy.GetAddressOf()));
    }
    return policy;
}

static bool CheckExactPublicRule(
    INetFwRules* rules,
    const wchar_t* rule_name,
    const std::wstring& expected_exe,
    std::wstring* out_reason = nullptr) noexcept
{
    if (!rules || !rule_name || expected_exe.empty()) {
        if (out_reason) *out_reason = L"Invalid parameters";
        return false;
    }

    BSTR bname = ::SysAllocString(rule_name);
    if (!bname) {
        if (out_reason) *out_reason = L"Out of memory";
        return false;
    }

    ComPtr<INetFwRule> rule;
    HRESULT hr = rules->Item(bname, &rule);
    ::SysFreeString(bname);

    if (FAILED(hr) || !rule) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' not found in policy", rule_name);
        return false;
    }

    // 1. Verify Enabled
    VARIANT_BOOL vb_enabled = VARIANT_FALSE;
    hr = rule->get_Enabled(&vb_enabled);
    if (FAILED(hr) || vb_enabled != VARIANT_TRUE) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' is disabled", rule_name);
        return false;
    }

    // 2. Verify Direction == Inbound
    NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
    hr = rule->get_Direction(&dir);
    if (FAILED(hr) || dir != NET_FW_RULE_DIR_IN) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' direction is not Inbound", rule_name);
        return false;
    }

    // 3. Verify Action == Allow
    NET_FW_ACTION act = NET_FW_ACTION_BLOCK;
    hr = rule->get_Action(&act);
    if (FAILED(hr) || act != NET_FW_ACTION_ALLOW) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' action is not Allow", rule_name);
        return false;
    }

    // 4. Verify Profile contains Public
    long profiles = 0;
    hr = rule->get_Profiles(&profiles);
    if (FAILED(hr) || !(profiles & NET_FW_PROFILE2_PUBLIC)) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' profile mask ({:#x}) does not include Public", rule_name, profiles);
        return false;
    }

    // 5. Verify Program Path matches exact installed executable (case-insensitive)
    BSTR bapp = nullptr;
    hr = rule->get_ApplicationName(&bapp);
    if (FAILED(hr) || !bapp) {
        if (out_reason) *out_reason = std::format(L"Rule '{}' missing application program path", rule_name);
        return false;
    }
    bool path_match = (_wcsicmp(bapp, expected_exe.c_str()) == 0);
    if (!path_match && out_reason) {
        *out_reason = std::format(L"Rule '{}' program path mismatch: got '{}', expected '{}'",
            rule_name, bapp ? bapp : L"(null)", expected_exe);
    }
    ::SysFreeString(bapp);

    if (!path_match) return false;

    // 6. Verify RemoteAddresses (check if LocalSubnet is enforced)
    BSTR baddr = nullptr;
    bool has_localsubnet = false;
    hr = rule->get_RemoteAddresses(&baddr);
    if (SUCCEEDED(hr) && baddr) {
        if (_wcsicmp(baddr, L"LocalSubnet") == 0) {
            has_localsubnet = true;
        }
        ::SysFreeString(baddr);
    }
    if (out_reason) {
        *out_reason = has_localsubnet ? L"OK (LocalSubnet)" : L"OK (AnyAddress)";
    }

    return true;
}

bool VerifyPublicFirewallRulesExist(std::wstring* out_details) noexcept {
    HRESULT hr_co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = SUCCEEDED(hr_co);
    bool result = false;

    {
        std::wstring core_exe, sidecar_exe;
        if (GetInstalledExes(core_exe, sidecar_exe)) {
            auto policy = GetFirewallPolicy();
            if (policy) {
                ComPtr<INetFwRules> rules;
                if (SUCCEEDED(policy->get_Rules(&rules)) && rules) {
                    std::wstring reason_core, reason_airplay;
                    bool core_ok = CheckExactPublicRule(rules.Get(), L"Duwn Mirror Core (Public)", core_exe, &reason_core);
                    bool airplay_ok = CheckExactPublicRule(rules.Get(), L"Duwn Mirror AirPlay (Public)", sidecar_exe, &reason_airplay);

                    if (core_ok && airplay_ok) {
                        result = true;
                        bool both_localsubnet = (reason_core.find(L"LocalSubnet") != std::wstring::npos &&
                                                reason_airplay.find(L"LocalSubnet") != std::wstring::npos);
                        if (out_details) {
                            *out_details = both_localsubnet
                                ? L"Verified exact match for Core and AirPlay public rules (LocalSubnet restricted)"
                                : L"Verified exact match for Core and AirPlay public rules";
                        }
                    } else if (out_details) {
                        *out_details = std::format(L"Core: {}; AirPlay: {}",
                            core_ok ? L"OK" : reason_core,
                            airplay_ok ? L"OK" : reason_airplay);
                    }
                } else if (out_details) {
                    *out_details = L"Failed to acquire INetFwRules collection";
                }
            } else if (out_details) {
                *out_details = L"Failed to acquire INetFwPolicy2 COM interface";
            }
        } else if (out_details) {
            *out_details = L"Failed to resolve installed executable paths";
        }
    }

    if (need_uninit) ::CoUninitialize();
    return result;
}

bool VerifyDomainPrivateFirewallRulesExist(std::wstring* out_details) noexcept {
    HRESULT hr_co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = SUCCEEDED(hr_co);
    bool result = false;

    {
        std::wstring core_exe, sidecar_exe;
        if (GetInstalledExes(core_exe, sidecar_exe)) {
            auto policy = GetFirewallPolicy();
            if (policy) {
                ComPtr<INetFwRules> rules;
                if (SUCCEEDED(policy->get_Rules(&rules)) && rules) {
                    bool core_found = false;
                    bool sidecar_found = false;

                    IUnknown* pUnk = nullptr;
                    if (SUCCEEDED(rules->get__NewEnum(&pUnk)) && pUnk) {
                        ComPtr<IEnumVARIANT> pEnum;
                        if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IEnumVARIANT), reinterpret_cast<void**>(pEnum.GetAddressOf())))) {
                            VARIANT var;
                            ::VariantInit(&var);
                            while (pEnum->Next(1, &var, nullptr) == S_OK) {
                                if (var.vt == VT_DISPATCH && var.pdispVal) {
                                    ComPtr<INetFwRule> rule;
                                    if (SUCCEEDED(var.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(rule.GetAddressOf())))) {
                                        VARIANT_BOOL enabled = VARIANT_FALSE;
                                        NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
                                        NET_FW_ACTION act = NET_FW_ACTION_BLOCK;
                                        long profiles = 0;
                                        BSTR app_name = nullptr;

                                        rule->get_Enabled(&enabled);
                                        rule->get_Direction(&dir);
                                        rule->get_Action(&act);
                                        rule->get_Profiles(&profiles);
                                        rule->get_ApplicationName(&app_name);

                                        if (enabled == VARIANT_TRUE && dir == NET_FW_RULE_DIR_IN && act == NET_FW_ACTION_ALLOW) {
                                            if (profiles & (NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_DOMAIN)) {
                                                if (app_name) {
                                                    if (_wcsicmp(app_name, core_exe.c_str()) == 0) {
                                                        core_found = true;
                                                    } else if (_wcsicmp(app_name, sidecar_exe.c_str()) == 0) {
                                                        sidecar_found = true;
                                                    }
                                                }
                                            }
                                        }
                                        if (app_name) ::SysFreeString(app_name);
                                    }
                                }
                                ::VariantClear(&var);
                                if (core_found && sidecar_found) break;
                            }
                        }
                        pUnk->Release();
                    }

                    if (core_found && sidecar_found) {
                        result = true;
                        if (out_details) *out_details = L"Domain/Private inbound rules verified for Core and AirPlay";
                    } else if (out_details) {
                        *out_details = std::format(L"Domain/Private rules: Core={}, AirPlay={}",
                            core_found ? L"Present" : L"Missing",
                            sidecar_found ? L"Present" : L"Missing");
                    }
                } else if (out_details) {
                    *out_details = L"Failed to acquire INetFwRules collection";
                }
            } else if (out_details) {
                *out_details = L"Failed to acquire INetFwPolicy2 COM interface";
            }
        } else if (out_details) {
            *out_details = L"Failed to resolve installed executable paths";
        }
    }

    if (need_uninit) ::CoUninitialize();
    return result;
}

bool ArePublicFirewallRulesPresent() noexcept {
    HRESULT hr_co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = SUCCEEDED(hr_co);
    bool present = false;

    {
        auto policy = GetFirewallPolicy();
        if (policy) {
            ComPtr<INetFwRules> rules;
            if (SUCCEEDED(policy->get_Rules(&rules)) && rules) {
                auto HasRule = [&](const wchar_t* name) -> bool {
                    BSTR bname = ::SysAllocString(name);
                    ComPtr<INetFwRule> r;
                    HRESULT hr = rules->Item(bname, &r);
                    ::SysFreeString(bname);
                    return SUCCEEDED(hr) && (r != nullptr);
                };
                present = HasRule(L"Duwn Mirror Core (Public)") || HasRule(L"Duwn Mirror AirPlay (Public)") ||
                          HasRule(L"DUWN Mirror Core (Public)") || HasRule(L"DUWN Mirror AirPlay (Public)");
            }
        }
    }

    if (need_uninit) ::CoUninitialize();
    return present;
}

static bool IsProcessElevated() noexcept {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool is_elevated = false;
    if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        is_elevated = (elevation.TokenIsElevated != 0);
    }
    ::CloseHandle(token);
    return is_elevated;
}

int ExecuteFirewallCliCommand(std::wstring_view action) noexcept {
    HRESULT hr_co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool need_uninit = SUCCEEDED(hr_co);
    int exit_code = 1;

    {
        auto policy = GetFirewallPolicy();
        if (!policy) {
            exit_code = 2;
        } else {
            ComPtr<INetFwRules> rules;
            if (FAILED(policy->get_Rules(&rules)) || !rules) {
                exit_code = 3;
            } else {
                auto HasRule = [&](const wchar_t* name) -> bool {
                    BSTR bname = ::SysAllocString(name);
                    if (!bname) return false;
                    ComPtr<INetFwRule> r;
                    HRESULT hr = rules->Item(bname, &r);
                    ::SysFreeString(bname);
                    return SUCCEEDED(hr) && (r != nullptr);
                };

                auto RemoveRule = [&](const wchar_t* name) {
                    BSTR bname = ::SysAllocString(name);
                    if (bname) {
                        rules->Remove(bname);
                        ::SysFreeString(bname);
                    }
                };

                if (action == L"disable-public") {
                    RemoveRule(L"Duwn Mirror Core (Public)");
                    RemoveRule(L"Duwn Mirror AirPlay (Public)");
                    RemoveRule(L"DUWN Mirror Core (Public)");
                    RemoveRule(L"DUWN Mirror AirPlay (Public)");

                    // 0 = cleanup successful OR rules already absent; 4 = cleanup failed
                    bool still_present = HasRule(L"Duwn Mirror Core (Public)") || HasRule(L"Duwn Mirror AirPlay (Public)") ||
                                         HasRule(L"DUWN Mirror Core (Public)") || HasRule(L"DUWN Mirror AirPlay (Public)");
                    exit_code = still_present ? 4 : 0;
                } else if (action == L"enable-public") {
                    std::wstring core_exe, sidecar_exe;
                    if (!GetInstalledExes(core_exe, sidecar_exe)) {
                        exit_code = 1;
                    } else {
                        // Clean out any stale or partial rules first
                        RemoveRule(L"Duwn Mirror Core (Public)");
                        RemoveRule(L"Duwn Mirror AirPlay (Public)");
                        RemoveRule(L"DUWN Mirror Core (Public)");
                        RemoveRule(L"DUWN Mirror AirPlay (Public)");

                        auto AddRule = [&](const wchar_t* name, const wchar_t* desc, const std::wstring& exe) -> bool {
                            ComPtr<INetFwRule> rule;
                            HRESULT hr = ::CoCreateInstance(
                                __uuidof(NetFwRule),
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(INetFwRule),
                                reinterpret_cast<void**>(rule.GetAddressOf()));
                            if (FAILED(hr) || !rule) {
                                return false;
                            }

                            BSTR bname = ::SysAllocString(name);
                            rule->put_Name(bname);
                            ::SysFreeString(bname);

                            BSTR bdesc = ::SysAllocString(desc);
                            rule->put_Description(bdesc);
                            ::SysFreeString(bdesc);

                            BSTR bapp = ::SysAllocString(exe.c_str());
                            rule->put_ApplicationName(bapp);
                            ::SysFreeString(bapp);

                            rule->put_Direction(NET_FW_RULE_DIR_IN);
                            rule->put_Action(NET_FW_ACTION_ALLOW);
                            rule->put_Profiles(NET_FW_PROFILE2_PUBLIC);
                            rule->put_Protocol(256); // NET_FW_IP_PROTOCOL_ANY
                            rule->put_Enabled(VARIANT_TRUE);

                            // Least-privilege: restrict inbound connections on public networks to LocalSubnet only
                            BSTR bscope = ::SysAllocString(L"LocalSubnet");
                            rule->put_RemoteAddresses(bscope);
                            ::SysFreeString(bscope);

                            hr = rules->Add(rule.Get());
                            return SUCCEEDED(hr);
                        };

                        bool ok_core = AddRule(
                            L"Duwn Mirror Core (Public)",
                            L"Allow Duwn Mirror Core inbound connections on public networks",
                            core_exe);
                        bool ok_airplay = AddRule(
                            L"Duwn Mirror AirPlay (Public)",
                            L"Allow Duwn Mirror AirPlay sidecar inbound connections on public networks",
                            sidecar_exe);

                        bool verified = ok_core && ok_airplay &&
                                        CheckExactPublicRule(rules.Get(), L"Duwn Mirror Core (Public)", core_exe) &&
                                        CheckExactPublicRule(rules.Get(), L"Duwn Mirror AirPlay (Public)", sidecar_exe);

                        exit_code = verified ? 0 : 5;
                    }
                } else if (action == L"repair-all") {
                    std::wstring core_exe, sidecar_exe;
                    if (!GetInstalledExes(core_exe, sidecar_exe)) {
                        exit_code = 1;
                    } else {
                        // Remove existing domain/private rules if any were corrupted
                        RemoveRule(L"Duwn Mirror Core (Domain/Private)");
                        RemoveRule(L"Duwn Mirror AirPlay (Domain/Private)");
                        RemoveRule(L"DUWN-Mirror-Core-TCP");
                        RemoveRule(L"DUWN-Mirror-Core-UDP");
                        RemoveRule(L"DUWN-Mirror-UxPlay-TCP");
                        RemoveRule(L"DUWN-Mirror-UxPlay-UDP");

                        auto AddDomainPrivateRule = [&](const wchar_t* name, const wchar_t* desc, const std::wstring& exe) -> bool {
                            ComPtr<INetFwRule> rule;
                            HRESULT hr = ::CoCreateInstance(
                                __uuidof(NetFwRule),
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(INetFwRule),
                                reinterpret_cast<void**>(rule.GetAddressOf()));
                            if (FAILED(hr) || !rule) return false;

                            BSTR bname = ::SysAllocString(name); rule->put_Name(bname); ::SysFreeString(bname);
                            BSTR bdesc = ::SysAllocString(desc); rule->put_Description(bdesc); ::SysFreeString(bdesc);
                            BSTR bapp = ::SysAllocString(exe.c_str()); rule->put_ApplicationName(bapp); ::SysFreeString(bapp);

                            rule->put_Direction(NET_FW_RULE_DIR_IN);
                            rule->put_Action(NET_FW_ACTION_ALLOW);
                            rule->put_Profiles(NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE);
                            rule->put_Protocol(256); // NET_FW_IP_PROTOCOL_ANY
                            rule->put_Enabled(VARIANT_TRUE);

                            BSTR bscope = ::SysAllocString(L"LocalSubnet");
                            rule->put_RemoteAddresses(bscope);
                            ::SysFreeString(bscope);

                            hr = rules->Add(rule.Get());
                            return SUCCEEDED(hr);
                        };

                        bool ok_core = AddDomainPrivateRule(
                            L"Duwn Mirror Core (Domain/Private)",
                            L"Allow Duwn Mirror Core inbound connections on Domain and Private networks",
                            core_exe);
                        bool ok_airplay = AddDomainPrivateRule(
                            L"Duwn Mirror AirPlay (Domain/Private)",
                            L"Allow Duwn Mirror AirPlay sidecar inbound connections on Domain and Private networks",
                            sidecar_exe);

                        // If public rules were present, re-apply them with LocalSubnet
                        if (HasRule(L"Duwn Mirror Core (Public)") || HasRule(L"Duwn Mirror AirPlay (Public)")) {
                            RemoveRule(L"Duwn Mirror Core (Public)");
                            RemoveRule(L"Duwn Mirror AirPlay (Public)");

                            auto AddPublicRule = [&](const wchar_t* name, const wchar_t* desc, const std::wstring& exe) -> bool {
                                ComPtr<INetFwRule> rule;
                                HRESULT hr = ::CoCreateInstance(
                                    __uuidof(NetFwRule),
                                    nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    __uuidof(INetFwRule),
                                    reinterpret_cast<void**>(rule.GetAddressOf()));
                                if (FAILED(hr) || !rule) return false;

                                BSTR bname = ::SysAllocString(name); rule->put_Name(bname); ::SysFreeString(bname);
                                BSTR bdesc = ::SysAllocString(desc); rule->put_Description(bdesc); ::SysFreeString(bdesc);
                                BSTR bapp = ::SysAllocString(exe.c_str()); rule->put_ApplicationName(bapp); ::SysFreeString(bapp);
                                rule->put_Direction(NET_FW_RULE_DIR_IN);
                                rule->put_Action(NET_FW_ACTION_ALLOW);
                                rule->put_Profiles(NET_FW_PROFILE2_PUBLIC);
                                rule->put_Protocol(256);
                                rule->put_Enabled(VARIANT_TRUE);
                                BSTR bscope = ::SysAllocString(L"LocalSubnet");
                                rule->put_RemoteAddresses(bscope);
                                ::SysFreeString(bscope);
                                return SUCCEEDED(rules->Add(rule.Get()));
                            };

                            AddPublicRule(L"Duwn Mirror Core (Public)", L"Allow Duwn Mirror Core inbound connections on public networks", core_exe);
                            AddPublicRule(L"Duwn Mirror AirPlay (Public)", L"Allow Duwn Mirror AirPlay sidecar inbound connections on public networks", sidecar_exe);
                        }

                        exit_code = (ok_core && ok_airplay) ? 0 : 5;
                    }
                } else {
                    exit_code = 6; // Unknown action
                }
            }
        }
    }

    if (need_uninit) ::CoUninitialize();
    return exit_code;
}

bool ConfigurePublicFirewall(bool allow, void* parent_hwnd) noexcept {
    HWND hwnd = static_cast<HWND>(parent_hwnd);

    // If disabling and no public rules are present, nothing to do -> success without UAC prompt
    if (!allow && !ArePublicFirewallRulesPresent()) {
        DUWN_LOG_INFO("Network", "Disable public firewall: no DUWN public rules currently present. Done.");
        return true;
    }

    if (IsProcessElevated()) {
        int ret = ExecuteFirewallCliCommand(allow ? L"enable-public" : L"disable-public");
        if (allow) {
            return (ret == 0) && VerifyPublicFirewallRulesExist();
        } else {
            return (ret == 0) && !ArePublicFirewallRulesPresent();
        }
    }

    // Unelevated process: invoke self with UAC elevation (runas) and fixed CLI argument
    wchar_t exe_path[MAX_PATH] = {0};
    if (::GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        DUWN_LOG_ERROR("Network", "Failed to get module file name for elevation");
        return false;
    }

    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    std::wstring params = allow ? L"--firewall enable-public" : L"--firewall disable-public";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!::ShellExecuteExW(&sei)) {
        DWORD err = ::GetLastError();
        DUWN_LOG_WARNF("Network", "UAC elevation request failed or was cancelled by user (error={})", err);
        return false;
    }

    if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, 15000);
        DWORD exit_code = 1;
        ::GetExitCodeProcess(sei.hProcess, &exit_code);
        ::CloseHandle(sei.hProcess);
        if (exit_code != 0) {
            DUWN_LOG_WARNF("Network", "Elevated firewall helper exited with code {}", exit_code);
            return false;
        }
    }

    // Exact verification step
    if (allow) {
        std::wstring details;
        bool verified = VerifyPublicFirewallRulesExist(&details);
        DUWN_LOG_INFOF("Network", "Verified Public firewall rules exist: {} ({})",
            verified ? "YES" : "NO", WideToUtf8(details));
        return verified;
    } else {
        bool rules_gone = !ArePublicFirewallRulesPresent();
        DUWN_LOG_INFOF("Network", "Verified Public firewall rules removed: {}", rules_gone ? "YES" : "NO");
        return rules_gone;
    }
}

bool RepairFirewallRules(void* parent_hwnd) noexcept {
    HWND hwnd = static_cast<HWND>(parent_hwnd);

    if (IsProcessElevated()) {
        int ret = ExecuteFirewallCliCommand(L"repair-all");
        return (ret == 0) && VerifyDomainPrivateFirewallRulesExist();
    }

    wchar_t exe_path[MAX_PATH] = {0};
    if (::GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        DUWN_LOG_ERROR("Network", "Failed to get module file name for elevation");
        return false;
    }

    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.lpParameters = L"--firewall repair-all";
    sei.nShow = SW_HIDE;

    if (!::ShellExecuteExW(&sei)) {
        DWORD err = ::GetLastError();
        DUWN_LOG_WARNF("Network", "UAC elevation request failed or was cancelled by user (error={})", err);
        return false;
    }

    if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, 15000);
        DWORD exit_code = 1;
        ::GetExitCodeProcess(sei.hProcess, &exit_code);
        ::CloseHandle(sei.hProcess);
        if (exit_code != 0) {
            DUWN_LOG_WARNF("Network", "Elevated firewall repair helper exited with code {}", exit_code);
            return false;
        }
    }

    std::wstring details;
    bool verified = VerifyDomainPrivateFirewallRulesExist(&details);
    DUWN_LOG_INFOF("Network", "Verified Domain/Private firewall repair: {} ({})",
        verified ? "YES" : "NO", WideToUtf8(details));
    return verified;
}

} // namespace duwn::network
