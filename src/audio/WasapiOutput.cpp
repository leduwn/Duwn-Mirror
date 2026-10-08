#include "WasapiOutput.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include "common/telemetry/ConnectionTimeline.h"
#include <avrt.h>
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <format>
#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

namespace duwn::audio {

// Target buffer duration: 20ms for low latency, sufficient to avoid underruns.
constexpr REFERENCE_TIME kTargetDuration100ns = 200'000; // 20ms in 100-ns units

// Stable session GUID: identifies DUWN Mirror audio in Windows audio session manager.
// DO NOT CHANGE — Windows stores per-session volume/mute state keyed to this GUID.
static const GUID kDuwnAudioSessionGuid =
    { 0x7f3d1a2e, 0x4b5c, 0x4d8f, {0xa3, 0x1b, 0x2c, 0x9e, 0x5f, 0x7a, 0x0b, 0x4d} };

static std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), bytes, nullptr, nullptr);
    return result;
}

static double GetDevWasapiPeriodOverrideMs() noexcept {
    wchar_t value[32]{};
    DWORD size = ::GetEnvironmentVariableW(L"DUWN_DEV_WASAPI_PERIOD_MS", value,
                                            static_cast<DWORD>(std::size(value)));
    if (size > 0 && size < std::size(value)) {
        try {
            double ms = std::stod(value);
            if (ms > 0.0) return ms;
        } catch (...) {}
    }
    return 0.0;
}

