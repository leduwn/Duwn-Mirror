// test_apple_model_database.cpp — Unit tests for AppleModelDatabase
// Verified 2026-09 against current Apple firmware and model listings.

#include "airplay/AppleModelDatabase.h"
#include <set>
#include <string>
#include <string_view>

using namespace duwn::airplay;

// ---------------------------------------------------------------------------
// 1. Zero duplicate entries test
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_NoDuplicateEntries) {
    size_t count = 0;
    const ModelEntry* entries = AppleModelDatabase::AllEntries(count);
    DUWN_ASSERT(entries != nullptr);
    DUWN_ASSERT(count > 0);

    std::set<std::wstring_view> seen;
    for (size_t i = 0; i < count; ++i) {
        auto id = entries[i].id;
        DUWN_ASSERT(!id.empty());
        DUWN_ASSERT(seen.find(id) == seen.end());
        seen.insert(id);
    }
    DUWN_ASSERT(seen.size() == count);
}

// ---------------------------------------------------------------------------
// 2. Exact iPhone lookups (including 2024-2026 verified models)
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_ExactLookups_iPhone) {
    // iPhone 8 / X
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone10,1") == L"iPhone 8");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone10,3") == L"iPhone X");

    // iPhone XS / XR
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone11,2") == L"iPhone XS");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone11,8") == L"iPhone XR");

    // iPhone 11 / SE 2
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone12,1") == L"iPhone 11");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone12,8") == L"iPhone SE (2nd generation)");

    // iPhone 12 / mini
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone13,1") == L"iPhone 12 mini");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone13,2") == L"iPhone 12");

    // iPhone 13 / SE 3
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone14,2") == L"iPhone 13 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone14,6") == L"iPhone SE (3rd generation)");

    // iPhone 14
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone14,7") == L"iPhone 14");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone15,2") == L"iPhone 14 Pro");

    // iPhone 15
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone15,4") == L"iPhone 15");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone16,1") == L"iPhone 15 Pro");

    // iPhone 16 family
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,1") == L"iPhone 16 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,2") == L"iPhone 16 Pro Max");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,3") == L"iPhone 16");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,4") == L"iPhone 16 Plus");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,5") == L"iPhone 16e");

    // iPhone 17 family
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone18,1") == L"iPhone 17 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone18,2") == L"iPhone 17 Pro Max");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone18,3") == L"iPhone 17");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone18,4") == L"iPhone Air");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone18,5") == L"iPhone 17e");

    // iPhone 18 Pro family (2026)
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone19,2") == L"iPhone 18 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone19,3") == L"iPhone 18 Pro Max");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone19,7") == L"iPhone 18 Pro Max");

    // Prohibited invented models must NOT be exact matches
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"iPhone19,1"));
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"iPhone19,4"));
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"iPhone19,5"));
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"iPhone19,6"));

    // Check structured resolution
    auto res18 = AppleModelDatabase::Resolve(L"iPhone18,1");
    DUWN_ASSERT(res18.marketing_name == L"iPhone 17 Pro");
    DUWN_ASSERT(res18.platform_family == L"iOS");
    DUWN_ASSERT(res18.match_type == ModelDbMatch::Exact);
    DUWN_ASSERT(res18.raw_product_type == L"iPhone18,1");
}

