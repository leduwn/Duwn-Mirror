#include "DuwnVirtualCam.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static HMODULE g_hModule = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        ::DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

class VirtualCamClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto* filter = new duwn::vcam::DuwnVirtualCamFilter();
        HRESULT hr = filter->QueryInterface(riid, ppv);
        filter->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override { return S_OK; }

private:
    std::atomic<ULONG> m_ref{1};
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (rclsid == CLSID_DuwnMirrorVirtualCam) {
        auto* factory = new VirtualCamClassFactory();
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return S_OK;
}

static const WCHAR kClsidString[] = L"{8B9F51B8-3232-4518-A7D9-4828E0D71B20}";
static const WCHAR kFriendlyName[] = L"Duwn Mirror Video";
static const WCHAR kVideoInputCat[] = L"{860BB310-5D01-11d0-BD3B-00A0C911CE86}";

STDAPI DllRegisterServer() {
    WCHAR module_path[MAX_PATH];
    if (::GetModuleFileNameW(g_hModule, module_path, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    // 1. Register in HKCU\Software\Classes\CLSID\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}
    std::wstring clsid_key = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    HKEY hkey = nullptr;
    LSTATUS status = ::RegCreateKeyExW(HKEY_CURRENT_USER, clsid_key.c_str(), 0, nullptr,
                                       REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    ::RegSetValueExW(hkey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(kFriendlyName),
                     static_cast<DWORD>((wcslen(kFriendlyName) + 1) * sizeof(WCHAR)));

    HKEY hkey_inproc = nullptr;
    status = ::RegCreateKeyExW(hkey, L"InprocServer32", 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey_inproc, nullptr);
    if (status == ERROR_SUCCESS) {
        ::RegSetValueExW(hkey_inproc, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(module_path),
                         static_cast<DWORD>((wcslen(module_path) + 1) * sizeof(WCHAR)));
        const WCHAR thread_model[] = L"Both";
        ::RegSetValueExW(hkey_inproc, L"ThreadingModel", 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(thread_model),
                         static_cast<DWORD>((wcslen(thread_model) + 1) * sizeof(WCHAR)));
        ::RegCloseKey(hkey_inproc);
    }
    ::RegCloseKey(hkey);

    // 2. Register under Video Input Category:
    // HKCU\Software\Classes\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}
    std::wstring cat_key = std::wstring(L"Software\\Classes\\CLSID\\") + kVideoInputCat +
                           L"\\Instance\\" + kClsidString;
    HKEY hkey_cat = nullptr;
    status = ::RegCreateKeyExW(HKEY_CURRENT_USER, cat_key.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey_cat, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    ::RegSetValueExW(hkey_cat, L"FriendlyName", 0, REG_SZ, reinterpret_cast<const BYTE*>(kFriendlyName),
                     static_cast<DWORD>((wcslen(kFriendlyName) + 1) * sizeof(WCHAR)));
    ::RegSetValueExW(hkey_cat, L"CLSID", 0, REG_SZ, reinterpret_cast<const BYTE*>(kClsidString),
                     static_cast<DWORD>((wcslen(kClsidString) + 1) * sizeof(WCHAR)));
    ::RegCloseKey(hkey_cat);

    return S_OK;
}

STDAPI DllUnregisterServer() {
    std::wstring clsid_key = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    ::SHDeleteKeyW(HKEY_CURRENT_USER, clsid_key.c_str());

    std::wstring cat_key = std::wstring(L"Software\\Classes\\CLSID\\") + kVideoInputCat +
                           L"\\Instance\\" + kClsidString;
    ::SHDeleteKeyW(HKEY_CURRENT_USER, cat_key.c_str());

    return S_OK;
}