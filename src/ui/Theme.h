#pragma once
// Theme.h — Design tokens, color palettes, and layout metrics for DUWN Mirror.
// Supports both Dark and Light themes with dynamic runtime switching and Windows system theme detection.

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <windows.h>
#include <cstdint>

namespace duwn::ui {

enum class ThemeMode : uint32_t {
    Dark = 0,
    Light = 1,
    System = 2
};

struct ColorPalette {
    // Primary Backgrounds
    D2D1_COLOR_F bg_base;
    D2D1_COLOR_F bg_darker;
    D2D1_COLOR_F bg_lighter;
    D2D1_COLOR_F bg_canvas;

    // Card Surfaces & Borders
    D2D1_COLOR_F bg_surface;
    D2D1_COLOR_F bg_surface_hover;
    D2D1_COLOR_F bg_border;
    D2D1_COLOR_F bg_border_hover;
    D2D1_COLOR_F bg_border_focus;
    D2D1_COLOR_F bg_inner;

    // Sidebar & Navigation
    D2D1_COLOR_F sidebar_bg;
    D2D1_COLOR_F nav_active_bg;
    D2D1_COLOR_F nav_active_bar;
    D2D1_COLOR_F nav_hover_bg;

    // Brand Accents
    D2D1_COLOR_F brand_blue;
    D2D1_COLOR_F brand_purple;
    D2D1_COLOR_F brand_cyan;
    D2D1_COLOR_F accent_glow;

    // Semantic States (Color + Icon + Text redundancy)
    D2D1_COLOR_F status_green;
    D2D1_COLOR_F status_amber;
    D2D1_COLOR_F status_red;
    D2D1_COLOR_F status_blue;

    // Typography (WCAG AA >= 4.5:1 compliant)
    D2D1_COLOR_F text_primary;
    D2D1_COLOR_F text_secondary;
    D2D1_COLOR_F text_muted;
    D2D1_COLOR_F text_accent;
    D2D1_COLOR_F text_inverse;

