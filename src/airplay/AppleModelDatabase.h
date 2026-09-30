#pragma once
// AppleModelDatabase.h — Offline lookup table mapping Apple hardware model identifiers
// (e.g., "iPhone15,2", "iPad13,18") to consumer marketing names and operating systems.

#include <string>
#include <string_view>
#include <cstddef>

namespace duwn::airplay {

enum class ModelDbMatch {
    Exact,
    Fallback,
    Unknown
};

struct ModelResolution {
    std::wstring marketing_name;
    std::wstring platform_family; // L"iOS", L"iPadOS", L"tvOS", L"macOS"
    ModelDbMatch match_type{ModelDbMatch::Unknown};
    std::wstring raw_product_type;
};

struct ModelEntry {
    std::wstring_view id;
    std::wstring_view name;
    std::wstring_view os;
};

class AppleModelDatabase {
public:
    static constexpr std::wstring_view kRevision{L"2026.09"};
    static constexpr std::wstring_view kUpdatedDate{L"2026-09-20"};

    // Full lookup returning structured resolution
    static ModelResolution Resolve(std::wstring_view model_id) noexcept;

    // Resolves Apple model identifier (e.g. "iPhone15,2") to marketing name ("iPhone 14 Pro").
    // If unknown, returns fallback family (e.g. "iPhone", "iPad", "Apple Device").
    static std::wstring GetMarketingName(std::wstring_view model_id) noexcept;

    // Detects OS family: "iOS", "iPadOS", or "macOS" from model string or user agent.
    static std::wstring DetectOsName(std::wstring_view model_id, std::wstring_view user_agent = L"") noexcept;

    // Returns whether model_id is an exact match in the database
    static bool IsExactMatch(std::wstring_view model_id) noexcept;

    // Total count of entries in the database
    static size_t EntryCount() noexcept;

    // iPhone entry count
    static size_t IPhoneCount() noexcept;

    // iPad entry count
    static size_t IPadCount() noexcept;

    // Direct access to table for test duplicate detection and audit
    static const ModelEntry* AllEntries(size_t& count) noexcept;
};

} // namespace duwn::airplay
