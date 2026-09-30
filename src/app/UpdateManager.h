#pragma once

#include "common/Version.h"
#include <string>
#include <string_view>
#include <cstdint>
#include <compare>

namespace duwn::app {

// Semantic version with strict numeric comparison (e.g. 0.2.9 < 0.2.10 < 0.3.0 < 1.0.0)
struct SemanticVersion {
    uint32_t major{0};
    uint32_t minor{0};
    uint32_t patch{0};
    uint32_t build{0};

    static SemanticVersion Parse(std::string_view str) noexcept;
    static SemanticVersion Parse(std::wstring_view str) noexcept;

    std::wstring ToString() const noexcept;
    std::string ToUtf8String() const noexcept;

    constexpr auto operator<=>(const SemanticVersion& other) const noexcept {
        if (auto cmp = major <=> other.major; cmp != 0) return cmp;
        if (auto cmp = minor <=> other.minor; cmp != 0) return cmp;
        if (auto cmp = patch <=> other.patch; cmp != 0) return cmp;
        return build <=> other.build;
    }

    constexpr bool operator==(const SemanticVersion& other) const noexcept {
        return major == other.major && minor == other.minor &&
               patch == other.patch && build == other.build;
    }
};

enum class InstallerState {
    NotInstalled,       // State A: Show "Install Duwn Mirror 0.x.x"
    InstalledOlder,     // State B: Show "Duwn Mirror Update" (Installed < Setup)
    InstalledSame,      // State C: Show maintenance mode "Repair" / "Uninstall"
    InstalledNewer      // State D: Block normal installation (Installed > Setup)
};

struct UpdateInfo {
    SemanticVersion latest_version;
    std::wstring    download_url;
    std::wstring    sha256;
    std::wstring    release_notes;
};

class UpdateManager {
public:
    // Returns the current running application's semantic version from Version.h
    static SemanticVersion GetCurrentVersion() noexcept;

    // Classify installer state given installed version vs setup package version
    static InstallerState ClassifyInstallerState(
        const SemanticVersion& installed_version,
        const SemanticVersion& setup_version) noexcept;

    // Detect installed version via Windows Installer API or Registry
    static bool DetectInstalledVersion(SemanticVersion* out_version) noexcept;

    // Future online update architecture:
    // Verifies update package SHA-256 and Authenticode signature before launching installer.
    // Defense-in-depth: Never execute update package solely because version is newer.
    static bool VerifyUpdatePackage(
        const std::wstring& file_path,
        const std::wstring& expected_sha256) noexcept;

    // Disabled in 0.2.0: local-only architecture without remote network checks
    static constexpr bool IsRemoteCheckEnabled() noexcept { return false; }
};

} // namespace duwn::app