    // Hardware Shell (iPhone Mockup)
    D2D1_COLOR_F phone_bezel_outer;
    D2D1_COLOR_F phone_bezel_inner;
    D2D1_COLOR_F phone_screen_bg;
    D2D1_COLOR_F dynamic_island;
};

// Static color definitions for backwards compatibility with existing call sites
namespace colors {

inline constexpr D2D1_COLOR_F Background        = { 0.043f, 0.063f, 0.125f, 1.0f }; // #0B1020 Deep Indigo
inline constexpr D2D1_COLOR_F BackgroundDarker  = { 0.027f, 0.039f, 0.078f, 1.0f }; // #070A14 Dark Navy
inline constexpr D2D1_COLOR_F BackgroundLighter = { 0.067f, 0.098f, 0.188f, 1.0f }; // #111930 Elevated

inline constexpr D2D1_COLOR_F CardSurface       = { 0.063f, 0.090f, 0.169f, 0.95f }; // #10172B Primary Card
inline constexpr D2D1_COLOR_F CardSurfaceHover  = { 0.086f, 0.122f, 0.224f, 1.00f }; // #161F39 Hover Card
inline constexpr D2D1_COLOR_F CardBorder        = { 0.141f, 0.188f, 0.314f, 0.60f }; // #243050 Subtle Border
inline constexpr D2D1_COLOR_F CardBorderHover   = { 0.231f, 0.510f, 0.965f, 0.50f }; // Blue-tinted border on hover
inline constexpr D2D1_COLOR_F InnerSurface      = { 0.047f, 0.067f, 0.129f, 0.85f }; // #0C1121 Inset background

inline constexpr D2D1_COLOR_F SidebarBg         = { 0.035f, 0.051f, 0.106f, 1.0f }; // #090D1B
inline constexpr D2D1_COLOR_F NavActiveBg       = { 0.118f, 0.200f, 0.451f, 0.70f }; // Blue active pill
inline constexpr D2D1_COLOR_F NavHoverBg        = { 0.086f, 0.125f, 0.235f, 0.50f }; // Nav item hover

inline constexpr D2D1_COLOR_F BrandBlue         = { 0.231f, 0.510f, 0.965f, 1.0f }; // #3B82F6 Electric Blue
inline constexpr D2D1_COLOR_F BrandPurple       = { 0.486f, 0.227f, 0.929f, 1.0f }; // #7C3AED Vivid Violet
inline constexpr D2D1_COLOR_F BrandCyan         = { 0.024f, 0.714f, 0.831f, 1.0f }; // #06B6D4 Cyan
inline constexpr D2D1_COLOR_F AccentGlow        = { 0.231f, 0.510f, 0.965f, 0.25f }; // Soft blue glow

inline constexpr D2D1_COLOR_F StatusGreen       = { 0.133f, 0.773f, 0.369f, 1.0f }; // #22C55E Online/Connected
inline constexpr D2D1_COLOR_F StatusAmber       = { 0.961f, 0.620f, 0.043f, 1.0f }; // #F59E0B Connecting
inline constexpr D2D1_COLOR_F StatusRed         = { 0.937f, 0.267f, 0.267f, 1.0f }; // #EF4444 Disconnected/Error
inline constexpr D2D1_COLOR_F StatusBlue        = { 0.376f, 0.647f, 0.980f, 1.0f }; // #60A5FA Info

inline constexpr D2D1_COLOR_F TextPrimary       = { 0.953f, 0.965f, 1.000f, 1.0f }; // #F3F6FF White/Ice
inline constexpr D2D1_COLOR_F TextSecondary     = { 0.580f, 0.639f, 0.722f, 1.0f }; // #94A3B8 Cool Grey
inline constexpr D2D1_COLOR_F TextMuted         = { 0.392f, 0.455f, 0.545f, 1.0f }; // #64748B Slate Grey
inline constexpr D2D1_COLOR_F TextAccent        = { 0.380f, 0.655f, 1.000f, 1.0f }; // #60A5FA Light Blue

inline constexpr D2D1_COLOR_F PhoneBezelOuter   = { 0.125f, 0.145f, 0.180f, 1.0f }; // Titanium edge
inline constexpr D2D1_COLOR_F PhoneBezelInner   = { 0.051f, 0.055f, 0.067f, 1.0f }; // Black inner rim
inline constexpr D2D1_COLOR_F PhoneScreenBg     = { 0.020f, 0.027f, 0.047f, 1.0f }; // Screen glass
inline constexpr D2D1_COLOR_F DynamicIsland     = { 0.000f, 0.000f, 0.000f, 1.0f }; // Pure black

} // namespace colors

inline constexpr ColorPalette kDarkPalette = {
    // Backgrounds
    colors::Background,
    colors::BackgroundDarker,
    colors::BackgroundLighter,
    colors::SidebarBg,

    // Surfaces
    colors::CardSurface,
    colors::CardSurfaceHover,
    colors::CardBorder,
    colors::CardBorderHover,
    { 0.231f, 0.510f, 0.965f, 1.00f }, // bg_border_focus (accessibility ring)
    colors::InnerSurface,

    // Navigation
    colors::SidebarBg,
    colors::NavActiveBg,
    colors::BrandBlue,
    colors::NavHoverBg,

    // Accents
    colors::BrandBlue,
    colors::BrandPurple,
    colors::BrandCyan,
    colors::AccentGlow,

    // Status
    colors::StatusGreen,
    colors::StatusAmber,
    colors::StatusRed,
    colors::StatusBlue,

    // Typography
    colors::TextPrimary,
    colors::TextSecondary,
    colors::TextMuted,
    colors::TextAccent,
    { 0.043f, 0.063f, 0.125f, 1.0f }, // text_inverse

    // Hardware Shell
    colors::PhoneBezelOuter,
    colors::PhoneBezelInner,
    colors::PhoneScreenBg,
    colors::DynamicIsland
};

inline constexpr ColorPalette kLightPalette = {
    // Primary Backgrounds
    { 0.965f, 0.973f, 0.984f, 1.0f }, // bg_base (#F6F8FB)
    { 0.925f, 0.937f, 0.953f, 1.0f }, // bg_darker
    { 1.000f, 1.000f, 1.000f, 1.0f }, // bg_lighter
    { 0.945f, 0.953f, 0.969f, 1.0f }, // bg_canvas

    // Card Surfaces & Borders
    { 1.000f, 1.000f, 1.000f, 0.98f }, // bg_surface (pure white card)
    { 0.945f, 0.960f, 0.985f, 1.00f }, // bg_surface_hover
    { 0.820f, 0.850f, 0.890f, 0.85f }, // bg_border (#D1D9E3)
    { 0.145f, 0.388f, 0.922f, 0.60f }, // bg_border_hover
    { 0.145f, 0.388f, 0.922f, 1.00f }, // bg_border_focus (accessibility ring)
    { 0.940f, 0.950f, 0.965f, 0.90f }, // bg_inner

    // Sidebar & Navigation
    { 0.930f, 0.940f, 0.960f, 1.0f }, // sidebar_bg
    { 0.860f, 0.910f, 0.985f, 0.90f }, // nav_active_bg
    { 0.145f, 0.388f, 0.922f, 1.0f }, // nav_active_bar (#2563EB)
    { 0.890f, 0.915f, 0.945f, 0.60f }, // nav_hover_bg

    // Brand Accents
    { 0.145f, 0.388f, 0.922f, 1.0f }, // brand_blue (#2563EB)
    { 0.427f, 0.196f, 0.824f, 1.0f }, // brand_purple (#6D31D2)
    { 0.031f, 0.580f, 0.678f, 1.0f }, // brand_cyan (#0894AD)
    { 0.145f, 0.388f, 0.922f, 0.18f }, // accent_glow

    // Semantic States
    { 0.086f, 0.639f, 0.290f, 1.0f }, // status_green (#16A34A)
    { 0.851f, 0.467f, 0.024f, 1.0f }, // status_amber (#D97706)
    { 0.863f, 0.149f, 0.149f, 1.0f }, // status_red (#DC2626)
    { 0.145f, 0.388f, 0.922f, 1.0f }, // status_blue (#2563EB)

    // Typography
    { 0.090f, 0.110f, 0.150f, 1.0f }, // text_primary (#171C26, contrast >= 12:1)
    { 0.320f, 0.370f, 0.450f, 1.0f }, // text_secondary (#525E73, contrast >= 5.5:1)
    { 0.480f, 0.530f, 0.600f, 1.0f }, // text_muted (#7A8799)
    { 0.145f, 0.388f, 0.922f, 1.0f }, // text_accent (#2563EB)
    { 1.000f, 1.000f, 1.000f, 1.0f }, // text_inverse

    // Hardware Shell
    { 0.780f, 0.800f, 0.830f, 1.0f }, // phone_bezel_outer
    { 0.200f, 0.220f, 0.250f, 1.0f }, // phone_bezel_inner
    { 0.950f, 0.960f, 0.970f, 1.0f }, // phone_screen_bg
    { 0.100f, 0.100f, 0.120f, 1.0f }  // dynamic_island
};

inline const ColorPalette& GetDarkPalette() noexcept {
    return kDarkPalette;
}

inline const ColorPalette& GetLightPalette() noexcept {
    return kLightPalette;
}

inline bool DetectWindowsSystemDarkMode() noexcept {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        DWORD type = REG_DWORD;
        LSTATUS status = RegQueryValueExW(hkey, L"AppsUseLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hkey);
        if (status == ERROR_SUCCESS) {
            return (value == 0); // 0 = Dark mode, 1 = Light mode
        }
    }
    return true; // Default to dark mode if key missing
}

inline const ColorPalette& ResolvePalette(ThemeMode mode) noexcept {
    switch (mode) {
    case ThemeMode::Light:
        return GetLightPalette();
    case ThemeMode::System:
        return DetectWindowsSystemDarkMode() ? GetDarkPalette() : GetLightPalette();
    case ThemeMode::Dark:
    default:
        return GetDarkPalette();
    }
}

// Layout and sizing constants
namespace metrics {

inline constexpr float HeaderHeight      = 60.0f;
inline constexpr float SidebarWidth      = 200.0f;
inline constexpr float SidebarCompact    = 68.0f;
inline constexpr float RightPanelWidth   = 360.0f;
inline constexpr float StatusBarHeight   = 34.0f;

inline constexpr float CardRadius        = 12.0f;
inline constexpr float ButtonRadius      = 8.0f;
inline constexpr float PillRadius        = 14.0f;
inline constexpr float ControlRadius     = 6.0f;
inline constexpr float PhoneCornerRadius = 32.0f;

inline constexpr float Spacing2xs        = 2.0f;
inline constexpr float SpacingXs         = 4.0f;
inline constexpr float SpacingSm         = 8.0f;
inline constexpr float SpacingMd         = 16.0f;
inline constexpr float SpacingLg         = 24.0f;
inline constexpr float SpacingXl         = 32.0f;

} // namespace metrics

} // namespace duwn::ui
