#pragma once
// NetworkEnvironment.h — Detects network adapters, physical vs virtual priority,
// and Windows Firewall network profiles (Private vs Public).

#include <string>
#include <vector>
#include <cstdint>

namespace duwn::network {

enum class NetworkProfileCategory {
    Unknown,
    Public,
    Private,
    Domain
};

enum class AdapterClassification {
    PhysicalLan,
    AppleUsb,
    Vpn,
    Virtual,
    Loopback,
    LinkLocalOnly,
    Disconnected
};

inline const char* AdapterClassificationToString(AdapterClassification c) noexcept {
    switch (c) {
    case AdapterClassification::PhysicalLan:   return "PHYSICAL_LAN";
    case AdapterClassification::AppleUsb:      return "APPLE_USB";
    case AdapterClassification::Vpn:           return "VPN";
    case AdapterClassification::Virtual:       return "VIRTUAL";
    case AdapterClassification::Loopback:      return "LOOPBACK";
    case AdapterClassification::LinkLocalOnly: return "LINK_LOCAL_ONLY";
    case AdapterClassification::Disconnected:  return "DISCONNECTED";
    default:                                   return "UNKNOWN";
    }
}

AdapterClassification ClassifyAdapter(
    std::wstring_view name,
    std::wstring_view desc,
    std::string_view ipv4,
    bool is_up,
    bool is_wifi,
    bool is_ethernet,
    int* out_score = nullptr,
    bool is_wired_mode = false) noexcept;

struct AdapterDetails {
    std::wstring          name;
    std::wstring          description;
    std::string           ipv4_address;
    uint8_t               ipv4_prefix{24};
    bool                  is_physical{false};
    bool                  is_wifi{false};
    bool                  is_ethernet{false};
    bool                  is_up{false};
    AdapterClassification classification{AdapterClassification::Disconnected};
    int                   priority_score{0};
};

struct NetworkEnvironmentInfo {
    NetworkProfileCategory primary_profile{NetworkProfileCategory::Unknown};
    bool is_public_profile{false};
    bool network_isolation_suspected{false};
    bool public_rules_localsubnet{false};
    std::string best_adapter_ip;
    uint8_t best_adapter_prefix{24};
    std::wstring best_adapter_name;
    AdapterClassification best_adapter_class{AdapterClassification::Disconnected};
    int best_adapter_score{0};
    int active_physical_count{0};
    int active_vpn_count{0};
    int active_virtual_count{0};
    std::vector<AdapterDetails> adapters;

    // Probes network interfaces and firewall profiles.
    static NetworkEnvironmentInfo Probe(bool is_wired_mode = false) noexcept;

    // Emits structured logs and actionable firewall recommendations.
    void LogEnvironment() const noexcept;
};

// Process-scoped Windows Defender Firewall configuration for Public network opt-in.
// Handles UAC elevation safely via duwn-mirror.exe --firewall enable-public / disable-public / repair-all
// Verifies exact rule configuration (name, enabled, dir=in, action=allow, profile=public, program path, LocalSubnet).
// Only modifies rules for duwn-mirror.exe and uxplay.exe; never modifies global firewall state.
bool ConfigurePublicFirewall(bool allow, void* parent_hwnd = nullptr) noexcept;
bool VerifyPublicFirewallRulesExist(std::wstring* out_details = nullptr) noexcept;
bool VerifyDomainPrivateFirewallRulesExist(std::wstring* out_details = nullptr) noexcept;
bool ArePublicFirewallRulesPresent() noexcept;
bool RepairFirewallRules(void* parent_hwnd = nullptr) noexcept;

// Heuristic to detect AP client isolation on Wi-Fi or public networks
bool DetectSuspectedNetworkIsolation(const NetworkEnvironmentInfo& env, int mock_peer_count = -1) noexcept;

// Fixed CLI entry point invoked when process runs with --firewall <action>
int ExecuteFirewallCliCommand(std::wstring_view action) noexcept;

struct FirewallReconciliationResult {
    bool setting_value{false};
    bool needs_save{false};
    bool rules_missing_warning{false};
    bool rules_unexpected_warning{false};
};

enum class FirewallValidationState {
    Unknown,
    Checking,
    Ready,
    NeedsFix,
    BlockedByPolicy
};

inline const char* FirewallValidationStateToString(FirewallValidationState s) noexcept {
    switch (s) {
    case FirewallValidationState::Unknown:         return "UNKNOWN";
    case FirewallValidationState::Checking:        return "CHECKING";
    case FirewallValidationState::Ready:           return "READY";
    case FirewallValidationState::NeedsFix:        return "NEEDS_FIX";
    case FirewallValidationState::BlockedByPolicy: return "BLOCKED_BY_POLICY";
    default:                                       return "UNKNOWN";
    }
}

inline FirewallValidationState EvaluateFirewallValidationState(
    NetworkProfileCategory profile,
    bool allow_public_networks_setting,
    bool public_rules_valid,
    bool private_domain_rules_valid,
    bool is_checking = false,
    bool policy_blocks_inbound = false) noexcept
{
    if (is_checking) {
        return FirewallValidationState::Checking;
    }
    if (policy_blocks_inbound) {
        return FirewallValidationState::BlockedByPolicy;
    }
    if (profile == NetworkProfileCategory::Unknown) {
        return FirewallValidationState::Unknown;
    }
    if (profile == NetworkProfileCategory::Public) {
        if (!allow_public_networks_setting || !public_rules_valid) {
            return FirewallValidationState::NeedsFix;
        }
        return FirewallValidationState::Ready;
    }
    // Private or Domain
    if (!private_domain_rules_valid) {
        return FirewallValidationState::NeedsFix;
    }
    return FirewallValidationState::Ready;
}

inline FirewallReconciliationResult ReconcileFirewallState(bool setting_allowed, bool rules_valid, bool any_rules_present) noexcept {
    FirewallReconciliationResult res;
    if (setting_allowed && !rules_valid) {
        // State C: setting=true, rules missing -> reconcile to false, persist, show repair warning
        res.setting_value = false;
        res.needs_save = true;
        res.rules_missing_warning = true;
    } else if (!setting_allowed && any_rules_present) {
        // State D: setting=false, rules unexpectedly present -> reconcile to true, show active exposure warning
        res.setting_value = true;
        res.needs_save = false;
        res.rules_unexpected_warning = true;
    } else {
        // State A: setting=false, rules absent -> setting stays false
        // State B: setting=true, rules valid -> setting stays true
        res.setting_value = setting_allowed;
        res.needs_save = false;
    }
    return res;
}

} // namespace duwn::network
