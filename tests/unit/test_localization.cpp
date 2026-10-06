// test_localization.cpp — Unit tests for Loc.h/Loc.cpp string table,
// runtime language switching, and Settings language field contract.

#include "ui/Loc.h"
#include "app/Settings.h"
#include <string>

using namespace duwn::ui::loc;

// ---------------------------------------------------------------------------
// 1. All S enum entries return non-null, non-empty strings in EN
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_AllEntriesNonEmpty_En) {
    SetLang(Lang::En);

    for (int i = 0; i < static_cast<int>(S::_COUNT); ++i) {
        const wchar_t* s = Get(static_cast<S>(i));
        DUWN_ASSERT(s != nullptr);
        DUWN_ASSERT(s[0] != L'\0');
    }
}

// ---------------------------------------------------------------------------
// 2. All S enum entries return non-null, non-empty strings in VI
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_AllEntriesNonEmpty_Vi) {
    SetLang(Lang::Vi);

    for (int i = 0; i < static_cast<int>(S::_COUNT); ++i) {
        const wchar_t* s = Get(static_cast<S>(i));
        DUWN_ASSERT(s != nullptr);
        DUWN_ASSERT(s[0] != L'\0');
    }

    SetLang(Lang::En); // restore
}

// ---------------------------------------------------------------------------
// 3. Runtime switch changes strings immediately
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_RuntimeSwitch) {
    SetLang(Lang::En);
    const wchar_t* en_mirror   = Get(S::Nav_Mirror);
    const wchar_t* en_settings = Get(S::Nav_Settings);

    SetLang(Lang::Vi);
    const wchar_t* vi_mirror   = Get(S::Nav_Mirror);
    const wchar_t* vi_settings = Get(S::Nav_Settings);

    // EN and VI arrays differ → pointers differ.
    DUWN_ASSERT(en_mirror   != vi_mirror);
    DUWN_ASSERT(en_settings != vi_settings);

    // Both non-empty in both languages.
    DUWN_ASSERT(en_mirror[0]   != L'\0');
    DUWN_ASSERT(en_settings[0] != L'\0');
    DUWN_ASSERT(vi_mirror[0]   != L'\0');
    DUWN_ASSERT(vi_settings[0] != L'\0');

    // Switching back to EN returns same pointer (same static array entry).
    SetLang(Lang::En);
    DUWN_ASSERT(Get(S::Nav_Mirror)   == en_mirror);
    DUWN_ASSERT(Get(S::Nav_Settings) == en_settings);
}

// ---------------------------------------------------------------------------
// 4. CurrentLang() tracks SetLang() calls
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_CurrentLang) {
    SetLang(Lang::En);
    DUWN_ASSERT(CurrentLang() == Lang::En);

    SetLang(Lang::Vi);
    DUWN_ASSERT(CurrentLang() == Lang::Vi);

    SetLang(Lang::En);
    DUWN_ASSERT(CurrentLang() == Lang::En);
}

// ---------------------------------------------------------------------------
// 5. Lang::System resolves to En or Vi — never stays unresolved
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_SystemResolves) {
    SetLang(Lang::System);
    Lang resolved = CurrentLang();

    // After SetLang(System), CurrentLang() must be En or Vi, never System.
    DUWN_ASSERT(resolved == Lang::En || resolved == Lang::Vi);

    // Strings must be valid after System resolution.
    const wchar_t* s = Get(S::Nav_Mirror);
    DUWN_ASSERT(s != nullptr && s[0] != L'\0');

    SetLang(Lang::En); // restore to deterministic state
}

// ---------------------------------------------------------------------------
// 6. Settings language field defaults and assignment (no Save/Load needed)
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_SettingsPersistence) {
    // Verify language field existence and default on a fresh Settings struct.
    duwn::app::Settings s{};
    DUWN_ASSERT(s.language == L"auto");

    // Assignment of valid values.
    s.language = L"en-US";
    DUWN_ASSERT(s.language == L"en-US");

    s.language = L"vi-VN";
    DUWN_ASSERT(s.language == L"vi-VN");

    s.language = L"auto";
    DUWN_ASSERT(s.language == L"auto");
}

