// test_theme.cpp — Unit tests for Phase 5 Theme engine, color palettes, and settings persistence.

#include "ui/Theme.h"
#include "app/Settings.h"
#include "ui/UiState.h"
#include <cmath>

namespace {

// Compute relative luminance according to WCAG 2.1
// https://www.w3.org/WAI/GL/wiki/Relative_luminance
double ChannelLuminance(float c) noexcept {
    return (c <= 0.03928f) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

double RelativeLuminance(const D2D1_COLOR_F& color) noexcept {
    double r = ChannelLuminance(color.r);
    double g = ChannelLuminance(color.g);
    double b = ChannelLuminance(color.b);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double ContrastRatio(const D2D1_COLOR_F& c1, const D2D1_COLOR_F& c2) noexcept {
    double l1 = RelativeLuminance(c1);
    double l2 = RelativeLuminance(c2);
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05) / (l2 + 0.05);
}

} // namespace

DUWN_TEST(Theme_PalettesAreValid) {
    using namespace duwn::ui;
    const ColorPalette& dark = GetDarkPalette();
    const ColorPalette& light = GetLightPalette();

    // Verify alpha is valid for core backgrounds and surfaces
    DUWN_ASSERT(dark.bg_base.a == 1.0f);
    DUWN_ASSERT(dark.bg_surface.a >= 0.9f);
    DUWN_ASSERT(light.bg_base.a == 1.0f);
    DUWN_ASSERT(light.bg_surface.a >= 0.9f);

    // Verify dark palette has dark background (< 0.2 luminance)
    DUWN_ASSERT(RelativeLuminance(dark.bg_base) < 0.2);

    // Verify light palette has bright background (> 0.8 luminance)
    DUWN_ASSERT(RelativeLuminance(light.bg_base) > 0.8);
}

DUWN_TEST(Theme_ResolveMode) {
    using namespace duwn::ui;
    const ColorPalette& dark_resolved = ResolvePalette(ThemeMode::Dark);
    DUWN_ASSERT(dark_resolved.bg_base.r == GetDarkPalette().bg_base.r);

    const ColorPalette& light_resolved = ResolvePalette(ThemeMode::Light);
    DUWN_ASSERT(light_resolved.bg_base.r == GetLightPalette().bg_base.r);

    // System mode should resolve to either dark or light without crashing
    const ColorPalette& sys_resolved = ResolvePalette(ThemeMode::System);
    bool matches_dark = (sys_resolved.bg_base.r == GetDarkPalette().bg_base.r);
    bool matches_light = (sys_resolved.bg_base.r == GetLightPalette().bg_base.r);
    DUWN_ASSERT(matches_dark || matches_light);
}

DUWN_TEST(Theme_ContrastRatioWcagAA) {
    using namespace duwn::ui;
    const ColorPalette& dark = GetDarkPalette();
    const ColorPalette& light = GetLightPalette();

    // WCAG AA requirement for normal text is >= 4.5:1
    double dark_ratio = ContrastRatio(dark.text_primary, dark.bg_base);
    DUWN_ASSERT(dark_ratio >= 4.5);

    double dark_surface_ratio = ContrastRatio(dark.text_primary, dark.bg_surface);
    DUWN_ASSERT(dark_surface_ratio >= 4.5);

    double light_ratio = ContrastRatio(light.text_primary, light.bg_base);
    DUWN_ASSERT(light_ratio >= 4.5);

    double light_surface_ratio = ContrastRatio(light.text_primary, light.bg_surface);
    DUWN_ASSERT(light_surface_ratio >= 4.5);
}

DUWN_TEST(Theme_SettingsPersistenceRoundTrip) {
    using namespace duwn::app;
    using namespace duwn::ui;

    Settings s{};
    s.theme_mode = ThemeMode::Light;
    s.accent_color = 0x2563EB;
    s.ui_scale = 1.25f;
    s.animations_enabled = false;

    // Validate settings
    DUWN_ASSERT(Settings::ValidateSettings(s));

    // Save and load
    s.Save();
    Settings loaded = Settings::Load();

    DUWN_ASSERT(loaded.theme_mode == ThemeMode::Light);
    DUWN_ASSERT(loaded.accent_color == 0x2563EB);
    DUWN_ASSERT(std::abs(loaded.ui_scale - 1.25f) < 0.01f);
    DUWN_ASSERT(loaded.animations_enabled == false);

    // Restore defaults for subsequent tests
    s.theme_mode = ThemeMode::System;
    s.accent_color = 0x3B82F6;
    s.ui_scale = 1.0f;
    s.animations_enabled = true;
    s.Save();
}
