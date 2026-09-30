#pragma once
// InAppSnapshotBackend.h — CADisplayLink / drawViewHierarchy in-app snapshot backend.
// Scope: CaptureScope::OwnApplication ONLY.
// Production full-display eligible: NO.
// Must NOT participate as a fallback for full-screen Direct mirroring.
// Intended only for development, tests, and own-app capture demos.

#include "DuwnCaptureBackend.h"
#include <atomic>
#include <mutex>

namespace duwn::direct::ios {

class InAppSnapshotBackend : public DuwnCaptureBackend {
public:
    InAppSnapshotBackend();
    ~InAppSnapshotBackend() override;

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
    static bool IsAvailable() noexcept;

private:
    void OnDisplayLinkTick();

    std::atomic<bool> m_capturing{false};
    std::atomic<bool> m_initialized{false};
    uint64_t m_frame_sequence{0};

    DuwnCaptureFrameSlot m_slot;
    mutable std::mutex m_mutex;
    CaptureCapabilities m_capabilities;
    std::function<void(const CapturedVideoFrame&)> m_video_callback;
    std::function<void(const CapturedAudioFrame&)> m_audio_callback;

#if defined(__APPLE__)
    void* m_display_link{nullptr};
    void* m_pixel_buffer_pool{nullptr};
#endif
};

} // namespace duwn::direct::ios
