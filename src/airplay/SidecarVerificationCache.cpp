#include "SidecarVerificationCache.h"
#include "common/logging/Logger.h"
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <format>
#include <algorithm>
#include <sstream>

namespace duwn::airplay {

namespace fs = std::filesystem;

static std::string_view ExtractJsonValue(std::string_view json, std::string_view key) noexcept {
    std::string search_key = std::format("\"{}\"", key);
    size_t pos = json.find(search_key);
    if (pos == std::string_view::npos) return {};

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return {};

    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string_view::npos) return {};

    if (json[pos] == '"') {
        size_t end_pos = json.find('"', pos + 1);
        if (end_pos == std::string_view::npos) return {};
        return json.substr(pos + 1, end_pos - (pos + 1));
    } else {
        size_t end_pos = json.find_first_of(",}\r\n", pos);
        if (end_pos == std::string_view::npos) end_pos = json.size();
        return json.substr(pos, end_pos - pos);
    }
}

static bool ParseBool(std::string_view val, bool def_val) noexcept {
    if (val.empty()) return def_val;
    if (val == "true" || val == "1") return true;
    if (val == "false" || val == "0") return false;
    return def_val;
}

static int ParseInt(std::string_view val, int def_val) noexcept {
    if (val.empty()) return def_val;
    try {
        return std::stoi(std::string(val));
    } catch (...) {
        return def_val;
    }
}

std::string SidecarVerificationCache::ComputeFileSha256(const std::wstring& file_path) noexcept {
    HANDLE hFile = ::CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string hash_hex;

    if (::CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (::CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            BYTE buffer[65536];
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
                    hash_hex.reserve(64);
                    for (DWORD i = 0; i < hash_len; ++i) {
                        hash_hex += std::format("{:02x}", hash_val[i]);
                    }
                }
            }
            ::CryptDestroyHash(hHash);
        }
        ::CryptReleaseContext(hProv, 0);
    }

    ::CloseHandle(hFile);
    return hash_hex;
}

fs::path SidecarVerificationCache::GetDefaultCachePath() noexcept {
    wchar_t local_app_data[MAX_PATH]{};
    DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return fs::path(L"cache") / L"sidecar_verification.json";
    }
    return fs::path(local_app_data) / L"Duwn Mirror" / L"cache" / L"sidecar_verification.json";
}

bool SidecarVerificationCache::CheckCache(const std::wstring& exe_path,
                                          bool require_bind_flags,
                                          SidecarCachedVerification& out_cached,
                                          const fs::path& custom_cache_path) noexcept {
    fs::path cache_path = !custom_cache_path.empty() ? custom_cache_path : GetDefaultCachePath();

    std::error_code ec;
    if (!fs::exists(cache_path, ec) || !fs::is_regular_file(cache_path, ec)) {
        return false;
    }

    std::string current_sha = ComputeFileSha256(exe_path);
    if (current_sha.empty()) {
        return false;
    }

    std::ifstream file(cache_path, std::ios::in);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    std::string cached_sha = std::string(ExtractJsonValue(json, "sha256"));
    auto to_lower_char = [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); };
    std::transform(cached_sha.begin(), cached_sha.end(), cached_sha.begin(), to_lower_char);
    std::transform(current_sha.begin(), current_sha.end(), current_sha.begin(), to_lower_char);

    if (cached_sha.empty() || cached_sha != current_sha) {
        return false;
    }

    int min_req_major = ParseInt(ExtractJsonValue(json, "min_required_major"), 0);
    int min_req_minor = ParseInt(ExtractJsonValue(json, "min_required_minor"), 0);
    if (min_req_major != kMinRequiredMajor || min_req_minor != kMinRequiredMinor) {
        return false;
    }

    int major = ParseInt(ExtractJsonValue(json, "major"), 0);
    int minor = ParseInt(ExtractJsonValue(json, "minor"), 0);
    if (major < kMinRequiredMajor || (major == kMinRequiredMajor && minor < kMinRequiredMinor)) {
        return false;
    }

    bool has_vrtp = ParseBool(ExtractJsonValue(json, "has_vrtp"), false);
    if (!has_vrtp) {
        return false;
    }

    bool has_bind_ip = ParseBool(ExtractJsonValue(json, "has_bind_ip"), false);
    bool has_bind_prefix = ParseBool(ExtractJsonValue(json, "has_bind_prefix"), false);
    if (require_bind_flags && (!has_bind_ip || !has_bind_prefix)) {
        return false;
    }

    out_cached.sha256 = current_sha;
    out_cached.major = major;
    out_cached.minor = minor;
    out_cached.has_vrtp = has_vrtp;
    out_cached.has_bind_ip = has_bind_ip;
    out_cached.has_bind_prefix = has_bind_prefix;
    out_cached.min_required_major = min_req_major;
    out_cached.min_required_minor = min_req_minor;

    return true;
}

bool SidecarVerificationCache::SaveCache(const SidecarCachedVerification& record,
                                         const fs::path& custom_cache_path) noexcept {
    fs::path cache_path = !custom_cache_path.empty() ? custom_cache_path : GetDefaultCachePath();

    std::error_code ec;
    fs::create_directories(cache_path.parent_path(), ec);

    fs::path tmp_path = cache_path;
    tmp_path += L".tmp";

    std::ofstream out(tmp_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n"
        << std::format("  \"sha256\": \"{}\",\n", record.sha256)
        << std::format("  \"major\": {},\n", record.major)
        << std::format("  \"minor\": {},\n", record.minor)
        << std::format("  \"has_vrtp\": {},\n", record.has_vrtp ? "true" : "false")
        << std::format("  \"has_bind_ip\": {},\n", record.has_bind_ip ? "true" : "false")
        << std::format("  \"has_bind_prefix\": {},\n", record.has_bind_prefix ? "true" : "false")
        << std::format("  \"min_required_major\": {},\n", record.min_required_major)
        << std::format("  \"min_required_minor\": {}\n", record.min_required_minor)
        << "}\n";
    out.close();

    // Atomic replace on Windows
    if (!::ReplaceFileW(cache_path.c_str(), tmp_path.c_str(), nullptr,
                        REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        // Fall back to remove + rename if target did not exist yet
        fs::remove(cache_path, ec);
        fs::rename(tmp_path, cache_path, ec);
    }
    fs::remove(tmp_path, ec);

    return true;
}

void SidecarVerificationCache::Invalidate(const fs::path& custom_cache_path) noexcept {
    fs::path cache_path = !custom_cache_path.empty() ? custom_cache_path : GetDefaultCachePath();
    std::error_code ec;
    fs::remove(cache_path, ec);
}

} // namespace duwn::airplay
