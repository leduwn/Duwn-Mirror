#include "AudioDeviceManager.h"
#include "common/logging/Logger.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

namespace duwn::audio {

AudioDeviceManager::~AudioDeviceManager() {
    Shutdown();
}

bool AudioDeviceManager::Init(HWND hwnd) noexcept {
    m_hwnd = hwnd;

    HRESULT hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&m_enumerator));
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("AudioDeviceManager",
            "CoCreateInstance MMDeviceEnumerator failed {:#010x}",
            static_cast<unsigned>(hr));
        return false;
    }

    if (hwnd) {
        hr = m_enumerator->RegisterEndpointNotificationCallback(this);
        if (FAILED(hr)) {
            DUWN_LOG_WARN("AudioDeviceManager",
                "RegisterEndpointNotificationCallback failed — hot-plug disabled");
        }
    }

    DUWN_LOG_INFO("AudioDeviceManager", "Initialized");
    return true;
}

void AudioDeviceManager::Shutdown() noexcept {
    if (m_enumerator && m_hwnd) {
        m_enumerator->UnregisterEndpointNotificationCallback(this);
    }
    m_enumerator.Reset();
    m_hwnd = nullptr;
}

std::vector<AudioEndpointInfo> AudioDeviceManager::Enumerate() noexcept {
    std::vector<AudioEndpointInfo> result;
    if (!m_enumerator) return result;

    ComPtr<IMMDeviceCollection> collection;
    HRESULT hr = m_enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return result;

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) continue;

        LPWSTR pwstr_id = nullptr;
        if (FAILED(device->GetId(&pwstr_id))) continue;
        std::wstring id = pwstr_id;
        ::CoTaskMemFree(pwstr_id);

        // Friendly name via property store
        std::wstring friendly;
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT pv;
            ::PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                pv.vt == VT_LPWSTR && pv.pwszVal) {
                friendly = pv.pwszVal;
            }
            ::PropVariantClear(&pv);
        }
        if (friendly.empty()) friendly = id;

        result.push_back({ std::move(id), std::move(friendly) });
    }

    std::sort(result.begin(), result.end(),
        [](const AudioEndpointInfo& a, const AudioEndpointInfo& b) {
            return a.friendly_name < b.friendly_name;
        });

    return result;
}

std::wstring AudioDeviceManager::DefaultDeviceId() noexcept {
    if (!m_enumerator) return {};
    ComPtr<IMMDevice> device;
    HRESULT hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr)) return {};
    LPWSTR pwstr = nullptr;
    if (FAILED(device->GetId(&pwstr))) return {};
    std::wstring id = pwstr;
    ::CoTaskMemFree(pwstr);
    return id;
}

AudioEndpointInfo AudioDeviceManager::GetDeviceInfo(const std::wstring& id) noexcept {
    AudioEndpointInfo info;
    info.id = id;
    info.available = false;
    if (id.empty() || !m_enumerator) return info;

    ComPtr<IMMDevice> device;
    if (FAILED(m_enumerator->GetDevice(id.c_str(), device.GetAddressOf())) || !device) {
        info.friendly_name = id;
        return info;
    }

    DWORD state = 0;
    info.available = SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE;
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
        PROPVARIANT pv;
        ::PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal) {
            info.friendly_name = pv.pwszVal;
        }
        ::PropVariantClear(&pv);
    }
    if (info.friendly_name.empty()) info.friendly_name = id;
    return info;
}

bool AudioDeviceManager::IsDeviceActive(const std::wstring& id) noexcept {
    if (id.empty() || !m_enumerator) return false;
    ComPtr<IMMDevice> device;
    HRESULT hr = m_enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr) || !device) return false;
    DWORD state = 0;
    hr = device->GetState(&state);
    return SUCCEEDED(hr) && (state == DEVICE_STATE_ACTIVE);
}

void AudioDeviceManager::SetWatchedDeviceId(const std::wstring& id) noexcept {
    ::AcquireSRWLockExclusive(&m_watched_lock);
    m_watched_device_id = id;
    ::ReleaseSRWLockExclusive(&m_watched_lock);
}

