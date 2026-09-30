#pragma once
// SidecarVerificationCache.h — SHA-256-based verification cache for AirPlay sidecar (uxplay.exe).
// Avoids redundant process execution (~185 ms) on warm launches while guaranteeing full
// cryptographic integrity and version/capability requirements.

#include <string>
#include <string_view>
#include <cstdint>
#include <filesystem>

namespace duwn::airplay {

struct SidecarCachedVerification {
    std::string sha256;
    int         major{0};
    int         minor{0};
    bool        has_vrtp{false};
    bool        has_bind_ip{false};
    bool        has_bind_prefix{false};
    int         min_required_major{1};
    int         min_required_minor{73};
};

class SidecarVerificationCache {
public:
    static constexpr int kMinRequiredMajor = 1;
    static constexpr int kMinRequiredMinor = 73;

    // Computes lowercase hex SHA-256 of the given file path via Win32 CryptoAPI.
    // Returns empty string on error / file missing.
    static std::string ComputeFileSha256(const std::wstring& file_path) noexcept;

    // Resolves standard cache path: %LOCALAPPDATA%\Duwn Mirror\cache\sidecar_verification.json
    static std::filesystem::path GetDefaultCachePath() noexcept;

    // Checks whether cache matches the executable's SHA-256 and meets compatibility requirements.
    // If valid, populates out_cached and returns true (cache hit).
    // If invalid, missing, or incompatible, returns false (cache miss).
    static bool CheckCache(const std::wstring& exe_path,
                           bool require_bind_flags,
                           SidecarCachedVerification& out_cached,
                           const std::filesystem::path& custom_cache_path = {}) noexcept;

    // Atomically saves verified record to cache path.
    static bool SaveCache(const SidecarCachedVerification& record,
                          const std::filesystem::path& custom_cache_path = {}) noexcept;

    // Invalidates (deletes) cache file.
    static void Invalidate(const std::filesystem::path& custom_cache_path = {}) noexcept;
};

} // namespace duwn::airplay
