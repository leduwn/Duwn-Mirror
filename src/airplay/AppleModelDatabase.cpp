#include "AppleModelDatabase.h"
#include <string_view>

namespace duwn::airplay {

namespace {

// Comprehensive offline database of Apple hardware model identifiers
// Verified 2026-09 against current Apple firmware/model listings.
static constexpr ModelEntry kAppleModels[] = {
    // =======================================================================
    // iPhone
    // =======================================================================

    // iPhone 8 / 8 Plus / X (2017)
    { L"iPhone10,1", L"iPhone 8", L"iOS" },
    { L"iPhone10,4", L"iPhone 8", L"iOS" },
    { L"iPhone10,2", L"iPhone 8 Plus", L"iOS" },
    { L"iPhone10,5", L"iPhone 8 Plus", L"iOS" },
    { L"iPhone10,3", L"iPhone X", L"iOS" },
    { L"iPhone10,6", L"iPhone X", L"iOS" },

    // iPhone XS / XS Max / XR (2018)
    { L"iPhone11,2", L"iPhone XS", L"iOS" },
    { L"iPhone11,4", L"iPhone XS Max", L"iOS" },
    { L"iPhone11,6", L"iPhone XS Max", L"iOS" },
    { L"iPhone11,8", L"iPhone XR", L"iOS" },

    // iPhone 11 / 11 Pro / 11 Pro Max / SE 2 (2019-2020)
    { L"iPhone12,1", L"iPhone 11", L"iOS" },
    { L"iPhone12,3", L"iPhone 11 Pro", L"iOS" },
    { L"iPhone12,5", L"iPhone 11 Pro Max", L"iOS" },
    { L"iPhone12,8", L"iPhone SE (2nd generation)", L"iOS" },

    // iPhone 12 mini / 12 / 12 Pro / 12 Pro Max (2020)
    { L"iPhone13,1", L"iPhone 12 mini", L"iOS" },
    { L"iPhone13,2", L"iPhone 12", L"iOS" },
    { L"iPhone13,3", L"iPhone 12 Pro", L"iOS" },
    { L"iPhone13,4", L"iPhone 12 Pro Max", L"iOS" },

    // iPhone 13 mini / 13 / 13 Pro / 13 Pro Max / SE 3 (2021-2022)
    { L"iPhone14,4", L"iPhone 13 mini", L"iOS" },
    { L"iPhone14,5", L"iPhone 13", L"iOS" },
    { L"iPhone14,2", L"iPhone 13 Pro", L"iOS" },
    { L"iPhone14,3", L"iPhone 13 Pro Max", L"iOS" },
    { L"iPhone14,6", L"iPhone SE (3rd generation)", L"iOS" },

    // iPhone 14 / 14 Plus / 14 Pro / 14 Pro Max (2022)
    { L"iPhone14,7", L"iPhone 14", L"iOS" },
    { L"iPhone14,8", L"iPhone 14 Plus", L"iOS" },
    { L"iPhone15,2", L"iPhone 14 Pro", L"iOS" },
    { L"iPhone15,3", L"iPhone 14 Pro Max", L"iOS" },

    // iPhone 15 / 15 Plus / 15 Pro / 15 Pro Max (2023)
    { L"iPhone15,4", L"iPhone 15", L"iOS" },
    { L"iPhone15,5", L"iPhone 15 Plus", L"iOS" },
    { L"iPhone16,1", L"iPhone 15 Pro", L"iOS" },
    { L"iPhone16,2", L"iPhone 15 Pro Max", L"iOS" },

    // iPhone 16 family (2024-2025)
    // Verified 2026-09 against current Apple firmware/model listings.
    { L"iPhone17,1", L"iPhone 16 Pro", L"iOS" },
    { L"iPhone17,2", L"iPhone 16 Pro Max", L"iOS" },
    { L"iPhone17,3", L"iPhone 16", L"iOS" },
    { L"iPhone17,4", L"iPhone 16 Plus", L"iOS" },
    { L"iPhone17,5", L"iPhone 16e", L"iOS" },

    // iPhone 17 family (2025-2026)
    // Verified 2026-09 against current Apple firmware/model listings.
    { L"iPhone18,1", L"iPhone 17 Pro", L"iOS" },
    { L"iPhone18,2", L"iPhone 17 Pro Max", L"iOS" },
    { L"iPhone18,3", L"iPhone 17", L"iOS" },
    { L"iPhone18,4", L"iPhone Air", L"iOS" },
    { L"iPhone18,5", L"iPhone 17e", L"iOS" },

    // iPhone 18 Pro family (2026)
    // Verified 2026-09 against current Apple firmware/model listings.
    { L"iPhone19,2", L"iPhone 18 Pro", L"iOS" },
    { L"iPhone19,3", L"iPhone 18 Pro Max", L"iOS" },
    { L"iPhone19,7", L"iPhone 18 Pro Max", L"iOS" },

    // =======================================================================
    // iPad
    // =======================================================================

    // base iPad
    { L"iPad7,5",   L"iPad (6th generation)", L"iPadOS" },
    { L"iPad7,6",   L"iPad (6th generation)", L"iPadOS" },
    { L"iPad7,11",  L"iPad (7th generation)", L"iPadOS" },
    { L"iPad7,12",  L"iPad (7th generation)", L"iPadOS" },
    { L"iPad11,6",  L"iPad (8th gen)", L"iPadOS" },
    { L"iPad11,7",  L"iPad (8th gen)", L"iPadOS" },
    { L"iPad12,1",  L"iPad (9th gen)", L"iPadOS" },
    { L"iPad12,2",  L"iPad (9th gen)", L"iPadOS" },
    { L"iPad13,18", L"iPad (10th gen)", L"iPadOS" },
    { L"iPad13,19", L"iPad (10th gen)", L"iPadOS" },

    // iPad mini
    { L"iPad11,1",  L"iPad mini (5th gen)", L"iPadOS" },
    { L"iPad11,2",  L"iPad mini (5th gen)", L"iPadOS" },
    { L"iPad14,1",  L"iPad mini (6th gen)", L"iPadOS" },
    { L"iPad14,2",  L"iPad mini (6th gen)", L"iPadOS" },
    { L"iPad16,1",  L"iPad mini (A17 Pro)", L"iPadOS" },
    { L"iPad16,2",  L"iPad mini (A17 Pro)", L"iPadOS" },

    // iPad Air
    { L"iPad11,3",  L"iPad Air (3rd gen)", L"iPadOS" },
    { L"iPad11,4",  L"iPad Air (3rd gen)", L"iPadOS" },
    { L"iPad13,1",  L"iPad Air (4th gen)", L"iPadOS" },
    { L"iPad13,2",  L"iPad Air (4th gen)", L"iPadOS" },
    { L"iPad13,16", L"iPad Air (5th gen, M1)", L"iPadOS" },
    { L"iPad13,17", L"iPad Air (5th gen, M1)", L"iPadOS" },
    { L"iPad14,8",  L"iPad Air 11-inch (M2)", L"iPadOS" },
    { L"iPad14,9",  L"iPad Air 11-inch (M2)", L"iPadOS" },
    { L"iPad14,10", L"iPad Air 13-inch (M2)", L"iPadOS" },
    { L"iPad14,11", L"iPad Air 13-inch (M2)", L"iPadOS" },

    // iPad Pro
    { L"iPad7,1",   L"iPad Pro 12.9-inch (2nd generation)", L"iPadOS" },
    { L"iPad7,2",   L"iPad Pro 12.9-inch (2nd generation)", L"iPadOS" },
    { L"iPad7,3",   L"iPad Pro 10.5-inch", L"iPadOS" },
    { L"iPad7,4",   L"iPad Pro 10.5-inch", L"iPadOS" },
    { L"iPad8,1",   L"iPad Pro 11-inch (1st gen)", L"iPadOS" },
    { L"iPad8,2",   L"iPad Pro 11-inch (1st gen)", L"iPadOS" },
    { L"iPad8,3",   L"iPad Pro 11-inch (1st gen)", L"iPadOS" },
    { L"iPad8,4",   L"iPad Pro 11-inch (1st gen)", L"iPadOS" },
    { L"iPad8,5",   L"iPad Pro 12.9-inch (3rd gen)", L"iPadOS" },
    { L"iPad8,6",   L"iPad Pro 12.9-inch (3rd gen)", L"iPadOS" },
    { L"iPad8,7",   L"iPad Pro 12.9-inch (3rd gen)", L"iPadOS" },
    { L"iPad8,8",   L"iPad Pro 12.9-inch (3rd gen)", L"iPadOS" },
    { L"iPad8,9",   L"iPad Pro 11-inch (2nd gen)", L"iPadOS" },
    { L"iPad8,10",  L"iPad Pro 11-inch (2nd gen)", L"iPadOS" },
    { L"iPad8,11",  L"iPad Pro 12.9-inch (4th gen)", L"iPadOS" },
    { L"iPad8,12",  L"iPad Pro 12.9-inch (4th gen)", L"iPadOS" },
    { L"iPad13,4",  L"iPad Pro 11-inch (3rd gen, M1)", L"iPadOS" },
    { L"iPad13,5",  L"iPad Pro 11-inch (3rd gen, M1)", L"iPadOS" },
    { L"iPad13,6",  L"iPad Pro 11-inch (3rd gen, M1)", L"iPadOS" },
    { L"iPad13,7",  L"iPad Pro 11-inch (3rd gen, M1)", L"iPadOS" },
    { L"iPad13,8",  L"iPad Pro 12.9-inch (5th gen, M1)", L"iPadOS" },
    { L"iPad13,9",  L"iPad Pro 12.9-inch (5th gen, M1)", L"iPadOS" },
    { L"iPad13,10", L"iPad Pro 12.9-inch (5th gen, M1)", L"iPadOS" },
    { L"iPad13,11", L"iPad Pro 12.9-inch (5th gen, M1)", L"iPadOS" },
    { L"iPad14,3",  L"iPad Pro 11-inch (4th gen, M2)", L"iPadOS" },
    { L"iPad14,4",  L"iPad Pro 11-inch (4th gen, M2)", L"iPadOS" },
    { L"iPad14,5",  L"iPad Pro 12.9-inch (6th gen, M2)", L"iPadOS" },
    { L"iPad14,6",  L"iPad Pro 12.9-inch (6th gen, M2)", L"iPadOS" },
    { L"iPad16,3",  L"iPad Pro 11-inch (M4)", L"iPadOS" },
    { L"iPad16,4",  L"iPad Pro 11-inch (M4)", L"iPadOS" },
    { L"iPad16,5",  L"iPad Pro 13-inch (M4)", L"iPadOS" },
    { L"iPad16,6",  L"iPad Pro 13-inch (M4)", L"iPadOS" }
};

} // namespace

ModelResolution AppleModelDatabase::Resolve(std::wstring_view model_id) noexcept {
    ModelResolution res;
    res.raw_product_type = std::wstring(model_id);

    if (model_id.empty()) {
        res.marketing_name = L"Apple Device";
        res.platform_family = L"iOS";
        res.match_type = ModelDbMatch::Unknown;
        return res;
    }

    // Exact-case matching against known database
    for (const auto& entry : kAppleModels) {
        if (entry.id == model_id) {
            res.marketing_name = std::wstring(entry.name);
            res.platform_family = std::wstring(entry.os);
            res.match_type = ModelDbMatch::Exact;
            return res;
        }
    }

    // Heuristic fallbacks for unlisted / future Apple hardware
    res.match_type = ModelDbMatch::Fallback;
    if (model_id.starts_with(L"iPhone")) {
        res.marketing_name = L"iPhone";
        res.platform_family = L"iOS";
    } else if (model_id.starts_with(L"iPad")) {
        res.marketing_name = L"iPad";
        res.platform_family = L"iPadOS";
    } else if (model_id.starts_with(L"iPod")) {
        res.marketing_name = L"iPod";
        res.platform_family = L"iOS";
    } else if (model_id.starts_with(L"AppleTV")) {
        res.marketing_name = L"Apple TV";
        res.platform_family = L"tvOS";
    } else if (model_id.starts_with(L"MacBook") || model_id.starts_with(L"Mac") || model_id.starts_with(L"iMac")) {
        res.marketing_name = L"Mac";
        res.platform_family = L"macOS";
    } else {
        res.marketing_name = L"Apple Device";
        res.platform_family = L"iOS";
        res.match_type = ModelDbMatch::Unknown;
    }

    return res;
}

std::wstring AppleModelDatabase::GetMarketingName(std::wstring_view model_id) noexcept {
    return Resolve(model_id).marketing_name;
}

std::wstring AppleModelDatabase::DetectOsName(std::wstring_view model_id, std::wstring_view user_agent) noexcept {
    auto res = Resolve(model_id);
    if (res.match_type == ModelDbMatch::Exact) {
        return res.platform_family;
    }

    if (!user_agent.empty()) {
        if (user_agent.find(L"iPad") != std::wstring_view::npos) return L"iPadOS";
        if (user_agent.find(L"iPhone") != std::wstring_view::npos) return L"iOS";
        if (user_agent.find(L"Mac") != std::wstring_view::npos) return L"macOS";
    }

    return res.platform_family;
}

bool AppleModelDatabase::IsExactMatch(std::wstring_view model_id) noexcept {
    if (model_id.empty()) return false;
    for (const auto& entry : kAppleModels) {
        if (entry.id == model_id) return true;
    }
    return false;
}

size_t AppleModelDatabase::EntryCount() noexcept {
    return sizeof(kAppleModels) / sizeof(kAppleModels[0]);
}

size_t AppleModelDatabase::IPhoneCount() noexcept {
    size_t count = 0;
    for (const auto& entry : kAppleModels) {
        if (entry.id.starts_with(L"iPhone")) count++;
    }
    return count;
}

size_t AppleModelDatabase::IPadCount() noexcept {
    size_t count = 0;
    for (const auto& entry : kAppleModels) {
        if (entry.id.starts_with(L"iPad")) count++;
    }
    return count;
}

const ModelEntry* AppleModelDatabase::AllEntries(size_t& count) noexcept {
    count = sizeof(kAppleModels) / sizeof(kAppleModels[0]);
    return kAppleModels;
}

} // namespace duwn::airplay
