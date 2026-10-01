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

static AudioFormatConfig ParseFormat(const WAVEFORMATEX* fmt) noexcept {
    AudioFormatConfig cfg;
    if (!fmt) return cfg;
    cfg.sample_rate = fmt->nSamplesPerSec;
    cfg.channels = fmt->nChannels;
    cfg.bits_per_sample = fmt->wBitsPerSample;
    cfg.valid_bits_per_sample = fmt->wBitsPerSample;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        cfg.valid_bits_per_sample = ext->Samples.wValidBitsPerSample;
        cfg.channel_mask = ext->dwChannelMask;
        if (::IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            cfg.sample_type = AudioSampleType::Float32;
        } else if (::IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            if (cfg.bits_per_sample == 16) cfg.sample_type = AudioSampleType::Int16;
            else if (cfg.bits_per_sample == 32 || cfg.bits_per_sample == 24) cfg.sample_type = AudioSampleType::Int24In32;
        }
    } else if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        cfg.sample_type = AudioSampleType::Float32;
    } else if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
        if (cfg.bits_per_sample == 16) cfg.sample_type = AudioSampleType::Int16;
        else if (cfg.bits_per_sample == 32 || cfg.bits_per_sample == 24) cfg.sample_type = AudioSampleType::Int24In32;
    }
    cfg.needs_resample = (cfg.sample_rate != 48000);
    return cfg;
}