bool ValidateAudioFormat(const WAVEFORMATEX* fmt, AudioFormatConfig& out_cfg) noexcept {
    if (!fmt) return false;
    if (fmt->nChannels == 0 || fmt->nChannels > 8) return false;
    if (fmt->nSamplesPerSec < 8000 || fmt->nSamplesPerSec > 192000) return false;
    if (fmt->wBitsPerSample == 0 || (fmt->wBitsPerSample % 8) != 0) return false;

    const uint32_t bytes_per_sample = fmt->wBitsPerSample / 8;
    if (fmt->nBlockAlign != fmt->nChannels * bytes_per_sample) return false;
    if (fmt->nAvgBytesPerSec != fmt->nSamplesPerSec * fmt->nBlockAlign) return false;

    AudioFormatConfig cfg{};
    cfg.sample_rate = fmt->nSamplesPerSec;
    cfg.channels = fmt->nChannels;
    cfg.bits_per_sample = fmt->wBitsPerSample;
    cfg.valid_bits_per_sample = fmt->wBitsPerSample;
    cfg.sample_type = AudioSampleType::Unknown;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (fmt->cbSize < 22) return false; // sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        cfg.valid_bits_per_sample = ext->Samples.wValidBitsPerSample;
        cfg.channel_mask = ext->dwChannelMask;

        if (::IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            if (cfg.bits_per_sample == 32 && cfg.valid_bits_per_sample == 32) {
                cfg.sample_type = AudioSampleType::Float32;
            } else {
                return false;
            }
        } else if (::IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            if (cfg.bits_per_sample == 16 && cfg.valid_bits_per_sample == 16) {
                cfg.sample_type = AudioSampleType::Int16;
            } else if (cfg.bits_per_sample == 24 && cfg.valid_bits_per_sample == 24) {
                cfg.sample_type = AudioSampleType::Int24Packed;
            } else if (cfg.bits_per_sample == 32 && cfg.valid_bits_per_sample == 24) {
                cfg.sample_type = AudioSampleType::Int24In32;
            } else if (cfg.bits_per_sample == 32 && cfg.valid_bits_per_sample == 32) {
                cfg.sample_type = AudioSampleType::Int32;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        if (cfg.bits_per_sample == 32) {
            cfg.sample_type = AudioSampleType::Float32;
            cfg.channel_mask = (cfg.channels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
        } else {
            return false;
        }
    } else if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
        if (cfg.bits_per_sample == 16) {
            cfg.sample_type = AudioSampleType::Int16;
        } else if (cfg.bits_per_sample == 24) {
            cfg.sample_type = AudioSampleType::Int24Packed;
        } else if (cfg.bits_per_sample == 32) {
            cfg.sample_type = AudioSampleType::Int32;
        } else {
            return false;
        }
        cfg.channel_mask = (cfg.channels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    } else {
        return false;
    }

    cfg.needs_resample = (cfg.sample_rate != 48000);
    out_cfg = cfg;
    return true;
}

size_t WriteAudioFrames(BYTE* pcm_dest, size_t dest_buffer_bytes,
                        uint32_t frames_to_write,
                        const float* src_stereo, uint32_t src_frames,
                        const AudioFormatConfig& fmt,
                        const std::function<float()>& next_gain) noexcept {
    if (!pcm_dest || frames_to_write == 0) return 0;
    const uint32_t ch = fmt.channels;
    const uint32_t bytes_per_sample = fmt.bits_per_sample / 8;
    if (bytes_per_sample == 0) return 0;

    const size_t total_bytes = static_cast<size_t>(frames_to_write) * ch * bytes_per_sample;
    if (dest_buffer_bytes < total_bytes) return 0;

    if (src_frames == 0 || !src_stereo) {
        std::memset(pcm_dest, 0, total_bytes);
        return total_bytes;
    }

    for (uint32_t f = 0; f < frames_to_write; ++f) {
        float g = next_gain ? next_gain() : 1.0f;
        float left = 0.0f, right = 0.0f;
        if (f < src_frames) {
            left  = src_stereo[f * 2 + 0] * g;
            right = src_stereo[f * 2 + 1] * g;
        }

        BYTE* frame_dest = pcm_dest + static_cast<size_t>(f) * ch * bytes_per_sample;

        if (fmt.sample_type == AudioSampleType::Float32) {
            float* out = reinterpret_cast<float*>(frame_dest);
            if (ch == 1) {
                out[0] = (left + right) * 0.5f;
            } else {
                out[0] = left;
                out[1] = right;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0.0f;
            }
        } else if (fmt.sample_type == AudioSampleType::Int16) {
            int16_t* out = reinterpret_cast<int16_t*>(frame_dest);
            int16_t s_l = static_cast<int16_t>(std::clamp(left * 32767.0f, -32768.0f, 32767.0f));
            int16_t s_r = static_cast<int16_t>(std::clamp(right * 32767.0f, -32768.0f, 32767.0f));
            if (ch == 1) {
                out[0] = static_cast<int16_t>((static_cast<int32_t>(s_l) + static_cast<int32_t>(s_r)) / 2);
            } else {
                out[0] = s_l;
                out[1] = s_r;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0;
            }
        } else if (fmt.sample_type == AudioSampleType::Int24Packed) {
            int32_t s_l = static_cast<int32_t>(std::clamp(left * 8388607.0f, -8388608.0f, 8388607.0f));
            int32_t s_r = static_cast<int32_t>(std::clamp(right * 8388607.0f, -8388608.0f, 8388607.0f));
            if (ch == 1) {
                int32_t mono = (s_l + s_r) / 2;
                uint32_t u = static_cast<uint32_t>(mono);
                frame_dest[0] = static_cast<uint8_t>(u & 0xFF);
                frame_dest[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
                frame_dest[2] = static_cast<uint8_t>((u >> 16) & 0xFF);
            } else {
                uint32_t ul = static_cast<uint32_t>(s_l);
                frame_dest[0] = static_cast<uint8_t>(ul & 0xFF);
                frame_dest[1] = static_cast<uint8_t>((ul >> 8) & 0xFF);
                frame_dest[2] = static_cast<uint8_t>((ul >> 16) & 0xFF);

                uint32_t ur = static_cast<uint32_t>(s_r);
                frame_dest[3] = static_cast<uint8_t>(ur & 0xFF);
                frame_dest[4] = static_cast<uint8_t>((ur >> 8) & 0xFF);
                frame_dest[5] = static_cast<uint8_t>((ur >> 16) & 0xFF);

                for (uint32_t c = 2; c < ch; ++c) {
                    frame_dest[c * 3 + 0] = 0;
                    frame_dest[c * 3 + 1] = 0;
                    frame_dest[c * 3 + 2] = 0;
                }
            }
        } else if (fmt.sample_type == AudioSampleType::Int24In32) {
            int32_t* out = reinterpret_cast<int32_t*>(frame_dest);
            int32_t s_l = static_cast<int32_t>(std::clamp(left * 8388607.0f, -8388608.0f, 8388607.0f)) << 8;
            int32_t s_r = static_cast<int32_t>(std::clamp(right * 8388607.0f, -8388608.0f, 8388607.0f)) << 8;
            if (ch == 1) {
                out[0] = static_cast<int32_t>((static_cast<int64_t>(s_l) + static_cast<int64_t>(s_r)) / 2);
            } else {
                out[0] = s_l;
                out[1] = s_r;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0;
            }
        } else if (fmt.sample_type == AudioSampleType::Int32) {
            int32_t* out = reinterpret_cast<int32_t*>(frame_dest);
            int32_t s_l = static_cast<int32_t>(std::clamp(left * 2147483647.0f, -2147483648.0f, 2147483647.0f));
            int32_t s_r = static_cast<int32_t>(std::clamp(right * 2147483647.0f, -2147483648.0f, 2147483647.0f));
            if (ch == 1) {
                out[0] = static_cast<int32_t>((static_cast<int64_t>(s_l) + static_cast<int64_t>(s_r)) / 2);
            } else {
                out[0] = s_l;
                out[1] = s_r;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0;
            }
        }
    }
    return total_bytes;
}

WasapiOutput::WasapiOutput(AudioRingBuffer& ring) noexcept : m_ring(ring) {
    m_wake_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_switch_ack_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WasapiOutput::~WasapiOutput() {
    Stop();
    if (m_wake_event) {
        ::CloseHandle(m_wake_event);
        m_wake_event = nullptr;
    }
    if (m_switch_ack_event) {
        ::CloseHandle(m_switch_ack_event);
        m_switch_ack_event = nullptr;
    }
}

bool WasapiOutput::Init(const std::wstring& device_id) noexcept {
    {
        std::lock_guard lock(m_selection_mutex);
        m_preferred_device_id = device_id;
        m_selection_policy = device_id.empty()
            ? EndpointSelectionPolicy::SystemDefault
            : EndpointSelectionPolicy::PinnedDevice;
    }

    m_state.store(AudioEndpointState::Opening, std::memory_order_release);

    AudioTargetResources target;
    const TargetOpenResult result = PrepareTarget(device_id, target);

    if (result == TargetOpenResult::Success) {
        std::lock_guard lock(m_active_mutex);
        m_active = std::move(target);
        ConfigureForActiveTarget();
        m_state.store(AudioEndpointState::Idle, std::memory_order_release);
        DUWN_LOG_INFOF("WasapiOutput", "Initialized: {}Hz, {} ch, buffer {:.1f}ms, endpoint={}",
            m_active.format.sample_rate, m_active.format.channels, m_active.buffer_ms,
            ToUtf8(m_active.resolved_name));
    } else {
        const auto state = result == TargetOpenResult::Unavailable
            ? AudioEndpointState::WaitingForDevice
            : AudioEndpointState::InitializationFailed;
        m_state.store(state, std::memory_order_release);
        DUWN_LOG_WARN("WasapiOutput", "Init: selected audio endpoint could not be opened");
    }

    return true;
}

void WasapiOutput::Start() noexcept {
    m_running.store(true, std::memory_order_release);
    m_first_audio_submitted.store(false, std::memory_order_release);
    GlobalMetrics().wasapi_running.store(true, std::memory_order_release);

    {
        std::lock_guard lock(m_active_mutex);
        if (m_active.client) {
            HRESULT hr = m_active.client->Start();
            if (SUCCEEDED(hr)) {
                m_state.store(AudioEndpointState::Playing, std::memory_order_release);
            } else {
                DUWN_LOG_ERRORF("WasapiOutput", "client->Start failed {:#010x}", static_cast<unsigned>(hr));
                m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            }
        } else {
            m_state.store(AudioEndpointState::WaitingForDevice, std::memory_order_release);
        }
    }

    m_thread = std::jthread([this](std::stop_token st) { RenderLoop(std::move(st)); });
}

void WasapiOutput::Stop() noexcept {
    m_running.store(false, std::memory_order_release);
    m_state.store(AudioEndpointState::Stopped, std::memory_order_release);
    GlobalMetrics().wasapi_running.store(false, std::memory_order_release);

    if (m_wake_event) ::SetEvent(m_wake_event);
    {
        std::lock_guard lock(m_active_mutex);
        if (m_active.ready_event) ::SetEvent(m_active.ready_event);
    }

    m_thread.request_stop();
    if (m_thread.joinable()) m_thread.join();

    {
        std::lock_guard lock(m_active_mutex);
        if (m_active.client) m_active.client->Stop();
    }
}

std::wstring WasapiOutput::CurrentDeviceId() const noexcept {
    std::lock_guard lock(m_selection_mutex);
    return m_preferred_device_id;
}

std::wstring WasapiOutput::ResolvedDeviceId() const noexcept {
    std::lock_guard lock(m_active_mutex);
    return m_active.resolved_id;
}

std::wstring WasapiOutput::ResolvedDeviceName() const noexcept {
    std::lock_guard lock(m_active_mutex);
    return m_active.resolved_name;
}

uint32_t WasapiOutput::OutputSampleRate() const noexcept {
    std::lock_guard lock(m_active_mutex);
    return m_active.client ? m_active.format.sample_rate : 0;
}

uint32_t WasapiOutput::OutputChannels() const noexcept {
    std::lock_guard lock(m_active_mutex);
    return m_active.client ? m_active.format.channels : 0;
}

EndpointSelectionPolicy WasapiOutput::SelectionPolicy() const noexcept {
    std::lock_guard lock(m_selection_mutex);
    return m_selection_policy;
}

std::wstring WasapiOutput::StateString() const noexcept {
    switch (m_state.load(std::memory_order_acquire)) {
    case AudioEndpointState::Idle:             return L"Idle";
    case AudioEndpointState::Opening:          return L"Opening";
    case AudioEndpointState::Playing:          return m_is_fallback.load() ? L"Playing (Fallback)" : L"Playing";
    case AudioEndpointState::Switching:        return L"Switching";
    case AudioEndpointState::Recovering:       return L"Recovering";
    case AudioEndpointState::WaitingForDevice: return L"Waiting for device";
    case AudioEndpointState::InitializationFailed: return L"Initialization failed";
    case AudioEndpointState::Stopped:          return L"Stopped";
    case AudioEndpointState::Error:            return L"Error";
    default:                                   return L"—";
    }
}

void WasapiOutput::SetVolume(float volume) noexcept {
    float clamped = std::clamp(volume, 0.0f, 1.0f);
    m_volume.store(clamped, std::memory_order_release);
}

void WasapiOutput::SetMuted(bool muted) noexcept {
    m_muted.store(muted, std::memory_order_release);
}

void WasapiOutput::SetSessionIdentity(IAudioClient* client) noexcept {
    if (!client) return;
    ComPtr<IAudioSessionControl> session_ctrl;
    HRESULT hr = client->GetService(__uuidof(IAudioSessionControl),
        reinterpret_cast<void**>(session_ctrl.GetAddressOf()));
    if (FAILED(hr)) return;

    ComPtr<IAudioSessionControl2> session_ctrl2;
    hr = session_ctrl.As(&session_ctrl2);
    if (FAILED(hr)) return;

    session_ctrl2->SetGroupingParam(&kDuwnAudioSessionGuid, nullptr);
    session_ctrl2->SetDisplayName(L"Duwn Mirror", nullptr);
}

void WasapiOutput::CleanTarget(AudioTargetResources& target) noexcept {
    if (target.client) {
        target.client->Stop();
        target.client->Reset();
    }
    target.render_client.Reset();
    target.client.Reset();
    target.device.Reset();
    if (target.ready_event) {
        ::CloseHandle(target.ready_event);
        target.ready_event = nullptr;
    }
    target.buffer_frames = 0;
    target.buffer_ms = 0.0;
    target.resolved_id.clear();
    target.resolved_name.clear();
    target.is_fallback = false;
}

void WasapiOutput::ConfigureForActiveTarget() noexcept {
    m_is_fallback.store(false, std::memory_order_release);
    m_buffer_ms.store(m_active.buffer_ms, std::memory_order_relaxed);
    GlobalMetrics().audio_output_rate.store(
        m_active.client ? m_active.format.sample_rate : 0, std::memory_order_relaxed);
    if (m_active.client && m_active.format.needs_resample) {
        m_resampler.Init(48000, 2, m_active.format.sample_rate, 2);
    } else {
        m_resampler.Reset();
    }
    m_resampled_fifo.clear();
    m_recovery_attempts.store(0, std::memory_order_release);
}

void WasapiOutput::DeactivateEndpoint(
    AudioEndpointState state, bool reset_recovery_attempts) noexcept {
    {
        std::lock_guard pending_lock(m_pending_mutex);
        if (m_has_pending_target) CleanTarget(m_pending_target);
        m_has_pending_target = false;
        m_pending_generation = 0;
    }
    {
        std::lock_guard active_lock(m_active_mutex);
        CleanTarget(m_active);
        ConfigureForActiveTarget();
    }
    m_ring.DiscardOldest(0);
    if (reset_recovery_attempts) {
        m_recovery_attempts.store(0, std::memory_order_release);
    }
    m_state.store(state, std::memory_order_release);
    if (m_wake_event) ::SetEvent(m_wake_event);
}

bool WasapiOutput::TryInitAudioClient3(IMMDevice* device, AudioTargetResources& target) noexcept {
    if (!device) return false;
    ComPtr<IAudioClient3> client3;
    HRESULT hr = device->Activate(
        __uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client3.GetAddressOf()));
    if (FAILED(hr) || !client3) return false;

    WAVEFORMATEX* mix_fmt = nullptr;
    hr = client3->GetMixFormat(&mix_fmt);
    if (FAILED(hr) || !mix_fmt) return false;

    if (!ValidateAudioFormat(mix_fmt, target.format)) {
        ::CoTaskMemFree(mix_fmt);
        return false;
    }

    UINT32 default_period = 0, fundamental_period = 0, min_period = 0, max_period = 0;
    hr = client3->GetSharedModeEnginePeriod(
        mix_fmt,
        &default_period, &fundamental_period, &min_period, &max_period);
    if (FAILED(hr)) {
        ::CoTaskMemFree(mix_fmt);
        return false;
    }

    UINT32 period = std::max(min_period, fundamental_period);
    const double dev_period_ms = GetDevWasapiPeriodOverrideMs();
    if (dev_period_ms > 0.0) {
        UINT32 req_frames = static_cast<UINT32>(
            std::lround(dev_period_ms * target.format.sample_rate / 1000.0));
        if (req_frames >= min_period && req_frames <= max_period) {
            period = req_frames;
        }
    }

    hr = client3->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        period,
        mix_fmt,
        nullptr);
    ::CoTaskMemFree(mix_fmt);
    if (FAILED(hr)) return false;

    hr = client3->SetEventHandle(target.ready_event);
    if (FAILED(hr)) return false;

    hr = client3->GetBufferSize(&target.buffer_frames);
    if (FAILED(hr)) return false;

    hr = client3->GetService(IID_PPV_ARGS(&target.render_client));
    if (FAILED(hr)) return false;

    target.client = client3;
    target.buffer_ms = static_cast<double>(target.buffer_frames) / target.format.sample_rate * 1000.0;
    GlobalMetrics().audio_engine_period_ms.store(1000.0 * period / target.format.sample_rate, std::memory_order_relaxed);
    REFERENCE_TIME stream_latency = 0;
    if (SUCCEEDED(client3->GetStreamLatency(&stream_latency)))
        GlobalMetrics().audio_stream_latency_ms.store(stream_latency / 10'000.0, std::memory_order_relaxed);
    return true;
}

bool WasapiOutput::FallbackInitAudioClient(IMMDevice* device, AudioTargetResources& target) noexcept {
    if (!device) return false;
    ComPtr<IAudioClient> client;
    HRESULT hr = device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr) || !client) return false;

    WAVEFORMATEX* mix_fmt = nullptr;
    hr = client->GetMixFormat(&mix_fmt);
    if (FAILED(hr) || !mix_fmt) return false;

    if (!ValidateAudioFormat(mix_fmt, target.format)) {
        ::CoTaskMemFree(mix_fmt);
        return false;
    }

    hr = client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        kTargetDuration100ns, 0, mix_fmt, nullptr);
    ::CoTaskMemFree(mix_fmt);
    if (FAILED(hr)) return false;

    hr = client->SetEventHandle(target.ready_event);
    if (FAILED(hr)) return false;

    hr = client->GetBufferSize(&target.buffer_frames);
    if (FAILED(hr)) return false;

    hr = client->GetService(IID_PPV_ARGS(&target.render_client));
    if (FAILED(hr)) return false;

    target.client = client;
    target.buffer_ms = static_cast<double>(target.buffer_frames) / target.format.sample_rate * 1000.0;
    GlobalMetrics().audio_engine_period_ms.store(0.0, std::memory_order_relaxed);
    REFERENCE_TIME stream_latency = 0;
    if (SUCCEEDED(client->GetStreamLatency(&stream_latency)))
        GlobalMetrics().audio_stream_latency_ms.store(stream_latency / 10'000.0, std::memory_order_relaxed);
    return true;
}

WasapiOutput::TargetOpenResult WasapiOutput::PrepareTarget(
    const std::wstring& target_id, AudioTargetResources& out_target) noexcept {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr) || !enumerator) return TargetOpenResult::InitializationFailed;

    if (target_id.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, out_target.device.GetAddressOf());
    } else {
        hr = enumerator->GetDevice(target_id.c_str(), out_target.device.GetAddressOf());
        if (SUCCEEDED(hr) && out_target.device) {
            DWORD dev_state = 0;
            if (FAILED(out_target.device->GetState(&dev_state)) || dev_state != DEVICE_STATE_ACTIVE) {
                out_target.device.Reset();
                hr = E_FAIL;
            }
        }
    }
    if (FAILED(hr) || !out_target.device) return TargetOpenResult::Unavailable;

    LPWSTR raw_id = nullptr;
    if (SUCCEEDED(out_target.device->GetId(&raw_id)) && raw_id) {
        out_target.resolved_id = raw_id;
        ::CoTaskMemFree(raw_id);
    }
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(out_target.device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
        PROPVARIANT pv;
        ::PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal) {
            out_target.resolved_name = pv.pwszVal;
        }
        ::PropVariantClear(&pv);
    }
    if (out_target.resolved_name.empty()) out_target.resolved_name = out_target.resolved_id;

    out_target.ready_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!out_target.ready_event) return TargetOpenResult::InitializationFailed;

    bool ok = TryInitAudioClient3(out_target.device.Get(), out_target);
    if (!ok) ok = FallbackInitAudioClient(out_target.device.Get(), out_target);
    if (!ok) {
        CleanTarget(out_target);
        return TargetOpenResult::InitializationFailed;
    }

    SetSessionIdentity(out_target.client.Get());

    BYTE* data = nullptr;
    if (SUCCEEDED(out_target.render_client->GetBuffer(out_target.buffer_frames, &data))) {
        out_target.render_client->ReleaseBuffer(out_target.buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    return TargetOpenResult::Success;
}

bool WasapiOutput::SwitchEndpoint(const std::wstring& device_id) noexcept {
    const uint64_t gen = m_endpoint_work.BeginRequest();
    std::lock_guard lock(m_switch_mutex);

    {
        std::lock_guard sel_lock(m_selection_mutex);
        m_preferred_device_id = device_id;
        m_selection_policy = device_id.empty()
            ? EndpointSelectionPolicy::SystemDefault
            : EndpointSelectionPolicy::PinnedDevice;
    }

    m_state.store(AudioEndpointState::Switching, std::memory_order_release);

    AudioTargetResources candidate;
    const TargetOpenResult result = PrepareTarget(device_id, candidate);

    if (result != TargetOpenResult::Success) {
        CleanTarget(candidate);
        const auto state = result == TargetOpenResult::Unavailable
            ? AudioEndpointState::WaitingForDevice
            : AudioEndpointState::InitializationFailed;
        const bool committed = m_endpoint_work.TryCommit(gen, [this, state] {
            DeactivateEndpoint(state);
        });
        if (committed) {
            DUWN_LOG_WARN("WasapiOutput", "Selected endpoint unavailable; audio output paused without fallback");
        }
        return false;
    }

    candidate.is_fallback = false;

    if (!m_running.load(std::memory_order_acquire)) {
        const bool committed = m_endpoint_work.TryCommit(gen, [this, &candidate] {
            std::lock_guard act_lock(m_active_mutex);
            CleanTarget(m_active);
            m_active = std::move(candidate);
            ConfigureForActiveTarget();
            m_ring.DiscardOldest(0);
            m_state.store(AudioEndpointState::Idle, std::memory_order_release);
        });
        if (!committed) CleanTarget(candidate);
        return true;
    }

    ::ResetEvent(m_switch_ack_event);
    const bool queued = m_endpoint_work.TryCommit(gen, [this, gen, &candidate] {
        std::lock_guard pend_lock(m_pending_mutex);
        if (m_has_pending_target) CleanTarget(m_pending_target);
        m_pending_target = std::move(candidate);
        m_has_pending_target = true;
        m_pending_generation = gen;
    });
    if (!queued) {
        CleanTarget(candidate);
        return true;
    }

    if (m_wake_event) ::SetEvent(m_wake_event);

    DWORD wr = ::WaitForSingleObject(m_switch_ack_event, 500);
    if (wr != WAIT_OBJECT_0) {
        DUWN_LOG_WARNF("WasapiOutput", "SwitchEndpoint: worker switch ack timed out ({})", wr);
    }

    if (m_endpoint_work.Capture() != gen) return true;

    std::lock_guard active_lock(m_active_mutex);
    DUWN_LOG_INFOF("WasapiOutput", "Endpoint switched: {}Hz, {} ch, buffer {:.1f}ms, endpoint={}",
        m_active.format.sample_rate, m_active.format.channels, m_active.buffer_ms,
        ToUtf8(m_active.resolved_name));
    return true;
}

void WasapiOutput::OnDeviceEnvironmentChanged() noexcept {
    std::wstring pref_id;
    EndpointSelectionPolicy policy;
    {
        std::lock_guard sel_lock(m_selection_mutex);
        pref_id = m_preferred_device_id;
        policy = m_selection_policy;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator))) || !enumerator) {
        return;
    }

    std::wstring default_id;
    ComPtr<IMMDevice> default_device;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, default_device.GetAddressOf())) && default_device) {
        LPWSTR raw_id = nullptr;
        if (SUCCEEDED(default_device->GetId(&raw_id)) && raw_id) {
            default_id = raw_id;
            ::CoTaskMemFree(raw_id);
        }
    }

    bool selected_active = false;
    if (policy == EndpointSelectionPolicy::PinnedDevice && !pref_id.empty()) {
        ComPtr<IMMDevice> selected;
        if (SUCCEEDED(enumerator->GetDevice(pref_id.c_str(), selected.GetAddressOf())) && selected) {
            DWORD device_state = 0;
            selected_active = SUCCEEDED(selected->GetState(&device_state)) &&
                              device_state == DEVICE_STATE_ACTIVE;
        }
    }

    const auto decision = DecideEndpointChange(
        policy, pref_id, ResolvedDeviceId(), default_id, selected_active);
    switch (decision.action) {
    case EndpointChangeAction::OpenDefault:
        DUWN_LOG_INFO("WasapiOutput", "Multimedia default endpoint changed; switching output");
        SwitchEndpoint(L"");
        break;
    case EndpointChangeAction::OpenSpecific:
        DUWN_LOG_INFO("WasapiOutput", "Selected endpoint became active; reopening exact endpoint ID");
        SwitchEndpoint(std::wstring(decision.target_id));
        break;
    case EndpointChangeAction::WaitForDevice:
        if (!ResolvedDeviceId().empty() ||
            m_state.load(std::memory_order_acquire) != AudioEndpointState::WaitingForDevice) {
            const uint64_t gen = m_endpoint_work.BeginRequest();
            m_endpoint_work.TryCommit(gen, [this] {
                DeactivateEndpoint(AudioEndpointState::WaitingForDevice);
            });
        }
        break;
    case EndpointChangeAction::None:
        break;
    }
}

