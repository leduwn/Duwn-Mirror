#include "direct/ios/capture/CaptureBackendFactory.h"
#include <string>

using namespace duwn::direct::ios;

// 1. Modern ScreenCaptureKit backend selected for FullDisplay when available and verified
DUWN_TEST(DirectCapture_ModernBackendPreferredWhenAvailableAndVerified) {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = true;
    probe.screen_capture_kit_verified = true;
    probe.replay_kit_legacy_available = true;
    probe.in_app_snapshot_available = true;

    auto result = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);

    DUWN_ASSERT(result.status == CaptureSelectionStatus::Success);
    DUWN_ASSERT(result.selected_backend_type == DirectCaptureBackendType::ScreenCaptureKit);
    DUWN_ASSERT(result.effective_scope == CaptureScope::FullDisplay);
    DUWN_ASSERT(!result.is_legacy);
    DUWN_ASSERT(!result.fallback_to_airplay);
}

// 2. FullDisplay never selects OwnApplication backends (neither ReplayKit nor InAppSnapshot)
DUWN_TEST(DirectCapture_FullDisplayNeverSelectsOwnApplicationBackends) {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = false;
    probe.screen_capture_kit_verified = false;
    probe.replay_kit_legacy_available = true; // ReplayKit is present
    probe.in_app_snapshot_available = true;   // InAppSnapshot is present

    auto result = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);

    // CRITICAL INVARIANT: FullDisplay Direct mirroring must NEVER select OwnApplication backends!
    DUWN_ASSERT(result.status == CaptureSelectionStatus::FullDisplayDirectUnavailable);
    DUWN_ASSERT(result.selected_backend_type != DirectCaptureBackendType::ReplayKitLegacy);
    DUWN_ASSERT(result.selected_backend_type != DirectCaptureBackendType::InAppSnapshot);
    DUWN_ASSERT(result.selected_backend_type == DirectCaptureBackendType::Unknown);
    DUWN_ASSERT(result.effective_scope == CaptureScope::Unsupported);
    DUWN_ASSERT(result.backend == nullptr);
    DUWN_ASSERT(result.fallback_to_airplay);
}

// 3. Unverified ScreenCaptureKit returns FullDisplayDirectUnavailable with AirPlay fallback
DUWN_TEST(DirectCapture_UnverifiedScreenCaptureKitReturnsUnavailable) {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = true;
    probe.screen_capture_kit_verified = false; // Available in SDK headers but not verified on runtime
    probe.replay_kit_legacy_available = false;
    probe.in_app_snapshot_available = false;

    auto result = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);

    DUWN_ASSERT(result.status == CaptureSelectionStatus::FullDisplayDirectUnavailable);
    DUWN_ASSERT(result.is_unverified);
    DUWN_ASSERT(result.backend == nullptr);
    DUWN_ASSERT(result.fallback_to_airplay);
    DUWN_ASSERT(result.failure_reason.find("FULL_DISPLAY_DIRECT_UNAVAILABLE") != std::string::npos);
}

// 4. AirPlay fallback remains possible across all unavailable and unsupported scenarios
DUWN_TEST(DirectCapture_AirPlayFallbackRemainsPossible) {
    // Scenario A: FullDisplay direct unavailable
    SystemCaptureProbe probe_none;
    auto res_a = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe_none);
    DUWN_ASSERT(res_a.fallback_to_airplay);

    // Scenario B: Unsupported scope requested
    auto res_b = CaptureBackendFactory::SelectBackendForScope(CaptureScope::Unsupported, probe_none);
    DUWN_ASSERT(res_b.fallback_to_airplay);

    // Scenario C: Modern backend available & verified -> no AirPlay fallback needed
    SystemCaptureProbe probe_modern;
    probe_modern.screen_capture_kit_available = true;
    probe_modern.screen_capture_kit_verified = true;
    auto res_c = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe_modern);
    DUWN_ASSERT(!res_c.fallback_to_airplay);
}

// 5. Zero device-model dependency
DUWN_TEST(DirectCapture_ZeroDeviceModelDependency) {
    // Backend selection is purely governed by SystemCaptureProbe and CaptureScope.
    // Telemetry strings (iPhone model, OS version) do not change selection outcomes.
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = true;
    probe.screen_capture_kit_verified = true;
    probe.replay_kit_legacy_available = false;

    auto res_old_device = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);
    auto res_new_device = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);

    DUWN_ASSERT(res_old_device.status == res_new_device.status);
    DUWN_ASSERT(res_old_device.selected_backend_type == res_new_device.selected_backend_type);
    DUWN_ASSERT(res_old_device.effective_scope == res_new_device.effective_scope);
    DUWN_ASSERT(res_old_device.is_legacy == res_new_device.is_legacy);
}

