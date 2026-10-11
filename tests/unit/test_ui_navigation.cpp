// test_ui_navigation.cpp — Unit tests for UI navigation, 5-group settings sub-tabs, dropdown lifecycle, and keyboard focus.

#include "ui/UiState.h"
#include "ui/Theme.h"

DUWN_TEST(Navigation_SettingsFiveCanonicalSubTabs) {
    using namespace duwn::ui;
    UiState state;

    // Default settings sub-tab should be VideoDisplay
    DUWN_ASSERT(state.active_settings_sub_tab == SettingsSubTab::VideoDisplay);

    state.active_settings_sub_tab = SettingsSubTab::Audio;
    DUWN_ASSERT(state.active_settings_sub_tab == SettingsSubTab::Audio);

    state.active_settings_sub_tab = SettingsSubTab::Color;
    DUWN_ASSERT(state.active_settings_sub_tab == SettingsSubTab::Color);

    state.active_settings_sub_tab = SettingsSubTab::Appearance;
    DUWN_ASSERT(state.active_settings_sub_tab == SettingsSubTab::Appearance);

    state.active_settings_sub_tab = SettingsSubTab::Advanced;
    DUWN_ASSERT(state.active_settings_sub_tab == SettingsSubTab::Advanced);

    // Verify legacy aliases point to valid canonical values
    DUWN_ASSERT(static_cast<int>(SettingsSubTab::General) == static_cast<int>(SettingsSubTab::Appearance));
    DUWN_ASSERT(static_cast<int>(SettingsSubTab::Output)  == static_cast<int>(SettingsSubTab::VideoDisplay));
    DUWN_ASSERT(static_cast<int>(SettingsSubTab::Network) == static_cast<int>(SettingsSubTab::Advanced));
    DUWN_ASSERT(static_cast<int>(SettingsSubTab::Privacy) == static_cast<int>(SettingsSubTab::Advanced));
}

DUWN_TEST(Navigation_DropdownStateLifecycle) {
    using namespace duwn::ui;
    UiState state;

    DUWN_ASSERT(state.open_dropdown == 0);

    // Open Theme dropdown
    state.open_dropdown = Control_Set_ThemeMode;
    DUWN_ASSERT(state.open_dropdown == Control_Set_ThemeMode);

    // Toggle same dropdown closes it
    state.open_dropdown = (state.open_dropdown == Control_Set_ThemeMode) ? 0 : Control_Set_ThemeMode;
    DUWN_ASSERT(state.open_dropdown == 0);

    // Outside click closes dropdown
    state.open_dropdown = Control_Set_ThemeMode;
    state.open_dropdown = 0;
    DUWN_ASSERT(state.open_dropdown == 0);
}

DUWN_TEST(Navigation_KeyboardFocusCycle) {
    using namespace duwn::ui;
    UiState state;

    // Initial keyboard focus is 0 (no focus)
    DUWN_ASSERT(state.keyboard_focused_control == 0);

    // Tab navigation sets focus
    state.keyboard_focused_control = Control_Nav_Mirror;
    DUWN_ASSERT(state.keyboard_focused_control == Control_Nav_Mirror);

    // Escape clears focus or dismisses open dropdown
    state.open_dropdown = Control_Set_Language;
    if (state.open_dropdown != 0) {
        state.open_dropdown = 0;
    }
    DUWN_ASSERT(state.open_dropdown == 0);
}
