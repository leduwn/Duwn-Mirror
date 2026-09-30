#include "UpdateManager.h"
#include <windows.h>
#include <msi.h>
#include <winnt.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <format>
#include <vector>
#include <algorithm>
#include <fstream>

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wintrust.lib")

namespace duwn::app {

namespace {
// Stable UpgradeCode from Package.wxs: A3D1E428-8902-4D9A-90A7-6831F901B94C
constexpr wchar_t kDuwnUpgradeCode[] = L"{A3D1E428-8902-4D9A-90A7-6831F901B94C}";
} // namespace

SemanticVersion SemanticVersion::Parse(std::string_view str) noexcept {
    SemanticVersion v{};
    if (str.empty()) return v;

    // Strip leading 'v' or 'V'
    if (str.front() == 'v' || str.front() == 'V') {
        str.remove_prefix(1);
    }

    // Split on '.'
    uint32_t* parts[4] = {&v.major, &v.minor, &v.patch, &v.build};
    size_t part_idx = 0;
    size_t start = 0;

    for (size_t i = 0; i <= str.size() && part_idx < 4; ++i) {
        if (i == str.size() || str[i] == '.') {
            if (i > start) {
                std::string token(str.substr(start, i - start));
                try {
                    *parts[part_idx] = static_cast<uint32_t>(std::stoul(token));
                } catch (...) {
                    *parts[part_idx] = 0;
                }
                ++part_idx;
            }
            start = i + 1;
        }
    }
    return v;
}

SemanticVersion SemanticVersion::Parse(std::wstring_view str) noexcept {
    SemanticVersion v{};
    if (str.empty()) return v;

    if (str.front() == L'v' || str.front() == L'V') {
        str.remove_prefix(1);
    }

    uint32_t* parts[4] = {&v.major, &v.minor, &v.patch, &v.build};
    size_t part_idx = 0;
    size_t start = 0;

    for (size_t i = 0; i <= str.size() && part_idx < 4; ++i) {
        if (i == str.size() || str[i] == L'.') {
            if (i > start) {
                std::wstring token(str.substr(start, i - start));
                try {
                    *parts[part_idx] = static_cast<uint32_t>(std::stoul(token));
                } catch (...) {
                    *parts[part_idx] = 0;
                }
                ++part_idx;
            }
            start = i + 1;
        }
    }
    return v;
}

std::wstring SemanticVersion::ToString() const noexcept {
    if (build > 0) {
        return std::format(L"{}.{}.{}.{}", major, minor, patch, build);
    }
    return std::format(L"{}.{}.{}", major, minor, patch);
}

std::string SemanticVersion::ToUtf8String() const noexcept {
    if (build > 0) {
        return std::format("{}.{}.{}.{}", major, minor, patch, build);
    }
    return std::format("{}.{}.{}", major, minor, patch);
}

SemanticVersion UpdateManager::GetCurrentVersion() noexcept {
    return SemanticVersion{
        DUWN_VERSION_MAJOR,
        DUWN_VERSION_MINOR,
        DUWN_VERSION_PATCH,
        0
    };
}

InstallerState UpdateManager::ClassifyInstallerState(
    const SemanticVersion& installed_version,
    const SemanticVersion& setup_version) noexcept {
    if (installed_version.major == 0 && installed_version.minor == 0 &&
        installed_version.patch == 0 && installed_version.build == 0) {
        return InstallerState::NotInstalled;
    }
    if (installed_version < setup_version) {
        return InstallerState::InstalledOlder;
    }
    if (installed_version == setup_version) {
        return InstallerState::InstalledSame;
    }
    return InstallerState::InstalledNewer;
}

bool UpdateManager::DetectInstalledVersion(SemanticVersion* out_version) noexcept {
    if (!out_version) return false;

    // 1. Query Windows Installer API using stable UpgradeCode
    wchar_t product_code[39] = {};
    UINT res = ::MsiEnumRelatedProductsW(kDuwnUpgradeCode, 0, 0, product_code);
    if (res == ERROR_SUCCESS) {
        wchar_t ver_buf[64] = {};
        DWORD ver_size = sizeof(ver_buf) / sizeof(wchar_t);
        if (::MsiGetProductInfoW(product_code, INSTALLPROPERTY_VERSIONSTRING, ver_buf, &ver_size) == ERROR_SUCCESS) {
            *out_version = SemanticVersion::Parse(std::wstring_view(ver_buf));
            return true;
        }
    }

    // 2. Fallback: Query Registry Uninstall keys
    HKEY uninstall_key = nullptr;
    static constexpr wchar_t kUninstallPath[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstallPath, 0, KEY_READ | KEY_WOW64_64KEY, &uninstall_key) == ERROR_SUCCESS) {
        DWORD subkeys = 0;
        if (::RegQueryInfoKeyW(uninstall_key, nullptr, nullptr, nullptr, &subkeys,
                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < subkeys; ++i) {
                wchar_t key_name[256] = {};
                DWORD name_len = sizeof(key_name) / sizeof(wchar_t);
                if (::RegEnumKeyExW(uninstall_key, i, key_name, &name_len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                    HKEY sub_key = nullptr;
                    if (::RegOpenKeyExW(uninstall_key, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &sub_key) == ERROR_SUCCESS) {
                        wchar_t display_name[128] = {};
                        DWORD dn_size = sizeof(display_name);
                        if (::RegGetValueW(sub_key, nullptr, L"DisplayName", RRF_RT_REG_SZ, nullptr, display_name, &dn_size) == ERROR_SUCCESS) {
                            if (::wcsstr(display_name, L"Duwn Mirror") != nullptr || ::wcsstr(display_name, L"DUWN Mirror") != nullptr) {
                                wchar_t ver_str[64] = {};
                                DWORD vs_size = sizeof(ver_str);
                                if (::RegGetValueW(sub_key, nullptr, L"DisplayVersion", RRF_RT_REG_SZ, nullptr, ver_str, &vs_size) == ERROR_SUCCESS) {
                                    *out_version = SemanticVersion::Parse(std::wstring_view(ver_str));
                                    ::RegCloseKey(sub_key);
                                    ::RegCloseKey(uninstall_key);
                                    return true;
                                }
                            }
                        }
                        ::RegCloseKey(sub_key);
                    }
                }
            }
        }
        ::RegCloseKey(uninstall_key);
    }

    *out_version = SemanticVersion{0, 0, 0, 0};
    return false;
}

bool UpdateManager::VerifyUpdatePackage(
    const std::wstring& file_path,
    const std::wstring& expected_sha256) noexcept {
    // 1. Check file existence
    HANDLE hFile = ::CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    // 2. Verify SHA-256 checksum if provided
    if (!expected_sha256.empty()) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        bool sha_ok = false;

        if (::CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            if (::CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                BYTE buffer[8192];
                DWORD bytes_read = 0;
                bool read_ok = true;
                while (::ReadFile(hFile, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
                    if (!::CryptHashData(hHash, buffer, bytes_read, 0)) {
                        read_ok = false;
                        break;
                    }
                }

                if (read_ok) {
                    BYTE hash_val[32] = {};
                    DWORD hash_len = sizeof(hash_val);
                    if (::CryptGetHashParam(hHash, HP_HASHVAL, hash_val, &hash_len, 0)) {
                        std::wstring hash_hex;
                        for (DWORD i = 0; i < hash_len; ++i) {
                            hash_hex += std::format(L"{:02x}", hash_val[i]);
                        }
                        std::wstring expected_lower = expected_sha256;
                        std::transform(expected_lower.begin(), expected_lower.end(), expected_lower.begin(), ::towlower);
                        sha_ok = (hash_hex == expected_lower);
                    }
                }
                ::CryptDestroyHash(hHash);
            }
            ::CryptReleaseContext(hProv, 0);
        }

        if (!sha_ok) {
            ::CloseHandle(hFile);
            return false;
        }
    }
    ::CloseHandle(hFile);

    // 3. Authenticode signature verification
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(WINTRUST_FILE_INFO);
    file_info.pcwszFilePath = file_path.c_str();

    GUID policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trust_data{};
    trust_data.cbStruct = sizeof(WINTRUST_DATA);
    trust_data.dwUIChoice = WTD_UI_NONE;
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust_data.dwUnionChoice = WTD_CHOICE_FILE;
    trust_data.pFile = &file_info;
    trust_data.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG trust_status = ::WinVerifyTrust(nullptr, &policy_guid, &trust_data);

    trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(nullptr, &policy_guid, &trust_data);

    // Allow success or explicit local development signatures
    return (trust_status == ERROR_SUCCESS);
}

} // namespace duwn::app