bool WasapiOutput::PerformRecovery(std::stop_token stop) noexcept {
    if (stop.stop_requested() || !m_running.load(std::memory_order_acquire)) return false;

    const uint64_t recovery_generation = m_endpoint_work.Capture();
    DUWN_LOG_INFO("WasapiOutput", "Performing audio endpoint recovery");
    std::wstring pref_id;
    EndpointSelectionPolicy policy;
    {
        std::lock_guard sel_lock(m_selection_mutex);
        pref_id = m_preferred_device_id;
        policy = m_selection_policy;
    }

    AudioTargetResources candidate;
    const TargetOpenResult result = PrepareTarget(
        policy == EndpointSelectionPolicy::PinnedDevice ? pref_id : L"", candidate);

    if (result == TargetOpenResult::Success) {
        const bool committed = m_endpoint_work.TryCommit(
            recovery_generation, [this, &candidate] {
            if (m_running.load(std::memory_order_acquire) && candidate.client) {
                candidate.client->Start();
            }
            std::lock_guard lock(m_active_mutex);
            CleanTarget(m_active);
            m_active = std::move(candidate);
            ConfigureForActiveTarget();
            m_state.store(AudioEndpointState::Playing, std::memory_order_release);
        });
        if (!committed) {
            CleanTarget(candidate);
            return false;
        }
        DUWN_LOG_INFO("WasapiOutput", "Recovery successful: playing on selected endpoint");
        return true;
    } else {
        const auto state = result == TargetOpenResult::Unavailable
            ? AudioEndpointState::WaitingForDevice
            : AudioEndpointState::InitializationFailed;
        const bool committed = m_endpoint_work.TryCommit(
            recovery_generation, [this, state] {
            DeactivateEndpoint(
                state, state != AudioEndpointState::InitializationFailed);
        });
        if (committed) {
            DUWN_LOG_WARN("WasapiOutput", "Selected audio endpoint is still unavailable");
        }
        return false;
    }
}

