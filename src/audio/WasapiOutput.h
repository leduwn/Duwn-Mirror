#pragma once
// WasapiOutput — WASAPI shared-mode audio output via IAudioClient3.
// Pulls from AudioRingBuffer on the WASAPI render thread.
// Uses MMCSS for real-time priority.
// Shared mode: compatible with OBS / TikTok Live Studio loopback.
// Stereo preserved: no downmix.
//
// Audio session is owned by duwn-mirror.exe. OBS/TikTok can capture via
// "Application Audio Capture" or "Audio Input Capture" pointing to this process.
// Session display name: "Duwn Mirror". No virtual audio driver required.
//
// AUDCLNT_STREAMFLAGS_NOPERSIST is intentionally NOT set: Windows stores
// per-session volume/mute state. This is the correct default for a media app.

#include "AudioRingBuffer.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <functional>
#include <cstdint>

namespace duwn::audio {

using Microsoft::WRL::ComPtr;

// Called when an underrun is detected (for metrics / diagnostics).
using UnderrunCallback = std::function<void()>;

constexpr bool HasAudioStartupPrebuffer(uint32_t available_frames,
                                        uint32_t target_frames,
                                        uint32_t endpoint_request_frames) noexcept {
    const uint32_t required = target_frames > endpoint_request_frames
        ? target_frames : endpoint_request_frames;
    return available_frames >= required;
}

constexpr uint32_t AudioPrebufferTargetFrames(bool recovering_from_underrun,
                                               uint32_t startup_frames,
                                               uint32_t adaptive_frames) noexcept {
    if (!recovering_from_underrun) return startup_frames;
    return adaptive_frames > startup_frames ? adaptive_frames : startup_frames;
}

class WasapiOutput {
public:
    explicit WasapiOutput(AudioRingBuffer& ring_buffer) noexcept;
    ~WasapiOutput();

    WasapiOutput(const WasapiOutput&) = delete;
    WasapiOutput& operator=(const WasapiOutput&) = delete;

    // device_id: empty string = default output device.
    bool Init(const std::wstring& device_id = {}) noexcept;

    void Start() noexcept;
    void Stop() noexcept;

    // Live endpoint switch — does NOT restart AirPlay/video pipeline.
    // device_id: empty string = system default. Safe to call while streaming.
    bool SwitchEndpoint(const std::wstring& device_id) noexcept;

    // Current active device endpoint ID (empty = system default).
    std::wstring CurrentDeviceId() const noexcept;
    std::wstring ResolvedDeviceId() const noexcept;
    std::wstring ResolvedDeviceName() const noexcept;

    // Returns actual buffer duration in milliseconds.
    double BufferMs() const noexcept { return m_buffer_ms; }

    // Number of underruns since Start().
    uint64_t UnderrunCount() const noexcept { return m_underruns.load(); }

    void SetMuted(bool muted) noexcept { m_muted.store(muted, std::memory_order_release); }
    bool IsMuted() const noexcept { return m_muted.load(std::memory_order_acquire); }

    void SetUnderrunCallback(UnderrunCallback cb) noexcept { m_underrun_cb = std::move(cb); }

private:
    void RenderLoop(std::stop_token stop) noexcept;
    bool TryIAudioClient3() noexcept; // low-latency path
    bool FallbackIAudioClient()  noexcept;
    bool InitAudioClient(const std::wstring& device_id) noexcept; // shared init path
    void SetSessionIdentity() noexcept; // stable GUID + display name
    void UpdateResolvedEndpoint() noexcept;

    AudioRingBuffer& m_ring;
    ComPtr<IMMDevice>         m_device;
    ComPtr<IAudioClient>      m_client;   // IAudioClient3 if available
    ComPtr<IAudioRenderClient> m_render_client;

    uint32_t          m_sample_rate{48000};
    uint32_t          m_channels{2};
    uint32_t          m_buffer_frames{0};
    double            m_buffer_ms{0.0};
    HANDLE            m_ready_event{nullptr};

    std::wstring      m_current_device_id;   // empty = system default
    std::wstring      m_resolved_device_id;
    std::wstring      m_resolved_device_name;
    mutable std::mutex m_device_id_mutex;

    std::atomic<uint64_t> m_underruns{0};
    std::atomic_bool      m_running{false};
    std::atomic_bool      m_muted{false};
    std::atomic_bool      m_switching{false}; // live endpoint switch in progress
    std::atomic_bool      m_first_audio_submitted{false};
    std::mutex            m_switch_mutex;
    std::jthread          m_thread;
    UnderrunCallback      m_underrun_cb;
};

} // namespace duwn::audio