// ---------------------------------------------------------------------------
// 3. Exact iPad lookups
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_ExactLookups_iPad) {
    // Base iPads
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad7,5") == L"iPad (6th generation)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad7,11") == L"iPad (7th generation)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad11,6") == L"iPad (8th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad12,1") == L"iPad (9th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad13,18") == L"iPad (10th gen)");

    // iPad mini
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad11,1") == L"iPad mini (5th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad14,1") == L"iPad mini (6th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad16,1") == L"iPad mini (A17 Pro)");

    // iPad Air
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad11,3") == L"iPad Air (3rd gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad13,1") == L"iPad Air (4th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad13,16") == L"iPad Air (5th gen, M1)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad14,8") == L"iPad Air 11-inch (M2)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad14,10") == L"iPad Air 13-inch (M2)");

    // iPad Pro
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad8,1") == L"iPad Pro 11-inch (1st gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad8,5") == L"iPad Pro 12.9-inch (3rd gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad13,4") == L"iPad Pro 11-inch (3rd gen, M1)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad14,3") == L"iPad Pro 11-inch (4th gen, M2)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad16,3") == L"iPad Pro 11-inch (M4)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad16,5") == L"iPad Pro 13-inch (M4)");

    auto resPad = AppleModelDatabase::Resolve(L"iPad16,3");
    DUWN_ASSERT(resPad.marketing_name == L"iPad Pro 11-inch (M4)");
    DUWN_ASSERT(resPad.platform_family == L"iPadOS");
    DUWN_ASSERT(resPad.match_type == ModelDbMatch::Exact);
}

// ---------------------------------------------------------------------------
// 4. Fallback matches and graceful degradation
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_FallbackLookups) {
    // Unlisted future iPhone
    auto resPhone = AppleModelDatabase::Resolve(L"iPhone99,1");
    DUWN_ASSERT(resPhone.marketing_name == L"iPhone");
    DUWN_ASSERT(resPhone.platform_family == L"iOS");
    DUWN_ASSERT(resPhone.match_type == ModelDbMatch::Fallback);
    DUWN_ASSERT(resPhone.raw_product_type == L"iPhone99,1");

    // Unlisted future iPad
    auto resPad = AppleModelDatabase::Resolve(L"iPad99,1");
    DUWN_ASSERT(resPad.marketing_name == L"iPad");
    DUWN_ASSERT(resPad.platform_family == L"iPadOS");
    DUWN_ASSERT(resPad.match_type == ModelDbMatch::Fallback);

    // iPod fallback
    auto resPod = AppleModelDatabase::Resolve(L"iPod9,1");
    DUWN_ASSERT(resPod.marketing_name == L"iPod");
    DUWN_ASSERT(resPod.platform_family == L"iOS");
    DUWN_ASSERT(resPod.match_type == ModelDbMatch::Fallback);

    // Apple TV fallback
    auto resTV = AppleModelDatabase::Resolve(L"AppleTV14,1");
    DUWN_ASSERT(resTV.marketing_name == L"Apple TV");
    DUWN_ASSERT(resTV.platform_family == L"tvOS");
    DUWN_ASSERT(resTV.match_type == ModelDbMatch::Fallback);

    // Mac fallback
    auto resMac = AppleModelDatabase::Resolve(L"Mac15,12");
    DUWN_ASSERT(resMac.marketing_name == L"Mac");
    DUWN_ASSERT(resMac.platform_family == L"macOS");
    DUWN_ASSERT(resMac.match_type == ModelDbMatch::Fallback);

    // Completely unknown identifier
    auto resUnknown = AppleModelDatabase::Resolve(L"Unknown99,1");
    DUWN_ASSERT(resUnknown.marketing_name == L"Apple Device");
    DUWN_ASSERT(resUnknown.platform_family == L"iOS");
    DUWN_ASSERT(resUnknown.match_type == ModelDbMatch::Unknown);

    // Empty model string
    auto resEmpty = AppleModelDatabase::Resolve(L"");
    DUWN_ASSERT(resEmpty.marketing_name == L"Apple Device");
    DUWN_ASSERT(resEmpty.platform_family == L"iOS");
    DUWN_ASSERT(resEmpty.match_type == ModelDbMatch::Unknown);
}

// ---------------------------------------------------------------------------
// 5. Case sensitivity tests
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_CaseSensitivity) {
    // Exact-case matching required: lowercase "iphone" is not exact
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"iphone17,1"));
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"IPHONE17,1"));
    DUWN_ASSERT(!AppleModelDatabase::IsExactMatch(L"ipad16,3"));

    // Lookup of lowercase identifier falls back to Unknown because prefix check is also case-sensitive
    auto res = AppleModelDatabase::Resolve(L"iphone17,1");
    DUWN_ASSERT(res.match_type == ModelDbMatch::Unknown);
    DUWN_ASSERT(res.marketing_name == L"Apple Device");
}

// ---------------------------------------------------------------------------
// 6. Metadata and counts
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_MetadataAndCounts) {
    DUWN_ASSERT(AppleModelDatabase::kRevision == L"2026.09");
    DUWN_ASSERT(AppleModelDatabase::kUpdatedDate == L"2026-09-20");

    size_t total = AppleModelDatabase::EntryCount();
    size_t iphones = AppleModelDatabase::IPhoneCount();
    size_t ipads = AppleModelDatabase::IPadCount();

    DUWN_ASSERT(total > 50);
    DUWN_ASSERT(iphones > 30);
    DUWN_ASSERT(ipads > 25);
    DUWN_ASSERT(total == iphones + ipads);
}
