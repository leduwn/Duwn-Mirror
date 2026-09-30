#pragma once
// DuwnCaptureTypes.h — Core data types for Duwn Direct iOS/iPadOS capture architecture.
// Exposes CaptureScope, CaptureCapabilities, CapturedVideoFrame, CapturedAudioFrame.
// Decoupled from Apple frameworks: NO ReplayKit or ScreenCaptureKit types leak to consumers.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CoreMedia.h>
#endif

namespace duwn::direct::ios {

// ============================================================================
// 1. Capture Scope Model
// ============================================================================
// Every backend must declare its actual capture scope.
// Never treat OwnApplication as system-wide screen mirroring.
enum class CaptureScope : uint8_t {
    Unsupported    = 0,
    FullDisplay    = 1, // System-wide screen capture (SpringBoard, all apps)
    OwnApplication = 2  // In-app window / view hierarchy capture only
};

inline const char* CaptureScopeToString(CaptureScope scope) noexcept {
    switch (scope) {
        case CaptureScope::FullDisplay:    return "FullDisplay";
        case CaptureScope::OwnApplication: return "OwnApplication";
        case CaptureScope::Unsupported:
        default:                           return "Unsupported";
    }
}

// ============================================================================
// 2. Capture Backend Type
// ============================================================================
enum class DirectCaptureBackendType : uint8_t {
    Unknown          = 0,
    ScreenCaptureKit = 1, // Modern preferred system-wide backend
    ReplayKitLegacy  = 2, // Legacy deprecated ReplayKit capture
    InAppSnapshot    = 3  // CADisplayLink / drawViewHierarchy (OwnApplication only)
};

inline const char* DirectCaptureBackendTypeToString(DirectCaptureBackendType type) noexcept {
    switch (type) {
        case DirectCaptureBackendType::ScreenCaptureKit: return "ScreenCaptureKit";
        case DirectCaptureBackendType::ReplayKitLegacy:  return "ReplayKitLegacy";
        case DirectCaptureBackendType::InAppSnapshot:    return "InAppSnapshot";
        case DirectCaptureBackendType::Unknown:
        default:                                         return "Unknown";
    }
}

// ============================================================================
// 3. Evidence / Verification Status
// ============================================================================
enum class BackendVerificationStatus : uint8_t {
    Unverified             = 0,
    SourceOnlyNotBuilt     = 1, // Source written, not built on Apple hardware
    LegacyUnverified       = 2, // Deprecated API, unverified against current SDKs
    AppleBuildVerified     = 3, // Compiled cleanly on Xcode SDK
    PhysicalDeviceVerified = 4  // Verified on physical iOS device
};

inline const char* BackendVerificationStatusToString(BackendVerificationStatus status) noexcept {
    switch (status) {
        case BackendVerificationStatus::SourceOnlyNotBuilt:     return "SOURCE_ONLY_NOT_BUILT";
        case BackendVerificationStatus::LegacyUnverified:       return "LEGACY_UNVERIFIED";
        case BackendVerificationStatus::AppleBuildVerified:     return "APPLE_BUILD_VERIFIED";
        case BackendVerificationStatus::PhysicalDeviceVerified: return "PHYSICAL_DEVICE_VERIFIED";
        case BackendVerificationStatus::Unverified:
        default:                                                return "UNVERIFIED";
    }
}

// ============================================================================
// 4. Pixel Formats & Timestamps
// ============================================================================
enum class CapturePixelFormat : uint32_t {
    Unknown         = 0,
    NV12_VideoRange = 0x34323076, // '420v' kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
    NV12_FullRange  = 0x34323066, // '420f' kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
    BGRA32          = 0x42475241  // 'BGRA' kCVPixelFormatType_32BGRA
};

inline const char* CapturePixelFormatToString(CapturePixelFormat format) noexcept {
    switch (format) {
        case CapturePixelFormat::NV12_VideoRange: return "NV12 (Video Range '420v')";
        case CapturePixelFormat::NV12_FullRange:  return "NV12 (Full Range '420f')";
        case CapturePixelFormat::BGRA32:          return "BGRA32 ('BGRA')";
        case CapturePixelFormat::Unknown:
        default:                                  return "Unknown";
    }
}

enum class CaptureOrientation : uint8_t {
    Unknown            = 0,
    Portrait           = 1,
    PortraitUpsideDown = 2,
    LandscapeLeft      = 3,
    LandscapeRight     = 4
};

struct CaptureTimestamp {
    uint64_t source_timestamp_ns{0};   // Nanosecond PTS from capture hardware / CMSampleBuffer
    uint64_t callback_timestamp_ns{0}; // Monotonic host nanoseconds when callback was entered

    uint64_t DeliveryDelayNs() const noexcept {
        return (callback_timestamp_ns > source_timestamp_ns)
            ? (callback_timestamp_ns - source_timestamp_ns)
            : 0;
    }
};

// ============================================================================
// 5. Captured Frames
// ============================================================================
struct CapturedVideoFrame {
#if defined(__APPLE__)
    CVPixelBufferRef pixel_buffer{nullptr};
#else
    void* pixel_buffer{nullptr};
#endif
    uint32_t visible_width{0};
    uint32_t visible_height{0};
    CapturePixelFormat pixel_format{CapturePixelFormat::Unknown};
    CaptureTimestamp timestamp{};
    CaptureOrientation orientation{CaptureOrientation::Unknown};
    uint64_t frame_sequence{0};

