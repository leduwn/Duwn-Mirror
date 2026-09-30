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

WasapiOutput::WasapiOutput(AudioRingBuffer& ring) noexcept : m_ring(ring) {}

WasapiOutput::~WasapiOutput() {
    Stop();
    if (m_ready_event) ::CloseHandle(m_ready_event);
}

bool WasapiOutput::Init(const std::wstring& device_id) noexcept {
    {
        std::lock_guard lock(m_device_id_mutex);
        m_current_device_id = device_id;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("WasapiOutput",
            "CoCreateInstance MMDeviceEnumerator failed {:#010x}",
            static_cast<unsigned>(hr));
        return false;
    }

    if (device_id.empty()) {
        // eMultimedia: Windows selects the default communications/media device.
        // eConsole is incorrect for audio playback — it targets gaming/system sounds.
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, m_device.GetAddressOf());
    } else {
        hr = enumerator->GetDevice(device_id.c_str(), m_device.GetAddressOf());
    }
    if (FAILED(hr)) {
        DUWN_LOG_ERROR("WasapiOutput", "GetDefaultAudioEndpoint failed");
        return false;
    }
    UpdateResolvedEndpoint();

    m_ready_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Try IAudioClient3 (low-latency, Windows 10+)
    if (!TryIAudioClient3()) {
        DUWN_LOG_WARN("WasapiOutput", "IAudioClient3 not available; using IAudioClient");
        if (!FallbackIAudioClient()) return false;
    }

    SetSessionIdentity();

    DUWN_LOG_INFOF("WasapiOutput",
        "Initialized: {}Hz, {} ch, buffer {:.1f} ms",
        m_sample_rate, m_channels, m_buffer_ms);
    return true;
}

std::wstring WasapiOutput::CurrentDeviceId() const noexcept {
    std::lock_guard lock(m_device_id_mutex);
    return m_current_device_id;
}

std::wstring WasapiOutput::ResolvedDeviceId() const noexcept {
    std::lock_guard lock(m_device_id_mutex);
    return m_resolved_device_id;
}

std::wstring WasapiOutput::ResolvedDeviceName() const noexcept {
    std::lock_guard lock(m_device_id_mutex);
    return m_resolved_device_name;
}

void WasapiOutput::UpdateResolvedEndpoint() noexcept {
    if (!m_device) return;
    std::wstring id, name;
    LPWSTR raw_id = nullptr;
    if (SUCCEEDED(m_device->GetId(&raw_id)) && raw_id) {
        id = raw_id;
        ::CoTaskMemFree(raw_id);
    }
    ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(m_device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
        PROPVARIANT value;
        ::PropVariantInit(&value);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
            value.vt == VT_LPWSTR && value.pwszVal) name = value.pwszVal;
        ::PropVariantClear(&value);
    }
    DUWN_LOG_INFOF("WasapiOutput", "Resolved endpoint: id={}, name={}", ToUtf8(id), ToUtf8(name));
    {
        std::lock_guard lock(m_device_id_mutex);
        m_resolved_device_id = std::move(id);
        m_resolved_device_name = std::move(name);
    }
}

void WasapiOutput::SetSessionIdentity() noexcept {
    if (!m_client) return;
    ComPtr<IAudioSessionControl> session_ctrl;
    HRESULT hr = m_client->GetService(__uuidof(IAudioSessionControl),
        reinterpret_cast<void**>(session_ctrl.GetAddressOf()));
    if (FAILED(hr)) return;

    ComPtr<IAudioSessionControl2> session_ctrl2;
    hr = session_ctrl.As(&session_ctrl2);
    if (FAILED(hr)) return;

    // Stable grouping GUID: keeps all DUWN Mirror streams under one session entry
    // in Volume Mixer regardless of endpoint switches.
    session_ctrl2->SetGroupingParam(&kDuwnAudioSessionGuid, nullptr);
    // Display name shown in Volume Mixer and audio device pickers (OBS, TikTok, etc.)
    session_ctrl2->SetDisplayName(L"Duwn Mirror", nullptr);
}