float WasapiOutput::NextGain() noexcept {
    float target = m_muted.load(std::memory_order_relaxed) ? 0.0f : m_volume.load(std::memory_order_relaxed);
    constexpr float kRampStep = 1.0f / 512.0f; // ~10.6ms at 48kHz
    if (std::abs(m_current_gain - target) > 0.0001f) {
        if (m_current_gain < target) m_current_gain = std::min(target, m_current_gain + kRampStep);
        else m_current_gain = std::max(target, m_current_gain - kRampStep);
    } else {
        m_current_gain = target;
    }
    return m_current_gain;
}

void WasapiOutput::RenderLoop(std::stop_token stop) noexcept {
    DWORD mmcss_task = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_task);
    bool stream_primed = false;

    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
        // Check for pending endpoint switch committed by worker thread
        AudioTargetResources new_target;
        bool do_switch = false;
        uint64_t switch_generation = 0;
        {
            std::lock_guard pend_lock(m_pending_mutex);
            if (m_has_pending_target) {
                new_target = std::move(m_pending_target);
                m_has_pending_target = false;
                switch_generation = m_pending_generation;
                m_pending_generation = 0;
                do_switch = true;
            }
        }
        if (do_switch) {
            const bool committed = m_endpoint_work.TryCommit(
                switch_generation, [this, &new_target] {
                std::lock_guard act_lock(m_active_mutex);
                CleanTarget(m_active);
                m_active = std::move(new_target);
                ConfigureForActiveTarget();
                if (m_running.load(std::memory_order_acquire) && m_active.client) {
                    m_active.client->Start();
                }
                m_ring.DiscardOldest(0);
                m_state.store(AudioEndpointState::Playing, std::memory_order_release);
            });
            if (!committed) {
                CleanTarget(new_target);
            }
            if (m_switch_ack_event) ::SetEvent(m_switch_ack_event);
            continue;
        }

        AudioEndpointState current_st = m_state.load(std::memory_order_acquire);
        if (current_st == AudioEndpointState::WaitingForDevice) {
            m_ring.DiscardOldest(4800); // 100ms at 48kHz
            ::WaitForSingleObject(m_wake_event, 1000);
            continue;
        }

        if (current_st == AudioEndpointState::InitializationFailed) {
            m_ring.DiscardOldest(4800);
            const uint32_t attempt = m_recovery_attempts.load(std::memory_order_acquire);
            const uint32_t delay_ms = AudioRecoveryRetryDelayMs(attempt);
            if (delay_ms == 0) {
                ::WaitForSingleObject(m_wake_event, 1000);
                continue;
            }
            const DWORD wr = ::WaitForSingleObject(m_wake_event, delay_ms);
            if (wr == WAIT_TIMEOUT) {
                m_recovery_attempts.fetch_add(1, std::memory_order_acq_rel);
                PerformRecovery(stop);
            }
            continue;
        }

        if (current_st == AudioEndpointState::Recovering) {
            PerformRecovery(stop);
            continue;
        }

        HANDLE wait_handles[2] = { nullptr, m_wake_event };
        {
            std::lock_guard lock(m_active_mutex);
            wait_handles[0] = m_active.ready_event;
        }

        if (!wait_handles[0]) {
            m_state.store(AudioEndpointState::WaitingForDevice, std::memory_order_release);
            continue;
        }

        DWORD wr = ::WaitForMultipleObjects(2, wait_handles, FALSE, 50);
        if (wr == WAIT_TIMEOUT) continue;
        if (wr == WAIT_OBJECT_0 + 1) continue; // Woken by wake_event
        if (wr != WAIT_OBJECT_0) continue;

        std::unique_lock lock(m_active_mutex);
        if (!m_active.client || !m_active.render_client) {
            lock.unlock();
            m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            continue;
        }

        UINT32 padding = 0;
        HRESULT hr = m_active.client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED ||
            hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
            hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
            lock.unlock();
            m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            continue;
        }
        if (FAILED(hr)) {
            lock.unlock();
            continue;
        }

        const uint32_t sample_rate = m_active.format.sample_rate;
        GlobalMetrics().audio_wasapi_padding_ms.store(
            1000.0 * padding / sample_rate, std::memory_order_relaxed);

        UINT32 frames_to_write = m_active.buffer_frames - padding;
        if (frames_to_write == 0) continue;

        BYTE* data = nullptr;
        hr = m_active.render_client->GetBuffer(frames_to_write, &data);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED ||
            hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
            hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
            lock.unlock();
            m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            continue;
        }
        if (FAILED(hr) || !data) {
            lock.unlock();
            continue;
        }

        const auto a5 = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
        auto& metrics = GlobalMetrics();
        const int64_t last_rtp = metrics.last_audio_rtp_arrival_ns.load(std::memory_order_relaxed);
        const int64_t now_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
        const bool rtp_recently_active =
            last_rtp > 0 && (now_ns - last_rtp) < 150'000'000LL;

        if (!rtp_recently_active) {
            stream_primed = false;
            m_first_audio_submitted.store(false, std::memory_order_relaxed);
        }

        bool prebuffering = false;
        if (!stream_primed && rtp_recently_active) {
            const uint32_t min_stable_frames = sample_rate / 50; // 20ms
            const double target_ms = metrics.audio_target_buffer_ms.load(std::memory_order_relaxed);
            const uint32_t adaptive_frames = target_ms > 0.0
                ? static_cast<uint32_t>(std::lround(target_ms * static_cast<double>(sample_rate) / 1000.0))
                : min_stable_frames;
            const bool recovering_from_underrun =
                m_first_audio_submitted.load(std::memory_order_relaxed);
            const uint32_t startup_target = AudioPrebufferTargetFrames(
                recovering_from_underrun, min_stable_frames, adaptive_frames);
            if (HasAudioStartupPrebuffer(m_ring.Available(), startup_target, frames_to_write)) {
                stream_primed = true;
            } else {
                prebuffering = true;
            }
        }

        const uint32_t available_before_pull = m_ring.Available();
        const size_t bytes_per_sample = m_active.format.bits_per_sample / 8;
        const size_t bytes_per_frame = static_cast<size_t>(m_active.format.channels) * bytes_per_sample;
        const size_t dest_buffer_bytes = static_cast<size_t>(frames_to_write) * bytes_per_frame;

        uint32_t pulled = 0;
        if (stream_primed) {
            if (!m_active.format.needs_resample) {
                // Passthrough sample rate (48kHz): pull EXACTLY frames_to_write
                m_staging_in.resize(frames_to_write * 2);
                uint32_t actual_pull = m_ring.Pull(m_staging_in.data(), frames_to_write);
                pulled = actual_pull;
                WriteAudioFrames(data, dest_buffer_bytes, frames_to_write,
                                 m_staging_in.data(), pulled, m_active.format,
                                 [this] { return NextGain(); });
            } else {
                // Resampling path: retain unused samples in persistent FIFO
                size_t avail_fifo = m_resampled_fifo.size() / 2;
                if (avail_fifo < frames_to_write) {
                    uint32_t deficit = frames_to_write - static_cast<uint32_t>(avail_fifo);
                    double ratio = 48000.0 / static_cast<double>(sample_rate);
                    uint32_t needed_in = static_cast<uint32_t>(std::ceil(deficit * ratio)) + 4;
                    uint32_t ring_avail = m_ring.Available();
                    uint32_t to_pull = std::min(needed_in, ring_avail);
                    if (to_pull > 0) {
                        m_staging_in.resize(to_pull * 2);
                        uint32_t actual_pull = m_ring.Pull(m_staging_in.data(), to_pull);
                        m_converted_chunk.clear();
                        m_resampler.Convert(m_staging_in.data(), actual_pull, m_converted_chunk);
                        m_resampled_fifo.insert(m_resampled_fifo.end(),
                                                m_converted_chunk.begin(), m_converted_chunk.end());
                    }
                }

                avail_fifo = m_resampled_fifo.size() / 2;
                uint32_t frames_from_fifo = std::min(frames_to_write, static_cast<uint32_t>(avail_fifo));
                pulled = frames_from_fifo;

                WriteAudioFrames(data, dest_buffer_bytes, frames_to_write,
                                 m_resampled_fifo.data(), frames_from_fifo, m_active.format,
                                 [this] { return NextGain(); });

                if (frames_from_fifo > 0) {
                    if (frames_from_fifo == avail_fifo) {
                        m_resampled_fifo.clear();
                    } else {
                        m_resampled_fifo.erase(m_resampled_fifo.begin(),
                                               m_resampled_fifo.begin() + frames_from_fifo * 2);
                    }
                }

                // Cap FIFO to 4800 frames (~100ms)
                constexpr size_t kMaxFifoFrames = 4800;
                if (m_resampled_fifo.size() / 2 > kMaxFifoFrames) {
                    size_t excess = (m_resampled_fifo.size() / 2) - kMaxFifoFrames;
                    m_resampled_fifo.erase(m_resampled_fifo.begin(),
                                           m_resampled_fifo.begin() + excess * 2);
                }
            }
        } else {
            pulled = 0;
            WriteAudioFrames(data, dest_buffer_bytes, frames_to_write,
                             nullptr, 0, m_active.format,
                             [this] { return NextGain(); });
        }

        uint32_t silence_frames = frames_to_write - pulled;

        if (pulled > 0) {
            if (!m_first_audio_submitted.exchange(true, std::memory_order_relaxed)) {
                telemetry::ConnectionTimeline::Get().Record(
                    telemetry::ConnectionMilestone::C15_FirstWasapiWrite,
                    std::format("frames={}, rate={}Hz", pulled, sample_rate));
                DUWN_LOG_INFOF("Diagnostics",
                    "FIRST EVENT: Audio buffer submitted to WASAPI (frames={}, rate={}Hz)",
                    pulled, sample_rate);
            }
        }

        if (silence_frames > 0) {
            GlobalMetrics().audio_silence_fill_frames.fetch_add(silence_frames, std::memory_order_relaxed);

            if (!prebuffering && rtp_recently_active &&
                m_first_audio_submitted.load(std::memory_order_relaxed)) {
                GlobalMetrics().audio_underrun_frames.fetch_add(silence_frames, std::memory_order_relaxed);
                m_underruns.fetch_add(1, std::memory_order_relaxed);
                GlobalMetrics().audio_real_underruns.fetch_add(1, std::memory_order_relaxed);
                GlobalMetrics().audio_underruns.fetch_add(1, std::memory_order_relaxed);
                const double rtp_age_ms = last_rtp > 0
                    ? static_cast<double>(now_ns - last_rtp) / 1'000'000.0 : 0.0;
                DUWN_LOG_WARNF("WasapiOutput",
                    "Real underrun: requested={} pulled={} available_before={} padding={} target={:.2f}ms rtp_age={:.2f}ms arrival_gap={:.2f}ms peak_gap={:.2f}ms",
                    frames_to_write, pulled, available_before_pull, padding,
                    metrics.audio_target_buffer_ms.load(std::memory_order_relaxed),
                    rtp_age_ms,
                    metrics.audio_arrival_gap_ms.load(std::memory_order_relaxed),
                    metrics.audio_arrival_gap_max_ms.load(std::memory_order_relaxed));
                if (m_underrun_cb) m_underrun_cb();
                stream_primed = false;
            }
        }

        DWORD rel_flags = (pulled == 0) ? AUDCLNT_BUFFERFLAGS_SILENT : 0;
        hr = m_active.render_client->ReleaseBuffer(frames_to_write, rel_flags);
        lock.unlock();

        if (hr == AUDCLNT_E_DEVICE_INVALIDATED ||
            hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
            hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
            m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            continue;
        }

        const auto a6 = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
        GlobalMetrics().audio_a5_a6_ms.store((a6 - a5) / 1'000'000.0, std::memory_order_relaxed);

        GlobalMetrics().audio_samples_rendered.fetch_add(frames_to_write, std::memory_order_relaxed);

        // Update buffer occupancy metric
        double avail_ms = static_cast<double>(m_ring.Available()) / 48000.0 * 1000.0;
        GlobalMetrics().audio_buffer_ms.store(avail_ms, std::memory_order_relaxed);
    }

    if (mmcss) ::AvRevertMmThreadCharacteristics(mmcss);
}

} // namespace duwn::audio