// IUnknown

HRESULT STDMETHODCALLTYPE AudioDeviceManager::QueryInterface(REFIID riid, void** ppv) {
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IMMNotificationClient)) {
        *ppv = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE AudioDeviceManager::Release() {
    ULONG ref = m_ref.fetch_sub(1) - 1;
    // AudioDeviceManager is stack/member owned — never self-delete
    return ref;
}

// IMMNotificationClient

HRESULT STDMETHODCALLTYPE AudioDeviceManager::OnDefaultDeviceChanged(
    EDataFlow flow, ERole role, LPCWSTR /*pwstrDefaultDeviceId*/) {
    // Only care about render + multimedia role (our eMultimedia default)
    if (flow != eRender || role != eMultimedia) return S_OK;

    // Use non-blocking lock to guarantee zero thread stall on Windows audio callback
    bool watching_default = false;
    if (::TryAcquireSRWLockShared(&m_watched_lock)) {
        watching_default = m_watched_device_id.empty();
        ::ReleaseSRWLockShared(&m_watched_lock);
    } else {
        watching_default = true; // safe fallback: dispatch change
    }

    if (watching_default && m_hwnd) {
        ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_CHANGED, 0, 0);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceManager::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    if (m_hwnd) ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_LIST_CHANGED, 0, 0);

    bool matches_watched = false;
    if (::TryAcquireSRWLockShared(&m_watched_lock)) {
        matches_watched = (!m_watched_device_id.empty() && pwstrDeviceId &&
                            m_watched_device_id == pwstrDeviceId);
        ::ReleaseSRWLockShared(&m_watched_lock);
    } else {
        matches_watched = true;
    }

    if (matches_watched && m_hwnd) {
        ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_CHANGED, 0, 0);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceManager::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    if (m_hwnd) ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_LIST_CHANGED, 0, 0);

    bool is_active = false;
    if (::TryAcquireSRWLockShared(&m_watched_lock)) {
        is_active = (!m_watched_device_id.empty() && pwstrDeviceId &&
                      m_watched_device_id == pwstrDeviceId);
        ::ReleaseSRWLockShared(&m_watched_lock);
    } else {
        is_active = true;
    }

    if (is_active && m_hwnd) {
        ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_CHANGED, 0, 0);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceManager::OnDeviceStateChanged(
    LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    if (m_hwnd) ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_LIST_CHANGED, 0, 0);

    bool is_watched = false;
    if (::TryAcquireSRWLockShared(&m_watched_lock)) {
        is_watched = (!m_watched_device_id.empty() && pwstrDeviceId &&
                       m_watched_device_id == pwstrDeviceId);
        ::ReleaseSRWLockShared(&m_watched_lock);
    } else {
        is_watched = true;
    }

    if (is_watched && m_hwnd) {
        // Trigger on both unplug/disable and reconnect/active
        if (dwNewState == DEVICE_STATE_ACTIVE ||
            dwNewState == DEVICE_STATE_NOTPRESENT ||
            dwNewState == DEVICE_STATE_DISABLED ||
            dwNewState == DEVICE_STATE_UNPLUGGED) {
            ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_CHANGED, 0, 0);
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceManager::OnPropertyValueChanged(
    LPCWSTR pwstrDeviceId, const PROPERTYKEY /*key*/) {
    if (m_hwnd) ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_LIST_CHANGED, 0, 0);

    bool affects_selection = false;
    if (::TryAcquireSRWLockShared(&m_watched_lock)) {
        affects_selection = m_watched_device_id.empty() ||
            (pwstrDeviceId && m_watched_device_id == pwstrDeviceId);
        ::ReleaseSRWLockShared(&m_watched_lock);
    } else {
        affects_selection = true;
    }

    if (affects_selection && m_hwnd) {
        ::PostMessageW(m_hwnd, WM_APP_AUDIO_DEVICE_CHANGED, 0, 0);
    }
    return S_OK;
}

} // namespace duwn::audio