bool WasapiOutput::SwitchEndpoint(const std::wstring& device_id) noexcept {
    if (!device_id.empty()) {
        ComPtr<IMMDeviceEnumerator> check_enumerator;
        ComPtr<IMMDevice> check_device;
        if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     IID_PPV_ARGS(&check_enumerator))) ||
            FAILED(check_enumerator->GetDevice(device_id.c_str(), check_device.GetAddressOf()))) {
            DUWN_LOG_WARN("WasapiOutput", "Requested output endpoint is unavailable; current endpoint preserved");
            return false;
        }
        DWORD state = 0;
        if (FAILED(check_device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) return false;
    }
    if (!m_running.load(std::memory_order_acquire)) {
        // Not running — just reinit from scratch
        if (m_client) m_client->Stop();
        m_client.Reset();
        m_render_client.Reset();
        m_device.Reset();
        return Init(device_id);
    }

    std::lock_guard lock(m_switch_mutex);

    // Signal render loop to submit silence and pause
    m_switching.store(true, std::memory_order_release);

    // Small yield to let the render loop notice the flag and exit its current
    // iteration. The loop checks m_switching after WaitForSingleObject returns.
    ::SetEvent(m_ready_event);
    ::Sleep(35); // > one 20ms buffer cycle

    // Tear down current client (Stop+Reset, NOT Shutdown — preserves stream clock)
    if (m_client) {
        m_client->Stop();
        m_client->Reset();
    }
    m_client.Reset();
    m_render_client.Reset();
    m_device.Reset();

    // Activate new endpoint
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        DUWN_LOG_ERROR("WasapiOutput", "SwitchEndpoint: CoCreateInstance failed");
        m_switching.store(false, std::memory_order_release);
        return false;
    }

    if (device_id.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, m_device.GetAddressOf());
    } else {
        hr = enumerator->GetDevice(device_id.c_str(), m_device.GetAddressOf());
    }
    if (FAILED(hr)) {
        DUWN_LOG_ERROR("WasapiOutput", "SwitchEndpoint: no usable endpoint");
        m_switching.store(false, std::memory_order_release);
        return false;
    }
    UpdateResolvedEndpoint();

    bool ok = TryIAudioClient3();
    if (!ok) ok = FallbackIAudioClient();
    if (!ok) {
        DUWN_LOG_ERROR("WasapiOutput", "SwitchEndpoint: failed to init new client");
        m_switching.store(false, std::memory_order_release);
        return false;
    }

    SetSessionIdentity();

    // Pre-fill one buffer of silence so the hardware clock starts immediately
    {
        BYTE* data = nullptr;
        if (SUCCEEDED(m_render_client->GetBuffer(m_buffer_frames, &data))) {
            m_render_client->ReleaseBuffer(m_buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    m_client->Start();

    {
        std::lock_guard id_lock(m_device_id_mutex);
        m_current_device_id = device_id;
    }

    DUWN_LOG_INFOF("WasapiOutput",
        "Endpoint switched: {}Hz, {} ch, buffer {:.1f} ms",
        m_sample_rate, m_channels, m_buffer_ms);

    // Release render loop
    m_switching.store(false, std::memory_order_release);
    ::SetEvent(m_ready_event);
    return true;
}

bool WasapiOutput::TryIAudioClient3() noexcept {
    ComPtr<IAudioClient3> client3;
    HRESULT hr = m_device->Activate(
        __uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client3.GetAddressOf()));
    if (FAILED(hr)) return false;

    // Get mix format
    WAVEFORMATEX* mix_fmt = nullptr;
    hr = client3->GetMixFormat(&mix_fmt);
    if (FAILED(hr)) return false;

    // Request 48kHz stereo float32 (or whatever the device prefers)
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

    UINT32 default_period, fundamental_period, min_period, max_period;
    hr = client3->GetSharedModeEnginePeriod(
        reinterpret_cast<WAVEFORMATEX*>(&req),
        &default_period, &fundamental_period, &min_period, &max_period);
    if (FAILED(hr)) {
        // Format not supported — fall back to mix format
        req = {};
        memcpy(&req, mix_fmt, mix_fmt->cbSize + sizeof(WAVEFORMATEX));
        hr = client3->GetSharedModeEnginePeriod(
            reinterpret_cast<WAVEFORMATEX*>(&req),
            &default_period, &fundamental_period, &min_period, &max_period);
        if (FAILED(hr)) {
            CoTaskMemFree(mix_fmt);
            return false;
        }
    }
    CoTaskMemFree(mix_fmt);

    // Use minimum period for lowest latency
    UINT32 period = std::max(min_period, fundamental_period);
    const double dev_period_ms = GetDevWasapiPeriodOverrideMs();
    if (dev_period_ms > 0.0) {
        UINT32 req_frames = static_cast<UINT32>(
            std::lround(dev_period_ms * req.Format.nSamplesPerSec / 1000.0));
        if (req_frames >= min_period && req_frames <= max_period) {
            DUWN_LOG_INFOF("WasapiOutput",
                "[AudioPeriod] Dev override period: {:.2f}ms ({} frames)",
                dev_period_ms, req_frames);
            period = req_frames;
        } else {
            DUWN_LOG_WARNF("WasapiOutput",
                "[AudioPeriod] Requested period {:.2f}ms ({} frames) outside supported range [{}, {}]; falling back to {}",
                dev_period_ms, req_frames, min_period, max_period, period);
        }
    }

    hr = client3->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        period,
        reinterpret_cast<WAVEFORMATEX*>(&req),
        nullptr);
    if (FAILED(hr)) return false;

    hr = client3->SetEventHandle(m_ready_event);
    if (FAILED(hr)) return false;

    hr = client3->GetBufferSize(&m_buffer_frames);
    if (FAILED(hr)) return false;

    hr = client3->GetService(IID_PPV_ARGS(&m_render_client));
    if (FAILED(hr)) return false;

    m_client      = client3;
    m_sample_rate = req.Format.nSamplesPerSec;
    m_channels    = req.Format.nChannels;
    m_buffer_ms   = static_cast<double>(m_buffer_frames) / m_sample_rate * 1000.0;
    GlobalMetrics().audio_engine_period_ms.store(1000.0 * period / m_sample_rate, std::memory_order_relaxed);
    REFERENCE_TIME stream_latency = 0;
    if (SUCCEEDED(client3->GetStreamLatency(&stream_latency)))
        GlobalMetrics().audio_stream_latency_ms.store(stream_latency / 10'000.0, std::memory_order_relaxed);
    DUWN_LOG_INFOF("WasapiOutput",
        "[AudioPeriod] client3=true rate={} channels={} default={} fundamental={} min={} max={} chosen={} buffer={} frames",
        m_sample_rate, m_channels, default_period, fundamental_period, min_period, max_period, period, m_buffer_frames);
    return true;
}

bool WasapiOutput::FallbackIAudioClient() noexcept {
    ComPtr<IAudioClient> client;
    HRESULT hr = m_device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) return false;

    WAVEFORMATEX* mix_fmt = nullptr;
    hr = client->GetMixFormat(&mix_fmt);
    if (FAILED(hr)) return false;

    hr = client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        kTargetDuration100ns, 0, mix_fmt, nullptr);
    CoTaskMemFree(mix_fmt);
    if (FAILED(hr)) return false;

    hr = client->SetEventHandle(m_ready_event);
    if (FAILED(hr)) return false;

    hr = client->GetBufferSize(&m_buffer_frames);
    if (FAILED(hr)) return false;

    hr = client->GetService(IID_PPV_ARGS(&m_render_client));
    if (FAILED(hr)) return false;

    m_client    = client;
    m_buffer_ms = static_cast<double>(m_buffer_frames) / m_sample_rate * 1000.0;
    GlobalMetrics().audio_engine_period_ms.store(0.0, std::memory_order_relaxed);
    REFERENCE_TIME stream_latency = 0;
    if (SUCCEEDED(client->GetStreamLatency(&stream_latency)))
        GlobalMetrics().audio_stream_latency_ms.store(stream_latency / 10'000.0, std::memory_order_relaxed);
    DUWN_LOG_INFOF("WasapiOutput", "[AudioPeriod] client3=false buffer={} frames (legacy 20ms request)", m_buffer_frames);
    return true;
}

