#pragma once
// WasapiOutput — WASAPI shared-mode audio output with robust endpoint lifecycle,
// format conversion, volume ramping, fallback/restore, and race-free switching.

#include "AudioRingBuffer.h"
#include "AudioConverter.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace duwn::audio {

using Microsoft::WRL::ComPtr;

// Called when an underrun is detected (for metrics / diagnostics).
using UnderrunCallback = std::function<void()>;

enum class AudioEndpointState {
    Idle,
    Opening,
    Playing,
    Switching,
    Recovering,
    WaitingForDevice,
    Stopped,
    Error
};

enum class EndpointSelectionPolicy {
    SystemDefault,
    PinnedDevice
};

enum class AudioSampleType {
    Float32,
    Int16,
    Int24In32,
    Unknown
};

struct AudioFormatConfig {
    uint32_t sample_rate{48000};
    uint32_t channels{2};
    uint32_t bits_per_sample{32};
    uint32_t valid_bits_per_sample{32};
    AudioSampleType sample_type{AudioSampleType::Float32};
    uint32_t channel_mask{SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT};
    bool needs_resample{false};
};

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

    // device_id: empty string = default output device (SystemDefault policy).
    // non-empty string = pinned device (PinnedDevice policy).
    bool Init(const std::wstring& device_id = {}) noexcept;

    void Start() noexcept;
    void Stop() noexcept;

    // Live endpoint switch — does NOT restart AirPlay/video pipeline.
    // Safe to call rapidly; newest request cancels earlier ones.
    bool SwitchEndpoint(const std::wstring& device_id) noexcept;

    // Called when system audio environment changes (hotplug or default changed).
    void OnDeviceEnvironmentChanged() noexcept;

    // App-level volume control (0.0f .. 1.0f) with soft ramping.
    void SetVolume(float volume) noexcept;
    float Volume() const noexcept { return m_volume.load(std::memory_order_relaxed); }

    void SetMuted(bool muted) noexcept;
    bool IsMuted() const noexcept { return m_muted.load(std::memory_order_acquire); }

    // Endpoint status
    std::wstring CurrentDeviceId() const noexcept;   // Preferred endpoint ID (empty = default)
    std::wstring PreferredDeviceId() const noexcept { return CurrentDeviceId(); }
    std::wstring ResolvedDeviceId() const noexcept;   // Active physical endpoint ID
    std::wstring ResolvedDeviceName() const noexcept; // Active physical device name
    bool IsFallbackActive() const noexcept { return m_is_fallback.load(std::memory_order_acquire); }

    AudioEndpointState State() const noexcept { return m_state.load(std::memory_order_acquire); }
    EndpointSelectionPolicy SelectionPolicy() const noexcept;
    std::wstring StateString() const noexcept;

    double BufferMs() const noexcept { return m_buffer_ms.load(std::memory_order_relaxed); }
    uint64_t UnderrunCount() const noexcept { return m_underruns.load(std::memory_order_relaxed); }
    void SetUnderrunCallback(UnderrunCallback cb) noexcept { m_underrun_cb = std::move(cb); }

private:
    struct AudioTargetResources {
        ComPtr<IMMDevice>          device;
        ComPtr<IAudioClient>       client;
        ComPtr<IAudioRenderClient> render_client;
        HANDLE                     ready_event{nullptr};
        AudioFormatConfig          format{};
        uint32_t                   buffer_frames{0};
        double                     buffer_ms{0.0};
        std::wstring               resolved_id;
        std::wstring               resolved_name;
        bool                       is_fallback{false};
    };

    void RenderLoop(std::stop_token stop) noexcept;
    bool PrepareTarget(const std::wstring& target_id, AudioTargetResources& out_target) noexcept;
    bool TryInitAudioClient3(IMMDevice* device, AudioTargetResources& target) noexcept;
    bool FallbackInitAudioClient(IMMDevice* device, AudioTargetResources& target) noexcept;
    void SetSessionIdentity(IAudioClient* client) noexcept;

    // Recovery executed inside the worker loop
    bool PerformRecovery(std::stop_token stop) noexcept;
    void CleanTarget(AudioTargetResources& target) noexcept;

    // Conversion and writing
    void WriteFramesToTarget(BYTE* pcm_dest, uint32_t frames_to_write, uint32_t pulled_frames) noexcept;
    float NextGain() noexcept;

    AudioRingBuffer& m_ring;

    // Active resources under render loop
    AudioTargetResources m_active;
    mutable std::mutex   m_active_mutex;

    // Preferred selection
    std::wstring            m_preferred_device_id;
    EndpointSelectionPolicy m_selection_policy{EndpointSelectionPolicy::SystemDefault};
    mutable std::mutex      m_selection_mutex;

    std::atomic<AudioEndpointState> m_state{AudioEndpointState::Idle};
    std::atomic<bool>               m_is_fallback{false};
    std::atomic<uint64_t>           m_request_generation{0};

    std::atomic<float>    m_volume{1.0f};
    std::atomic<bool>     m_muted{false};
    float                 m_current_gain{1.0f}; // smoothly ramped gain

    std::atomic<double>   m_buffer_ms{0.0};
    std::atomic<uint64_t> m_underruns{0};
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_switching{false};
    std::atomic<bool>     m_first_audio_submitted{false};

    HANDLE                m_wake_event{nullptr}; // unblocks worker for state changes/switches
    std::mutex            m_switch_mutex;
    std::jthread          m_thread;
    UnderrunCallback      m_underrun_cb;

    // Resampler & format buffers
    AudioConverter        m_resampler;
    std::vector<float>    m_resample_in;
    std::vector<float>    m_resample_out;
};

} // namespace duwn::audio