// 6. Static capabilities audit: ReplayKit declared OwnApplication and legacy deprecated
DUWN_TEST(DirectCapture_CapabilitiesAudit_ReplayKitScopeAndDeprecation) {
    auto legacy_caps = CaptureBackendFactory::GetStaticBackendCapabilities(DirectCaptureBackendType::ReplayKitLegacy);
    DUWN_ASSERT(legacy_caps.backend_type == DirectCaptureBackendType::ReplayKitLegacy);
    DUWN_ASSERT(legacy_caps.capture_scope == CaptureScope::OwnApplication); // STRICT R2.1 INVARIANT!
    DUWN_ASSERT(legacy_caps.is_legacy);
    DUWN_ASSERT(legacy_caps.uses_deprecated_api);
    DUWN_ASSERT(legacy_caps.verification_status == BackendVerificationStatus::LegacyUnverified);
    DUWN_ASSERT(std::string(BackendVerificationStatusToString(legacy_caps.verification_status)) == "LEGACY_UNVERIFIED");

    auto modern_caps = CaptureBackendFactory::GetStaticBackendCapabilities(DirectCaptureBackendType::ScreenCaptureKit);
    DUWN_ASSERT(modern_caps.backend_type == DirectCaptureBackendType::ScreenCaptureKit);
    DUWN_ASSERT(modern_caps.capture_scope == CaptureScope::FullDisplay);
    DUWN_ASSERT(!modern_caps.is_legacy);
    DUWN_ASSERT(!modern_caps.uses_deprecated_api);
    DUWN_ASSERT(modern_caps.verification_status == BackendVerificationStatus::SourceOnlyNotBuilt);
    DUWN_ASSERT(std::string(BackendVerificationStatusToString(modern_caps.verification_status)) == "SOURCE_ONLY_NOT_BUILT");

    auto inapp_caps = CaptureBackendFactory::GetStaticBackendCapabilities(DirectCaptureBackendType::InAppSnapshot);
    DUWN_ASSERT(inapp_caps.backend_type == DirectCaptureBackendType::InAppSnapshot);
    DUWN_ASSERT(inapp_caps.capture_scope == CaptureScope::OwnApplication);
    DUWN_ASSERT(inapp_caps.is_legacy);
    DUWN_ASSERT(inapp_caps.uses_deprecated_api);
}

// 7. ReplayKit selectable for OwnApplication scope
DUWN_TEST(DirectCapture_ReplayKitSelectableForOwnApplicationScope) {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = false;
    probe.replay_kit_legacy_available = true;
    probe.in_app_snapshot_available = true;

    // FullDisplay rejects ReplayKit even if available
    auto res_full = CaptureBackendFactory::SelectBackendForScope(CaptureScope::FullDisplay, probe);
    DUWN_ASSERT(res_full.status == CaptureSelectionStatus::FullDisplayDirectUnavailable);
    DUWN_ASSERT(res_full.fallback_to_airplay);

    // OwnApplication accepts ReplayKit as in-app capture backend
    auto res_own = CaptureBackendFactory::SelectBackendForScope(CaptureScope::OwnApplication, probe);
    DUWN_ASSERT(res_own.status == CaptureSelectionStatus::Success);
    DUWN_ASSERT(res_own.selected_backend_type == DirectCaptureBackendType::ReplayKitLegacy);
    DUWN_ASSERT(res_own.effective_scope == CaptureScope::OwnApplication);
    DUWN_ASSERT(res_own.is_legacy);
    DUWN_ASSERT(!res_own.fallback_to_airplay);
}

// 8. InAppSnapshot selectable for OwnApplication when ReplayKit unavailable
DUWN_TEST(DirectCapture_OwnApplicationFallbackToInAppSnapshot) {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = false;
    probe.replay_kit_legacy_available = false;
    probe.in_app_snapshot_available = true;

    auto res = CaptureBackendFactory::SelectBackendForScope(CaptureScope::OwnApplication, probe);
    DUWN_ASSERT(res.status == CaptureSelectionStatus::Success);
    DUWN_ASSERT(res.selected_backend_type == DirectCaptureBackendType::InAppSnapshot);
    DUWN_ASSERT(res.effective_scope == CaptureScope::OwnApplication);
    DUWN_ASSERT(res.is_legacy);
    DUWN_ASSERT(!res.fallback_to_airplay);
}