void WasapiOutput::Start() noexcept {
    if (!m_client || !m_render_client) return;
    m_first_audio_submitted.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    GlobalMetrics().wasapi_running.store(true, std::memory_order_release);
    m_client->Start();
    m_thread = std::jthread([this](std::stop_token st) { RenderLoop(std::move(st)); });
}

void WasapiOutput::Stop() noexcept {
    m_running.store(false, std::memory_order_release);
    GlobalMetrics().wasapi_running.store(false, std::memory_order_release);
    if (m_ready_event) ::SetEvent(m_ready_event);
    m_thread.request_stop();
    if (m_thread.joinable()) m_thread.join();
    if (m_client) m_client->Stop();
}

void WasapiOutput::RenderLoop(std::stop_token stop) noexcept {
    // MMCSS: promote to real-time audio thread priority
    DWORD mmcss_task = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_task);
    bool stream_primed = false;

    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
        // Endpoint switch in progress — spin with brief sleep until complete
        if (m_switching.load(std::memory_order_acquire)) {
            ::Sleep(5);
            continue;
        }

        DWORD result = ::WaitForSingleObject(m_ready_event, 50 /*ms timeout*/);
        if (result == WAIT_TIMEOUT) continue;
        if (result != WAIT_OBJECT_0) break;

        // Re-check switching after wakeup (SetEvent used to unblock during switch)
        if (m_switching.load(std::memory_order_acquire)) continue;

        UINT32 padding = 0;
        HRESULT hr = m_client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            // Hot-unplug: fall back to system default without restarting AirPlay
            DUWN_LOG_WARN("WasapiOutput", "Audio device invalidated (unplugged); switching to system default");
            // Post to main thread via a secondary mechanism — call SwitchEndpoint directly
            // since we ARE on the render thread (switching flag will block us briefly, that's OK)
            std::thread([this] { SwitchEndpoint(L""); }).detach();
            break; // exit loop; SwitchEndpoint will start a new render loop via m_client->Start()
        }
        if (FAILED(hr)) break;
        GlobalMetrics().audio_wasapi_padding_ms.store(
            1000.0 * padding / m_sample_rate, std::memory_order_relaxed);

        UINT32 frames_to_write = m_buffer_frames - padding;
        if (frames_to_write == 0) continue;

        BYTE* data = nullptr;
        hr = m_render_client->GetBuffer(frames_to_write, &data);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            DUWN_LOG_WARN("WasapiOutput", "Audio device invalidated on GetBuffer; falling back");
            std::thread([this] { SwitchEndpoint(L""); }).detach();
            break;
        }
        if (FAILED(hr)) break;
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
            // Keep first startup at the measured 20 ms minimum. After a real underrun,
            // re-prime to the adaptive target so the controller's fast increase is
            // actually applied instead of repeatedly restarting at the unstable floor.
            const uint32_t min_stable_frames = m_sample_rate / 50; // 20ms
            const double target_ms = metrics.audio_target_buffer_ms.load(std::memory_order_relaxed);
            const uint32_t adaptive_frames = target_ms > 0.0
                ? static_cast<uint32_t>(std::lround(target_ms * static_cast<double>(m_sample_rate) / 1000.0))
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
            pulled = m_ring.Pull(reinterpret_cast<float*>(data), frames_to_write);
        } else {
            std::memset(data, 0, static_cast<size_t>(frames_to_write) * m_channels * sizeof(float));
        }

        uint32_t silence_frames = frames_to_write - pulled;

        if (pulled > 0) {
            if (!m_first_audio_submitted.exchange(true, std::memory_order_relaxed)) {
                telemetry::ConnectionTimeline::Get().Record(
                    telemetry::ConnectionMilestone::C15_FirstWasapiWrite,
                    std::format("frames={}, rate={}Hz", pulled, m_sample_rate));
                DUWN_LOG_INFOF("Diagnostics",
                    "FIRST EVENT: Audio buffer submitted to WASAPI (frames={}, rate={}Hz)",
                    pulled, m_sample_rate);
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

        DWORD release_flags = m_muted.load(std::memory_order_relaxed) ? AUDCLNT_BUFFERFLAGS_SILENT : 0;
        m_render_client->ReleaseBuffer(frames_to_write, release_flags);
        const auto a6 = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
        GlobalMetrics().audio_a5_a6_ms.store((a6 - a5) / 1'000'000.0, std::memory_order_relaxed);

        GlobalMetrics().audio_samples_rendered.fetch_add(frames_to_write, std::memory_order_relaxed);

        // Update buffer occupancy metric
        double avail_ms = static_cast<double>(m_ring.Available()) / m_sample_rate * 1000.0;
        GlobalMetrics().audio_buffer_ms.store(avail_ms, std::memory_order_relaxed);
    }

    if (mmcss) ::AvRevertMmThreadCharacteristics(mmcss);
}

} // namespace duwn::audio
