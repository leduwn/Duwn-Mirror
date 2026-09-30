#pragma once

#include "VideoFrame.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace duwn::video {

enum class AspectRatioMode { AspectLocked, Fit, Fill, Stretch };
enum class PresentResult { Ok, Skipped, DeviceLost, Fatal };

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;
    virtual bool Init(uint32_t width, uint32_t height) noexcept = 0;
    virtual PresentResult Present(VideoFrame& frame, bool skip_wait) noexcept = 0;
    virtual HANDLE GetFrameLatencyWaitableObject() const noexcept = 0;
    virtual void SignalResize(uint32_t width, uint32_t height) noexcept = 0;
    virtual void SetAspectRatioMode(AspectRatioMode mode) noexcept = 0;
    virtual void SetPixelPerfect(int mode) noexcept = 0;
    virtual void SetScalingQuality(int quality) noexcept = 0;
    virtual void SetColorSpace(int range, int matrix) noexcept = 0;
    virtual void SetColorControl(size_t index, int value) noexcept = 0;
    virtual bool SupportsColorControl(size_t index) const noexcept = 0;
    virtual uint32_t FilterCaps() const noexcept = 0;
    virtual uint32_t SwapWidth() const noexcept = 0;
    virtual uint32_t SwapHeight() const noexcept = 0;
    virtual bool HandleDeviceRemoved() noexcept = 0;
    // Non-blocking best-effort presentation (e.g. for preview windows)
    virtual void SetNonBlocking(bool /*non_blocking*/) noexcept {}
    virtual bool IsNonBlocking() const noexcept { return false; }
    // Clear output to black. Call on disconnect when no stream is active.
    virtual void PresentBlack() noexcept {}
    // Log swapchain configuration for diagnostics
    virtual void LogSwapChainConfig(const char* /*label*/) const noexcept {}
};

} // namespace duwn::video
