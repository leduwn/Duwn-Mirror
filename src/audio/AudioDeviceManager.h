#pragma once
// AudioDeviceManager — enumerates WASAPI render endpoints and watches for hot-plug
// events via IMMNotificationClient.
//
// Usage:
//   AudioDeviceManager mgr;
//   mgr.Init(hwnd);          // posts WM_APP_AUDIO_* messages to hwnd on changes
//   auto devs = mgr.Enumerate();
//   mgr.Shutdown();
//
// Message constants (posted to the registered HWND):
//   WM_APP_AUDIO_DEVICE_CHANGED      (0xB001) — default device changed or active
//                                                device unplugged; wParam = 0
//   WM_APP_AUDIO_DEVICE_LIST_CHANGED (0xB002) — device added/removed; re-enumerate

#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <atomic>
#include <windows.h>

namespace duwn::audio {

using Microsoft::WRL::ComPtr;

constexpr UINT WM_APP_AUDIO_DEVICE_CHANGED      = WM_APP + 0x001;
constexpr UINT WM_APP_AUDIO_DEVICE_LIST_CHANGED = WM_APP + 0x002;

struct AudioEndpointInfo {
    std::wstring id;            // IMMDevice::GetId() — stable across reboots
    std::wstring friendly_name; // PKEY_Device_FriendlyName
};

class AudioDeviceManager : public IMMNotificationClient {
public:
    AudioDeviceManager() noexcept = default;
    ~AudioDeviceManager();

    AudioDeviceManager(const AudioDeviceManager&) = delete;
    AudioDeviceManager& operator=(const AudioDeviceManager&) = delete;

    // Init: creates IMMDeviceEnumerator and registers IMMNotificationClient.
    // hwnd receives WM_APP_AUDIO_DEVICE_* messages. Pass nullptr to disable notifications.
    bool Init(HWND hwnd) noexcept;
    void Shutdown() noexcept;

    // Enumerate active render endpoints. First call may be slow (COM).
    // Returns list sorted by friendly name; call after WM_APP_AUDIO_DEVICE_LIST_CHANGED.
    std::vector<AudioEndpointInfo> Enumerate() noexcept;

    // Returns the endpoint ID of the current system default render device.
    // Empty string on failure.
    std::wstring DefaultDeviceId() noexcept;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override { return m_ref.fetch_add(1) + 1; }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
        LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override { return S_OK; }

    // Current active device ID being monitored (set by App when user selects a device).
    // Used to determine if OnDeviceStateChanged should post DEVICE_CHANGED.
    void SetWatchedDeviceId(const std::wstring& id) noexcept;

private:
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    HWND                        m_hwnd{nullptr};
    std::atomic<ULONG>          m_ref{1};
    std::wstring                m_watched_device_id; // empty = watching default
    mutable SRWLOCK             m_watched_lock = SRWLOCK_INIT;
};

} // namespace duwn::audio