WasapiOutput::WasapiOutput(AudioRingBuffer& ring) noexcept : m_ring(ring) {
    m_wake_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WasapiOutput::~WasapiOutput() {
    Stop();
    {
        std::lock_guard lock(m_active_mutex);
        CleanTarget(m_active);
    }
    if (m_wake_event) {
        ::CloseHandle(m_wake_event);
        m_wake_event = nullptr;
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
    bool ok = PrepareTarget(device_id, target);
    if (!ok && !device_id.empty()) {
        // Fallback to system default if pinned device is missing
        ok = PrepareTarget(L"", target);
        if (ok) target.is_fallback = true;
    }

    if (ok) {
        std::lock_guard lock(m_active_mutex);
        m_active = std::move(target);
        m_is_fallback.store(m_active.is_fallback, std::memory_order_release);
        m_buffer_ms.store(m_active.buffer_ms, std::memory_order_relaxed);
        m_state.store(AudioEndpointState::Idle, std::memory_order_release);
        DUWN_LOG_INFOF("WasapiOutput", "Initialized: {}Hz, {} ch, buffer {:.1f}ms, endpoint={}",
            m_active.format.sample_rate, m_active.format.channels, m_active.buffer_ms,
            ToUtf8(m_active.resolved_name));
    } else {
        m_state.store(AudioEndpointState::WaitingForDevice, std::memory_order_release);
        DUWN_LOG_WARN("WasapiOutput", "Init: no usable audio endpoint; entering WaitingForDevice");
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

    // Request 48kHz stereo float32 first
    WAVEFORMATEXTENSIBLE req{};
    req.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    req.Format.nChannels       = 2;
    req.Format.nSamplesPerSec  = 48000;
    req.Format.wBitsPerSample  = 32;
    req.Format.nBlockAlign     = req.Format.nChannels * req.Format.wBitsPerSample / 8;
    req.Format.nAvgBytesPerSec = req.Format.nSamplesPerSec * req.Format.nBlockAlign;
    req.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    req.Samples.wValidBitsPerSample = 32;
    req.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    req.SubFormat     = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    UINT32 default_period = 0, fundamental_period = 0, min_period = 0, max_period = 0;
    hr = client3->GetSharedModeEnginePeriod(
        reinterpret_cast<WAVEFORMATEX*>(&req),
        &default_period, &fundamental_period, &min_period, &max_period);

    const WAVEFORMATEX* chosen_fmt = reinterpret_cast<const WAVEFORMATEX*>(&req);
    if (SUCCEEDED(hr)) {
        target.format = ParseFormat(chosen_fmt);
    } else {
        chosen_fmt = mix_fmt;
        target.format = ParseFormat(mix_fmt);
        hr = client3->GetSharedModeEnginePeriod(
            mix_fmt,
            &default_period, &fundamental_period, &min_period, &max_period);
        if (FAILED(hr)) {
            ::CoTaskMemFree(mix_fmt);
            return false;
        }
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
        chosen_fmt,
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
    if (target.format.needs_resample) {
        m_resampler.Init(48000, 2, target.format.sample_rate, 2);
    }
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

    target.format = ParseFormat(mix_fmt);

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
    if (target.format.needs_resample) {
        m_resampler.Init(48000, 2, target.format.sample_rate, 2);
    }
    GlobalMetrics().audio_engine_period_ms.store(0.0, std::memory_order_relaxed);
    REFERENCE_TIME stream_latency = 0;
    if (SUCCEEDED(client->GetStreamLatency(&stream_latency)))
        GlobalMetrics().audio_stream_latency_ms.store(stream_latency / 10'000.0, std::memory_order_relaxed);
    return true;
}

bool WasapiOutput::PrepareTarget(const std::wstring& target_id, AudioTargetResources& out_target) noexcept {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr) || !enumerator) return false;

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
    if (FAILED(hr) || !out_target.device) return false;

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
    if (!out_target.ready_event) return false;

    bool ok = TryInitAudioClient3(out_target.device.Get(), out_target);
    if (!ok) ok = FallbackInitAudioClient(out_target.device.Get(), out_target);
    if (!ok) {
        CleanTarget(out_target);
        return false;
    }

    SetSessionIdentity(out_target.client.Get());

    BYTE* data = nullptr;
    if (SUCCEEDED(out_target.render_client->GetBuffer(out_target.buffer_frames, &data))) {
        out_target.render_client->ReleaseBuffer(out_target.buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    return true;
}

bool WasapiOutput::SwitchEndpoint(const std::wstring& device_id) noexcept {
    const uint64_t gen = ++m_request_generation;
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
    bool ok = PrepareTarget(device_id, candidate);
    bool fallback = false;
    if (!ok && !device_id.empty()) {
        ok = PrepareTarget(L"", candidate);
        if (ok) fallback = true;
    }

    if (gen != m_request_generation.load(std::memory_order_acquire)) {
        CleanTarget(candidate);
        return true;
    }

    if (ok) {
        candidate.is_fallback = fallback;
        m_switching.store(true, std::memory_order_release);
        if (m_wake_event) ::SetEvent(m_wake_event);
        {
            std::lock_guard act_lock(m_active_mutex);
            if (m_active.ready_event) ::SetEvent(m_active.ready_event);
            if (m_running.load(std::memory_order_acquire) && candidate.client) {
                candidate.client->Start();
            }
            AudioTargetResources old = std::move(m_active);
            m_active = std::move(candidate);
            m_is_fallback.store(m_active.is_fallback, std::memory_order_release);
            m_buffer_ms.store(m_active.buffer_ms, std::memory_order_relaxed);
            CleanTarget(old);
        }
        m_switching.store(false, std::memory_order_release);
        m_state.store(AudioEndpointState::Playing, std::memory_order_release);
        DUWN_LOG_INFOF("WasapiOutput", "Endpoint switched: {}Hz, {} ch, buffer {:.1f}ms, endpoint={} (fallback={})",
            m_active.format.sample_rate, m_active.format.channels, m_active.buffer_ms,
            ToUtf8(m_active.resolved_name), fallback);
        return true;
    } else {
        std::lock_guard act_lock(m_active_mutex);
        if (m_active.client) {
            m_state.store(AudioEndpointState::Playing, std::memory_order_release);
        } else {
            m_state.store(AudioEndpointState::WaitingForDevice, std::memory_order_release);
        }
        DUWN_LOG_WARN("WasapiOutput", "SwitchEndpoint failed: candidate unavailable; preserving current endpoint");
        return false;
    }
}

void WasapiOutput::OnDeviceEnvironmentChanged() noexcept {
    std::wstring pref_id;
    EndpointSelectionPolicy policy;
    {
        std::lock_guard sel_lock(m_selection_mutex);
        pref_id = m_preferred_device_id;
        policy = m_selection_policy;
    }

    if (policy == EndpointSelectionPolicy::PinnedDevice) {
        if (m_is_fallback.load(std::memory_order_acquire)) {
            ComPtr<IMMDeviceEnumerator> enumerator;
            if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
                ComPtr<IMMDevice> dev;
                if (SUCCEEDED(enumerator->GetDevice(pref_id.c_str(), dev.GetAddressOf())) && dev) {
                    DWORD st = 0;
                    if (SUCCEEDED(dev->GetState(&st)) && st == DEVICE_STATE_ACTIVE) {
                        DUWN_LOG_INFO("WasapiOutput", "Pinned device re-detected; switching back from fallback");
                        SwitchEndpoint(pref_id);
                    }
                }
            }
        }
    } else {
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            ComPtr<IMMDevice> def_dev;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, def_dev.GetAddressOf())) && def_dev) {
                LPWSTR cur_id = nullptr;
                if (SUCCEEDED(def_dev->GetId(&cur_id)) && cur_id) {
                    std::wstring new_def_id = cur_id;
                    ::CoTaskMemFree(cur_id);
                    std::wstring cur_active = ResolvedDeviceId();
                    if (new_def_id != cur_active) {
                        DUWN_LOG_INFO("WasapiOutput", "System default endpoint changed; switching output");
                        SwitchEndpoint(L"");
                    }
                }
            }
        }
    }
}

