#pragma once
// ReplayKitLegacyBackend.h — Legacy deprecated ReplayKit capture backend.
// Uses RPScreenRecorder (startCaptureWithHandler:).
// Scope: CaptureScope::OwnApplication (in-app capture only; records host application).
// Ineligible for FullDisplay Direct mirroring.
// Marked as legacy = true, deprecated_api = true, verification_status = LEGACY_UNVERIFIED.
// Purpose: legacy compatibility path only where target SDK/runtime still supports required behavior.
// Does NOT claim modern status. Replaces "ModernReplayKit".

#include "DuwnCaptureBackend.h"
#include <atomic>
#include <mutex>

namespace duwn::direct::ios {

class ReplayKitLegacyBackend : public DuwnCaptureBackend {
public:
    ReplayKitLegacyBackend();
    ~ReplayKitLegacyBackend() override;

    // DuwnCaptureBackend implementation
    bool Initialize() override;
    bool StartCapture() override;
    void StopCapture() override;
    bool IsCapturing() const noexcept override { return m_capturing.load(std::memory_order_relaxed); }
    CaptureCapabilities GetCapabilities() const override;

    DuwnCaptureFrameSlot& GetVideoFrameSlot() noexcept override { return m_slot; }
    void SetVideoFrameCallback(std::function<void(const CapturedVideoFrame&)> callback) override;
    void SetAudioFrameCallback(std::function<void(const CapturedAudioFrame&)> callback) override;
    CaptureSlotMetrics GetSlotMetrics() const noexcept override { return m_slot.GetMetrics(); }

    // Static availability probe
    // Verifies whether deprecated RPScreenRecorder is present at runtime.
    static bool IsAvailable() noexcept;

    // Checks whether this legacy backend has been verified on the target Apple OS/runtime
    static bool IsVerifiedOnRuntime() noexcept;

private:
    void HandleSampleBuffer(void* sample_buffer_ref, int sample_type);

    std::atomic<bool> m_capturing{false};
    std::atomic<bool> m_initialized{false};
    uint64_t m_frame_sequence{0};

    DuwnCaptureFrameSlot m_slot;
    mutable std::mutex m_mutex;
    CaptureCapabilities m_capabilities;
    std::function<void(const CapturedVideoFrame&)> m_video_callback;
    std::function<void(const CapturedAudioFrame&)> m_audio_callback;
};

} // namespace duwn::direct::ios
