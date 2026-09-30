#pragma once
// DuwnCaptureBackend.h — Abstract capture interface for iOS/iPadOS screen capture backends.
// Decouples application layer from ScreenCaptureKit, ReplayKit, and UIKit APIs.
// Every backend must declare its actual CaptureScope and CaptureCapabilities.

#include "DuwnCaptureTypes.h"
#include "DuwnCaptureFrameSlot.h"
#include <functional>
#include <memory>

namespace duwn::direct::ios {

class DuwnCaptureBackend {
public:
    virtual ~DuwnCaptureBackend() = default;

    // Initializes backend and queries hardware/system capabilities.
    virtual bool Initialize() = 0;

    // Starts asynchronous screen capture.
    virtual bool StartCapture() = 0;

    // Stops screen capture and flushes slots.
    virtual void StopCapture() = 0;

    // Checks if capture is currently active.
    virtual bool IsCapturing() const noexcept = 0;

    // Retrieves declared runtime capabilities.
    virtual CaptureCapabilities GetCapabilities() const = 0;

    // Convenience accessor for capture scope
    virtual CaptureScope GetCaptureScope() const noexcept {
        return GetCapabilities().capture_scope;
    }

    // Convenience accessor for backend type
    virtual DirectCaptureBackendType GetBackendType() const noexcept {
        return GetCapabilities().backend_type;
    }

    // Access to the 1-frame freshest video slot.
    virtual DuwnCaptureFrameSlot& GetVideoFrameSlot() noexcept = 0;

    // Optional notification callback when a new video frame is placed in slot.
    virtual void SetVideoFrameCallback(std::function<void(const CapturedVideoFrame&)> callback) = 0;

    // Audio callback for captured application / microphone audio chunks.
    virtual void SetAudioFrameCallback(std::function<void(const CapturedAudioFrame&)> callback) = 0;

    // Metrics snapshot
    virtual CaptureSlotMetrics GetSlotMetrics() const noexcept = 0;
};

} // namespace duwn::direct::ios