bool WasapiOutput::PerformRecovery(std::stop_token stop) noexcept {
    if (stop.stop_requested() || !m_running.load(std::memory_order_acquire)) return false;

    DUWN_LOG_INFO("WasapiOutput", "Performing audio endpoint recovery");
    std::wstring pref_id;
    EndpointSelectionPolicy policy;
    {
        std::lock_guard sel_lock(m_selection_mutex);
        pref_id = m_preferred_device_id;
        policy = m_selection_policy;
    }

    AudioTargetResources candidate;
    bool ok = false;
    bool fallback = false;

    if (policy == EndpointSelectionPolicy::PinnedDevice && !pref_id.empty()) {
        ok = PrepareTarget(pref_id, candidate);
        if (!ok) {
            ok = PrepareTarget(L"", candidate);
            if (ok) fallback = true;
        }
    } else {
        ok = PrepareTarget(L"", candidate);
    }

    if (ok) {
        candidate.is_fallback = fallback;
        if (m_running.load(std::memory_order_acquire) && candidate.client) {
            candidate.client->Start();
        }
        {
            std::lock_guard lock(m_active_mutex);
            AudioTargetResources old = std::move(m_active);
            m_active = std::move(candidate);
            m_is_fallback.store(m_active.is_fallback, std::memory_order_release);
            m_buffer_ms.store(m_active.buffer_ms, std::memory_order_relaxed);
            CleanTarget(old);
        }
        m_state.store(AudioEndpointState::Playing, std::memory_order_release);
        DUWN_LOG_INFOF("WasapiOutput", "Recovery successful: playing on {} (fallback={})",
            ToUtf8(m_active.resolved_name), fallback);
        return true;
    } else {
        m_state.store(AudioEndpointState::WaitingForDevice, std::memory_order_release);
        DUWN_LOG_WARN("WasapiOutput", "No audio endpoint available; entering WaitingForDevice");
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

void WasapiOutput::WriteFramesToTarget(BYTE* pcm_dest, uint32_t frames_to_write, uint32_t pulled_frames) noexcept {
    if (!pcm_dest || frames_to_write == 0) return;

    const auto& fmt = m_active.format;
    const uint32_t ch = fmt.channels;

    if (pulled_frames == 0) {
        size_t bytes = static_cast<size_t>(frames_to_write) * ch * (fmt.bits_per_sample / 8);
        std::memset(pcm_dest, 0, bytes);
        return;
    }

    if (!fmt.needs_resample && ch == 2 && fmt.sample_type == AudioSampleType::Float32) {
        float* dst = reinterpret_cast<float*>(pcm_dest);
        for (uint32_t f = 0; f < frames_to_write; ++f) {
            float g = NextGain();
            dst[f * 2 + 0] *= g;
            dst[f * 2 + 1] *= g;
        }
        return;
    }

    const float* src_stereo = fmt.needs_resample ? m_resample_out.data() : m_resample_in.data();
    size_t available_src_frames = (fmt.needs_resample ? m_resample_out.size() : m_resample_in.size()) / 2;

    for (uint32_t f = 0; f < frames_to_write; ++f) {
        float g = NextGain();
        float left = 0.0f, right = 0.0f;
        if (f < available_src_frames) {
            left  = src_stereo[f * 2 + 0] * g;
            right = src_stereo[f * 2 + 1] * g;
        }

        if (fmt.sample_type == AudioSampleType::Float32) {
            float* out = reinterpret_cast<float*>(pcm_dest) + f * ch;
            if (ch == 1) {
                out[0] = (left + right) * 0.5f;
            } else {
                out[0] = left;
                out[1] = right;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0.0f;
            }
        } else if (fmt.sample_type == AudioSampleType::Int16) {
            int16_t* out = reinterpret_cast<int16_t*>(pcm_dest) + f * ch;
            int16_t s_l = static_cast<int16_t>(std::clamp(left * 32767.0f, -32768.0f, 32767.0f));
            int16_t s_r = static_cast<int16_t>(std::clamp(right * 32767.0f, -32768.0f, 32767.0f));
            if (ch == 1) {
                out[0] = static_cast<int16_t>((s_l + s_r) / 2);
            } else {
                out[0] = s_l;
                out[1] = s_r;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0;
            }
        } else if (fmt.sample_type == AudioSampleType::Int24In32) {
            int32_t* out = reinterpret_cast<int32_t*>(pcm_dest) + f * ch;
            int32_t s_l = static_cast<int32_t>(std::clamp(left * 8388607.0f, -8388608.0f, 8388607.0f)) << 8;
            int32_t s_r = static_cast<int32_t>(std::clamp(right * 8388607.0f, -8388608.0f, 8388607.0f)) << 8;
            if (ch == 1) {
                out[0] = static_cast<int16_t>((s_l + s_r) / 2);
            } else {
                out[0] = s_l;
                out[1] = s_r;
                for (uint32_t c = 2; c < ch; ++c) out[c] = 0;
            }
        }
    }
}

void WasapiOutput::RenderLoop(std::stop_token stop) noexcept {
    DWORD mmcss_task = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_task);
    bool stream_primed = false;

    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
        if (m_switching.load(std::memory_order_acquire)) {
            ::WaitForSingleObject(m_wake_event, 5);
            continue;
        }

        AudioEndpointState current_st = m_state.load(std::memory_order_acquire);
        if (current_st == AudioEndpointState::WaitingForDevice) {
            m_ring.DiscardOldest(4800); // 100ms at 48kHz
            DWORD wr = ::WaitForSingleObject(m_wake_event, 250);
            if (wr == WAIT_OBJECT_0 || wr == WAIT_TIMEOUT) {
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
        if (wr == WAIT_OBJECT_0 + 1) continue;
        if (wr != WAIT_OBJECT_0) continue;

        if (m_switching.load(std::memory_order_acquire)) continue;

        std::unique_lock lock(m_active_mutex);
        if (!m_active.client || !m_active.render_client) {
            lock.unlock();
            m_state.store(AudioEndpointState::Recovering, std::memory_order_release);
            continue;
        }

        UINT32 padding = 0;
        HRESULT hr = m_active.client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED) {
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
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED) {
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
        uint32_t pulled = 0;
        if (stream_primed) {
            if (!m_active.format.needs_resample && m_active.format.channels == 2 && m_active.format.sample_type == AudioSampleType::Float32) {
                pulled = m_ring.Pull(reinterpret_cast<float*>(data), frames_to_write);
            } else {
                double ratio = 48000.0 / sample_rate;
                uint32_t ring_frames = static_cast<uint32_t>(frames_to_write * ratio) + 8;
                m_resample_in.resize(ring_frames * 2);
                uint32_t actual_pull = m_ring.Pull(m_resample_in.data(), ring_frames);
                if (m_active.format.needs_resample) {
                    m_resampler.Convert(m_resample_in.data(), actual_pull, m_resample_out);
                }
                pulled = actual_pull > 0 ? frames_to_write : 0;
            }
        } else {
            pulled = 0;
        }

        WriteFramesToTarget(data, frames_to_write, pulled);

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

        hr = m_active.render_client->ReleaseBuffer(frames_to_write, 0);
        lock.unlock();

        if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED) {
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
