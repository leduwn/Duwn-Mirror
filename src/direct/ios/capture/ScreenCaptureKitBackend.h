#pragma once
// ScreenCaptureKitBackend.h — Modern preferred system-wide screen capture backend.
// Expected API family: SCContentSharingPicker, SCStream, SCStreamOutput, SCStreamConfiguration.
// Scope: CaptureScope::FullDisplay.
// Status: SOURCE_ONLY_NOT_BUILT (Apple build pending Xcode compilation).

#include "DuwnCaptureBackend.h"
#include <atomic>
#include <mutex>

namespace duwn::direct::ios {

class ScreenCaptureKitBackend : public DuwnCaptureBackend {
public:
    ScreenCaptureKitBackend();
    ~ScreenCaptureKitBackend() override;

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
    // Verifies whether ScreenCaptureKit classes (SCStream, SCContentSharingPicker) are present at runtime.
    // Does NOT fabricate hardcoded availability numbers; defers to Xcode SDK runtime checks.
    static bool IsAvailable() noexcept;

    // Checks whether modern ScreenCaptureKit streaming has been verified on the target Apple OS/runtime
    static bool IsVerifiedOnRuntime() noexcept;

private:
    void HandleStreamSampleBuffer(void* sample_buffer_ref, int stream_output_type);

    std::atomic<bool> m_capturing{false};
    std::atomic<bool> m_initialized{false};
    uint64_t m_frame_sequence{0};

    DuwnCaptureFrameSlot m_slot;
    mutable std::mutex m_mutex;
    CaptureCapabilities m_capabilities;
    std::function<void(const CapturedVideoFrame&)> m_video_callback;
    std::function<void(const CapturedAudioFrame&)> m_audio_callback;

#if defined(__APPLE__)
    void* m_stream{nullptr};           // SCStream*
    void* m_stream_delegate{nullptr};  // id<SCStreamDelegate, SCStreamOutput>
#endif
};

} // namespace duwn::direct::ios