// ---------------------------------------------------------------------------
// 7. Full Vietnamese Localization Audit Across All 9 Views / Sub-Tabs
// ---------------------------------------------------------------------------
DUWN_TEST(Localization_VietnameseNineViewsAudit) {
    SetLang(Lang::Vi);

    // View 1: Mirror View
    DUWN_ASSERT(std::wstring_view(Get(S::Nav_Mirror)) == L"Màn hình");
    DUWN_ASSERT(std::wstring_view(Get(S::Mirror_NoDeviceConnected)) == L"Chưa kết nối thiết bị");

    // View 2: Performance View
    DUWN_ASSERT(std::wstring_view(Get(S::Nav_Performance)) == L"Hiệu suất");
    DUWN_ASSERT(std::wstring_view(Get(S::Perf_Lbl_DecodedPresented)) == L"Đã giải mã / Đã hiển thị");

    // View 3: Settings - General Sub-tab
    DUWN_ASSERT(std::wstring_view(Get(S::Settings_General)) == L"Chung");
    DUWN_ASSERT(std::wstring_view(Get(S::General_Language)) == L"Ngôn ngữ");
    DUWN_ASSERT(std::wstring_view(Get(S::General_StartWithWindows)) == L"Khởi động cùng Windows");
    DUWN_ASSERT(std::wstring_view(Get(S::General_AllowPublicNetworks)) == L"Cho phép AirPlay trên mạng công cộng");
    DUWN_ASSERT(std::wstring_view(Get(S::Network_PublicWarning)) == L"Mạng hiện tại đang là mạng Công cộng. Windows Firewall có thể chặn kết nối AirPlay.");

    // View 4: Settings - Output Sub-tab
    DUWN_ASSERT(std::wstring_view(Get(S::Settings_Output)) == L"Đầu ra");
    DUWN_ASSERT(std::wstring_view(Get(S::Output_Resolution)) == L"Độ phân giải");
    DUWN_ASSERT(std::wstring_view(Get(S::Output_AspectMode)) == L"Chế độ tỷ lệ");

    // View 5: Settings - Audio Sub-tab
    DUWN_ASSERT(std::wstring_view(Get(S::Settings_Audio)) == L"Âm thanh");
    DUWN_ASSERT(std::wstring_view(Get(S::Audio_OutputDevice)) == L"Thiết bị đầu ra");
    DUWN_ASSERT(std::wstring_view(Get(S::Audio_PlayTestTone)) == L"Phát âm thanh kiểm tra");

    // View 6: Settings - Privacy & Legal Sub-tab
    DUWN_ASSERT(std::wstring_view(Get(S::Settings_Privacy)) == L"Riêng tư & Pháp lý");
    DUWN_ASSERT(std::wstring_view(Get(S::Privacy_NoTelemetry)) == L"Không thu thập dữ liệu đo từ xa hoặc phân tích.");

    // View 7: Settings - Advanced Sub-tab
    DUWN_ASSERT(std::wstring_view(Get(S::Settings_Advanced)) == L"Nâng cao");
    DUWN_ASSERT(std::wstring_view(Get(S::Adv_DebugLog)) == L"Ghi nhật ký gỡ lỗi");

    // View 8: About View
    DUWN_ASSERT(std::wstring_view(Get(S::Nav_About)) == L"Giới thiệu");
    DUWN_ASSERT(std::wstring_view(Get(S::About_VersionTitle)) == L"Phiên bản 1.1.2 (Xem trước kết nối hai chế độ)");

    // View 9: Telemetry / Status Overlay
    DUWN_ASSERT(std::wstring_view(Get(S::Status_Streaming)) == L"Đang phát luồng");
    DUWN_ASSERT(std::wstring_view(Get(S::Status_Connected)) == L"Đã kết nối");
    DUWN_ASSERT(std::wstring_view(Get(S::Status_Ready)) == L"Sẵn sàng kết nối");

    SetLang(Lang::En); // restore
}
