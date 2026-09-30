#include "ui/Loc.h"
#include <string_view>

DUWN_TEST(dual_mode_strings_available_in_english_and_vietnamese) {
    using duwn::ui::loc::Lang;
    using duwn::ui::loc::S;
    using duwn::ui::loc::Get;
    using duwn::ui::loc::SetLang;
    const S strings[] = {S::Mode_Title, S::Mode_Wireless, S::Mode_Wired,
        S::Wireless_Description, S::Wired_Description, S::Wired_NoCable,
        S::Wired_DriverMissing, S::Wired_VideoBridgeUnavailable,
        S::Wired_TrustUnknown, S::Wired_Start, S::Wired_Troubleshoot,
        S::Mirror_Empty_Wireless_Title, S::Mirror_Empty_Wireless_Desc,
        S::Mirror_Empty_Wired_NoCable, S::Mirror_Empty_Wired_Preparing, S::Mirror_Empty_Wired_Ready,
        S::Mirror_Waiting_Video, S::Status_Reconnecting, S::Settings_Network,
        S::Opt_Rec_Original60};
    for (const S id : strings) {
        SetLang(Lang::En);
        const std::wstring_view english = Get(id);
        SetLang(Lang::Vi);
        const std::wstring_view vietnamese = Get(id);
        DUWN_ASSERT(!english.empty() && english != L"???");
        DUWN_ASSERT(!vietnamese.empty() && vietnamese != L"???");
        DUWN_ASSERT(english != vietnamese);
    }
    SetLang(Lang::System);
}

DUWN_TEST(quality_terminology_user_facing_labels) {
    using duwn::ui::loc::Lang;
    using duwn::ui::loc::S;
    using duwn::ui::loc::Get;
    using duwn::ui::loc::SetLang;

    // Verify English presets
    SetLang(Lang::En);
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_Auto)) == L"Auto");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_HD)) == L"HD");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_FullHD)) == L"Full HD");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_2K)) == L"2K");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_4K)) == L"4K");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_Original)) == L"Original");

    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_Auto)) == L"Auto");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_720p30)) == L"HD · 30 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_720p60)) == L"HD · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1080p30)) == L"Full HD · 30 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1080p60)) == L"Full HD · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1440p60)) == L"2K · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_Original60)) == L"Original · 60 FPS");

    // Verify Vietnamese presets
    SetLang(Lang::Vi);
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_Auto)) == L"Tự động");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_HD)) == L"HD");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_FullHD)) == L"Full HD");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_2K)) == L"2K");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_4K)) == L"4K");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Out_Original)) == L"Gốc");

    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_Auto)) == L"Tự động");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_720p30)) == L"HD · 30 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_720p60)) == L"HD · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1080p30)) == L"Full HD · 30 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1080p60)) == L"Full HD · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_1440p60)) == L"2K · 60 FPS");
    DUWN_ASSERT(std::wstring_view(Get(S::Opt_Rec_Original60)) == L"Gốc · 60 FPS");

    SetLang(Lang::System);
}
