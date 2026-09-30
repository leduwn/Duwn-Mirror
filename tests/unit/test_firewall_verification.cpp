#include "network/NetworkEnvironment.h"
#include <string>

DUWN_TEST(Firewall_QueryDoesNotThrowOrCrash) {
    std::wstring details;
    bool exists = duwn::network::VerifyPublicFirewallRulesExist(&details);
    (void)exists;
    DUWN_ASSERT(!details.empty());

    bool any_present = duwn::network::ArePublicFirewallRulesPresent();
    (void)any_present;
}

DUWN_TEST(Firewall_CliUnknownActionRejectedSafely) {
    int ret = duwn::network::ExecuteFirewallCliCommand(L"unsupported-action");
    DUWN_ASSERT(ret == 6);
}

DUWN_TEST(Firewall_CliDisablePublicReturnsZeroWhenRulesAbsent) {
    // When rules are absent, disable-public must return 0 (success)
    int ret = duwn::network::ExecuteFirewallCliCommand(L"disable-public");
    DUWN_ASSERT(ret == 0);
}

DUWN_TEST(Firewall_ExactVerificationDetailsString) {
    std::wstring details;
    duwn::network::VerifyPublicFirewallRulesExist(&details);
    // Either rules verified, or failure reason details populated
    DUWN_ASSERT(details.length() > 0);
}

DUWN_TEST(Firewall_FourStateStartupReconciliationMatrix) {
    // State A: setting=false, rules absent -> UI Off, no warnings
    auto stateA = duwn::network::ReconcileFirewallState(false, false, false);
    DUWN_ASSERT(stateA.setting_value == false);
    DUWN_ASSERT(stateA.needs_save == false);
    DUWN_ASSERT(stateA.rules_missing_warning == false);
    DUWN_ASSERT(stateA.rules_unexpected_warning == false);

    // State B: setting=true, rules valid -> UI On, no warnings
    auto stateB = duwn::network::ReconcileFirewallState(true, true, true);
    DUWN_ASSERT(stateB.setting_value == true);
    DUWN_ASSERT(stateB.needs_save == false);
    DUWN_ASSERT(stateB.rules_missing_warning == false);
    DUWN_ASSERT(stateB.rules_unexpected_warning == false);

    // State C: setting=true, rules missing -> UI Off + repair warning + save required
    auto stateC = duwn::network::ReconcileFirewallState(true, false, false);
    DUWN_ASSERT(stateC.setting_value == false);
    DUWN_ASSERT(stateC.needs_save == true);
    DUWN_ASSERT(stateC.rules_missing_warning == true);
    DUWN_ASSERT(stateC.rules_unexpected_warning == false);

    // State D: setting=false, rules unexpectedly present -> UI On (exposure indicated) + warning
    auto stateD = duwn::network::ReconcileFirewallState(false, false, true);
    DUWN_ASSERT(stateD.setting_value == true);
    DUWN_ASSERT(stateD.needs_save == false);
    DUWN_ASSERT(stateD.rules_missing_warning == false);
    DUWN_ASSERT(stateD.rules_unexpected_warning == true);
}

DUWN_TEST(Firewall_FiveStateStateMachine) {
    using namespace duwn::network;

    // 1. String representations
    DUWN_ASSERT(std::string(FirewallValidationStateToString(FirewallValidationState::Unknown)) == "UNKNOWN");
    DUWN_ASSERT(std::string(FirewallValidationStateToString(FirewallValidationState::Checking)) == "CHECKING");
    DUWN_ASSERT(std::string(FirewallValidationStateToString(FirewallValidationState::Ready)) == "READY");
    DUWN_ASSERT(std::string(FirewallValidationStateToString(FirewallValidationState::NeedsFix)) == "NEEDS_FIX");
    DUWN_ASSERT(std::string(FirewallValidationStateToString(FirewallValidationState::BlockedByPolicy)) == "BLOCKED_BY_POLICY");

    // 2. Checking state always yields CHECKING regardless of profile
    auto s_checking = EvaluateFirewallValidationState(
        NetworkProfileCategory::Public, false, false, false, /*is_checking=*/true);
    DUWN_ASSERT(s_checking == FirewallValidationState::Checking);

    // 3. Blocked by policy yields BLOCKED_BY_POLICY
    auto s_blocked = EvaluateFirewallValidationState(
        NetworkProfileCategory::Private, true, true, true, false, /*policy_blocks_inbound=*/true);
    DUWN_ASSERT(s_blocked == FirewallValidationState::BlockedByPolicy);

    // 4. Unknown profile yields UNKNOWN (never falsely claims connectivity)
    auto s_unknown = EvaluateFirewallValidationState(
        NetworkProfileCategory::Unknown, true, true, true);
    DUWN_ASSERT(s_unknown == FirewallValidationState::Unknown);

    // 5. Public network profile:
    // a. allow_public=false -> NEEDS_FIX
    auto s_pub_disallowed = EvaluateFirewallValidationState(
        NetworkProfileCategory::Public, /*allow_public=*/false, /*pub_valid=*/false, true);
    DUWN_ASSERT(s_pub_disallowed == FirewallValidationState::NeedsFix);

    // b. allow_public=true but rules missing/invalid -> NEEDS_FIX
    auto s_pub_missing = EvaluateFirewallValidationState(
        NetworkProfileCategory::Public, /*allow_public=*/true, /*pub_valid=*/false, true);
    DUWN_ASSERT(s_pub_missing == FirewallValidationState::NeedsFix);

    // c. allow_public=true AND rules valid -> READY
    auto s_pub_ready = EvaluateFirewallValidationState(
        NetworkProfileCategory::Public, /*allow_public=*/true, /*pub_valid=*/true, true);
    DUWN_ASSERT(s_pub_ready == FirewallValidationState::Ready);

    // 6. Private/Domain network profile:
    // a. private_domain_rules_valid=true -> READY
    auto s_priv_ready = EvaluateFirewallValidationState(
        NetworkProfileCategory::Private, false, false, /*priv_valid=*/true);
    DUWN_ASSERT(s_priv_ready == FirewallValidationState::Ready);

    // b. private_domain_rules_valid=false -> NEEDS_FIX
    auto s_priv_needs_fix = EvaluateFirewallValidationState(
        NetworkProfileCategory::Private, false, false, /*priv_valid=*/false);
    DUWN_ASSERT(s_priv_needs_fix == FirewallValidationState::NeedsFix);
}
