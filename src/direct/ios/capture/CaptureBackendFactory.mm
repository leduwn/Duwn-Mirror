#include "CaptureBackendFactory.h"
#include "ScreenCaptureKitBackend.h"
#include "ReplayKitLegacyBackend.h"
#include "InAppSnapshotBackend.h"

#if defined(__APPLE__)
namespace duwn::direct::ios {

SystemCaptureProbe CaptureBackendFactory::ProbeRuntimeSystem() noexcept {
    SystemCaptureProbe probe;
    probe.screen_capture_kit_available = ScreenCaptureKitBackend::IsAvailable();
    probe.screen_capture_kit_verified = ScreenCaptureKitBackend::IsVerifiedOnRuntime();
    probe.replay_kit_legacy_available = ReplayKitLegacyBackend::IsAvailable();
    probe.in_app_snapshot_available = InAppSnapshotBackend::IsAvailable();
    return probe;
}

BackendSelectionResult CaptureBackendFactory::CreateOptimalBackend(CaptureScope requested_scope) {
    return SelectBackendForScope(requested_scope, ProbeRuntimeSystem());
}

std::unique_ptr<DuwnCaptureBackend> CaptureBackendFactory::CreateBackend(DirectCaptureBackendType type) {
    switch (type) {
        case DirectCaptureBackendType::ScreenCaptureKit:
            return std::make_unique<ScreenCaptureKitBackend>();
        case DirectCaptureBackendType::ReplayKitLegacy:
            return std::make_unique<ReplayKitLegacyBackend>();
        case DirectCaptureBackendType::InAppSnapshot:
            return std::make_unique<InAppSnapshotBackend>();
        case DirectCaptureBackendType::Unknown:
        default:
            return nullptr;
    }
}

} // namespace duwn::direct::ios
#endif
