#pragma once
// CaptureBackendFactory.h — Capability-driven iOS/iPadOS capture backend selector.
// Implements strict selection rules for FullDisplay Direct mirroring vs OwnApplication snapshotting.
//
// STRICT INVARIANTS:
// 1. For CaptureScope::FullDisplay:
//    - Prefer ScreenCaptureKit when available.
//    - Otherwise return FULL_DISPLAY_DIRECT_UNAVAILABLE (fallback to AirPlay).
//    - RPScreenRecorder (startCaptureWithHandler:) and CADisplayLink snapshotting are
//      OwnApplication only and NEVER participate as FullDisplay mirroring backends.
// 2. Zero device-model branching (no 'if iPhone...').
//
// SOURCE OWNERSHIP ARCHITECTURE:
// - Platform-independent selection policy and static capability matrices are owned by CaptureBackendFactory.cpp.
// - Apple runtime construction and live hardware probing are owned by CaptureBackendFactory.mm on Apple builds,
//   and stubbed out under #if !defined(__APPLE__) in CaptureBackendFactory.cpp on non-Apple hosts.
// - Zero duplicate symbols exist on either Windows or Apple builds.

#include "DuwnCaptureBackend.h"
#include <memory>
#include <string>

namespace duwn::direct::ios {

// Runtime availability probe results
struct SystemCaptureProbe {
    bool screen_capture_kit_available{false};
    bool screen_capture_kit_verified{false};
    bool replay_kit_legacy_available{false};
    bool in_app_snapshot_available{false};
};

enum class CaptureSelectionStatus : uint8_t {
    Success                      = 0,
    FullDisplayDirectUnavailable = 1,
    UnsupportedScope             = 2
};

inline const char* CaptureSelectionStatusToString(CaptureSelectionStatus status) noexcept {
    switch (status) {
        case CaptureSelectionStatus::Success:                      return "SUCCESS";
        case CaptureSelectionStatus::FullDisplayDirectUnavailable: return "FULL_DISPLAY_DIRECT_UNAVAILABLE";
        case CaptureSelectionStatus::UnsupportedScope:             return "UNSUPPORTED_SCOPE";
        default:                                                   return "UNKNOWN";
    }
}

struct BackendSelectionResult {
    CaptureSelectionStatus status{CaptureSelectionStatus::Success};
    DirectCaptureBackendType selected_backend_type{DirectCaptureBackendType::Unknown};
    CaptureScope effective_scope{CaptureScope::Unsupported};
    bool is_legacy{false};
    bool is_unverified{false};
    bool fallback_to_airplay{false};
    std::string failure_reason;
    std::unique_ptr<DuwnCaptureBackend> backend{nullptr};
};

class CaptureBackendFactory {
public:
    // Core selection rule (platform-independent, implemented in CaptureBackendFactory.cpp):
    // Evaluates requested scope and system probe to select the authoritative backend.
    static BackendSelectionResult SelectBackendForScope(
        CaptureScope requested_scope,
        const SystemCaptureProbe& probe);

    // Declared static capabilities (platform-independent, implemented in CaptureBackendFactory.cpp)
    static CaptureCapabilities GetStaticBackendCapabilities(DirectCaptureBackendType type);

    // Apple runtime construction & live probing:
    // - On Apple builds (__APPLE__): implemented in CaptureBackendFactory.mm
    // - On non-Apple builds (!__APPLE__): fallback stubs in CaptureBackendFactory.cpp
    static SystemCaptureProbe ProbeRuntimeSystem() noexcept;

    static BackendSelectionResult CreateOptimalBackend(
        CaptureScope requested_scope = CaptureScope::FullDisplay);

    static std::unique_ptr<DuwnCaptureBackend> CreateBackend(DirectCaptureBackendType type);
};

} // namespace duwn::direct::ios
