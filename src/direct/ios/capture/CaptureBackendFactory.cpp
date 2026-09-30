#include "CaptureBackendFactory.h"

namespace duwn::direct::ios {

BackendSelectionResult CaptureBackendFactory::SelectBackendForScope(
    CaptureScope requested_scope,
    const SystemCaptureProbe& probe) {

    BackendSelectionResult res;

    // Rule 1: FullDisplay Direct Mirroring
    if (requested_scope == CaptureScope::FullDisplay) {
        // Modern ScreenCaptureKit is the sole system-wide FullDisplay backend.
        // It must be both available and verified on the runtime to succeed.
        if (probe.screen_capture_kit_available && probe.screen_capture_kit_verified) {
            res.status = CaptureSelectionStatus::Success;
            res.selected_backend_type = DirectCaptureBackendType::ScreenCaptureKit;
            res.effective_scope = CaptureScope::FullDisplay;
            res.is_legacy = false;
            res.is_unverified = false;
            res.fallback_to_airplay = false;
            res.backend = CreateBackend(DirectCaptureBackendType::ScreenCaptureKit);
            return res;
        }

        // Neither ReplayKit (RPScreenRecorder) nor InAppSnapshot (CADisplayLink)
        // are FullDisplay backends (both are strictly OwnApplication).
        // There is currently NO verified legacy FullDisplay backend.
        // STRICT INVARIANT: Never fake FullDisplay compatibility.
        res.status = CaptureSelectionStatus::FullDisplayDirectUnavailable;
        res.selected_backend_type = DirectCaptureBackendType::Unknown;
        res.effective_scope = CaptureScope::Unsupported;
        res.is_legacy = false;
        res.is_unverified = (probe.screen_capture_kit_available && !probe.screen_capture_kit_verified);
        res.fallback_to_airplay = true;
        res.failure_reason = "FULL_DISPLAY_DIRECT_UNAVAILABLE: ScreenCaptureKit is not available or unverified, and no verified legacy FullDisplay backend exists. Fallback to AirPlay.";
        res.backend = nullptr;
        return res;
    }

    // Rule 2: OwnApplication In-App Capture (dev / test / demo only)
    if (requested_scope == CaptureScope::OwnApplication) {
        // Prefer ReplayKit (in-app RPScreenRecorder) if available
        if (probe.replay_kit_legacy_available) {
            res.status = CaptureSelectionStatus::Success;
            res.selected_backend_type = DirectCaptureBackendType::ReplayKitLegacy;
            res.effective_scope = CaptureScope::OwnApplication;
            res.is_legacy = true;
            res.is_unverified = true;
            res.fallback_to_airplay = false;
            res.backend = CreateBackend(DirectCaptureBackendType::ReplayKitLegacy);
            return res;
        }

        // Otherwise fallback to CADisplayLink snapshotting
        if (probe.in_app_snapshot_available) {
            res.status = CaptureSelectionStatus::Success;
            res.selected_backend_type = DirectCaptureBackendType::InAppSnapshot;
            res.effective_scope = CaptureScope::OwnApplication;
            res.is_legacy = true;
            res.is_unverified = false;
            res.fallback_to_airplay = false;
            res.backend = CreateBackend(DirectCaptureBackendType::InAppSnapshot);
            return res;
        }

        res.status = CaptureSelectionStatus::UnsupportedScope;
        res.selected_backend_type = DirectCaptureBackendType::Unknown;
        res.effective_scope = CaptureScope::Unsupported;
        res.fallback_to_airplay = false;
        res.failure_reason = "UNSUPPORTED_SCOPE: No OwnApplication capture backend available.";
        res.backend = nullptr;
        return res;
    }

    // Rule 3: Unsupported Scope
    res.status = CaptureSelectionStatus::UnsupportedScope;
    res.selected_backend_type = DirectCaptureBackendType::Unknown;
    res.effective_scope = CaptureScope::Unsupported;
    res.fallback_to_airplay = true;
    res.failure_reason = "UNSUPPORTED_SCOPE: Requested capture scope is unsupported. Fallback to AirPlay.";
    res.backend = nullptr;
    return res;
}

CaptureCapabilities CaptureBackendFactory::GetStaticBackendCapabilities(DirectCaptureBackendType type) {
    CaptureCapabilities caps;

    switch (type) {
        case DirectCaptureBackendType::ScreenCaptureKit:
            caps.backend_type = DirectCaptureBackendType::ScreenCaptureKit;
            caps.backend_name = "ScreenCaptureKit";
            caps.capture_scope = CaptureScope::FullDisplay;
            caps.is_legacy = false;
            caps.uses_deprecated_api = false;
            caps.supports_video = true;
            caps.supports_system_audio = true;
            caps.supports_microphone = false;
            caps.supports_background_capture = true;
            caps.supported_pixel_formats = {
                CapturePixelFormat::NV12_VideoRange,
                CapturePixelFormat::NV12_FullRange,
                CapturePixelFormat::BGRA32
            };
            caps.preferred_pixel_format = CapturePixelFormat::NV12_VideoRange;
            caps.supports_native_yuv = true;
            caps.verification_status = BackendVerificationStatus::SourceOnlyNotBuilt;
            break;

        case DirectCaptureBackendType::ReplayKitLegacy:
            caps.backend_type = DirectCaptureBackendType::ReplayKitLegacy;
            caps.backend_name = "ReplayKit (Legacy Deprecated, OwnApplication Only)";
            caps.capture_scope = CaptureScope::OwnApplication; // In-app capture only; records host app
            caps.is_legacy = true;
            caps.uses_deprecated_api = true;
            caps.supports_video = true;
            caps.supports_system_audio = true;
            caps.supports_microphone = true;
            caps.supports_background_capture = false;
            caps.supported_pixel_formats = {
                CapturePixelFormat::NV12_VideoRange,
                CapturePixelFormat::NV12_FullRange,
                CapturePixelFormat::BGRA32
            };
            caps.preferred_pixel_format = CapturePixelFormat::NV12_VideoRange;
            caps.supports_native_yuv = true;
            caps.verification_status = BackendVerificationStatus::LegacyUnverified;
            break;

        case DirectCaptureBackendType::InAppSnapshot:
            caps.backend_type = DirectCaptureBackendType::InAppSnapshot;
            caps.backend_name = "In-App CADisplayLink Snapshot (OwnApplication Only)";
            caps.capture_scope = CaptureScope::OwnApplication;
            caps.is_legacy = true;
            caps.uses_deprecated_api = true;
            caps.supports_video = true;
            caps.supports_system_audio = false;
            caps.supports_microphone = false;
            caps.supports_background_capture = false;
            caps.supported_pixel_formats = {
                CapturePixelFormat::BGRA32
            };
            caps.preferred_pixel_format = CapturePixelFormat::BGRA32;
            caps.supports_native_yuv = false;
            caps.verification_status = BackendVerificationStatus::SourceOnlyNotBuilt;
            break;

        case DirectCaptureBackendType::Unknown:
        default:
            caps.backend_type = DirectCaptureBackendType::Unknown;
            caps.backend_name = "Unknown";
            caps.capture_scope = CaptureScope::Unsupported;
            caps.is_legacy = false;
            caps.uses_deprecated_api = false;
            caps.supports_video = false;
            caps.verification_status = BackendVerificationStatus::Unverified;
            break;
    }

    return caps;
}

#if !defined(__APPLE__)
SystemCaptureProbe CaptureBackendFactory::ProbeRuntimeSystem() noexcept {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = false;
    probe.screen_capture_kit_verified = false;
    probe.replay_kit_legacy_available = false;
    probe.in_app_snapshot_available = false;
    return probe;
}

BackendSelectionResult CaptureBackendFactory::CreateOptimalBackend(CaptureScope requested_scope) {
    return SelectBackendForScope(requested_scope, ProbeRuntimeSystem());
}

std::unique_ptr<DuwnCaptureBackend> CaptureBackendFactory::CreateBackend(DirectCaptureBackendType) {
    return nullptr;
}
#endif

} // namespace duwn::direct::ios