    CapturedVideoFrame() = default;

#if defined(__APPLE__)
    explicit CapturedVideoFrame(CVPixelBufferRef buffer,
                                uint32_t width,
                                uint32_t height,
                                CapturePixelFormat format,
                                CaptureTimestamp ts,
                                CaptureOrientation orient,
                                uint64_t seq)
        : visible_width(width),
          visible_height(height),
          pixel_format(format),
          timestamp(ts),
          orientation(orient),
          frame_sequence(seq) {
        if (buffer) {
            pixel_buffer = CVPixelBufferRetain(buffer);
        }
    }

    ~CapturedVideoFrame() {
        if (pixel_buffer) {
            CVPixelBufferRelease(pixel_buffer);
            pixel_buffer = nullptr;
        }
    }

    CapturedVideoFrame(const CapturedVideoFrame& other)
        : visible_width(other.visible_width),
          visible_height(other.visible_height),
          pixel_format(other.pixel_format),
          timestamp(other.timestamp),
          orientation(other.orientation),
          frame_sequence(other.frame_sequence) {
        if (other.pixel_buffer) {
            pixel_buffer = CVPixelBufferRetain(other.pixel_buffer);
        }
    }

    CapturedVideoFrame& operator=(const CapturedVideoFrame& other) {
        if (this != &other) {
            if (pixel_buffer) {
                CVPixelBufferRelease(pixel_buffer);
            }
            visible_width = other.visible_width;
            visible_height = other.visible_height;
            pixel_format = other.pixel_format;
            timestamp = other.timestamp;
            orientation = other.orientation;
            frame_sequence = other.frame_sequence;
            pixel_buffer = other.pixel_buffer ? CVPixelBufferRetain(other.pixel_buffer) : nullptr;
        }
        return *this;
    }

    CapturedVideoFrame(CapturedVideoFrame&& other) noexcept
        : pixel_buffer(other.pixel_buffer),
          visible_width(other.visible_width),
          visible_height(other.visible_height),
          pixel_format(other.pixel_format),
          timestamp(other.timestamp),
          orientation(other.orientation),
          frame_sequence(other.frame_sequence) {
        other.pixel_buffer = nullptr;
    }

    CapturedVideoFrame& operator=(CapturedVideoFrame&& other) noexcept {
        if (this != &other) {
            if (pixel_buffer) {
                CVPixelBufferRelease(pixel_buffer);
            }
            pixel_buffer = other.pixel_buffer;
            visible_width = other.visible_width;
            visible_height = other.visible_height;
            pixel_format = other.pixel_format;
            timestamp = other.timestamp;
            orientation = other.orientation;
            frame_sequence = other.frame_sequence;
            other.pixel_buffer = nullptr;
        }
        return *this;
    }
#else
    ~CapturedVideoFrame() = default;
    CapturedVideoFrame(const CapturedVideoFrame&) = default;
    CapturedVideoFrame& operator=(const CapturedVideoFrame&) = default;
    CapturedVideoFrame(CapturedVideoFrame&&) noexcept = default;
    CapturedVideoFrame& operator=(CapturedVideoFrame&&) noexcept = default;
#endif

    bool IsValid() const noexcept {
        return pixel_buffer != nullptr && visible_width > 0 && visible_height > 0;
    }
};

struct CapturedAudioFrame {
    std::vector<uint8_t> pcm_data;
    uint32_t sample_rate{48000};
    uint16_t channel_count{2};
    uint16_t bits_per_sample{16};
    uint64_t source_timestamp_ns{0};
    uint64_t frame_sequence{0};
    bool is_app_audio{true}; // true = system/app audio, false = microphone
};

// ============================================================================
// 6. Capability Model
// ============================================================================
// Exposes declared runtime capabilities.
// STRICT INVARIANTS:
// - Do NOT infer capabilities from iPhone model string.
// - Do NOT infer capabilities solely from OS version string.
// - Runtime/API availability is authoritative.
struct CaptureCapabilities {
    DirectCaptureBackendType backend_type{DirectCaptureBackendType::Unknown};
    std::string backend_name;
    CaptureScope capture_scope{CaptureScope::Unsupported};
    bool is_legacy{false};
    bool uses_deprecated_api{false};
    bool supports_video{true};
    bool supports_system_audio{false};
    bool supports_microphone{false};
    bool supports_background_capture{false};
    std::vector<CapturePixelFormat> supported_pixel_formats;
    CapturePixelFormat preferred_pixel_format{CapturePixelFormat::NV12_VideoRange};

    uint32_t screen_width{0};
    uint32_t screen_height{0};
    float screen_scale{1.0f};
    uint32_t max_observed_cadence_fps{60};
    bool supports_native_yuv{false};

    BackendVerificationStatus verification_status{BackendVerificationStatus::SourceOnlyNotBuilt};
};

} // namespace duwn::direct::ios
