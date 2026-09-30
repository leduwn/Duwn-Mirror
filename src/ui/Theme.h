#pragma once
// Theme.h — Design tokens, color palette, and layout metrics for DUWN Mirror.
// Conforms to Image A (modern dark-mode desktop stream receiver) & Image B (branding).

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>

namespace duwn::ui {

// Color definitions (Hex to D2D1_COLOR_F)
namespace colors {

// Primary Backgrounds
inline constexpr D2D1_COLOR_F Background        = { 0.043f, 0.063f, 0.125f, 1.0f }; // #0B1020 Deep Indigo
inline constexpr D2D1_COLOR_F BackgroundDarker  = { 0.027f, 0.039f, 0.078f, 1.0f }; // #070A14 Dark Navy
inline constexpr D2D1_COLOR_F BackgroundLighter = { 0.067f, 0.098f, 0.188f, 1.0f }; // #111930 Elevated

// Card Surfaces
inline constexpr D2D1_COLOR_F CardSurface       = { 0.063f, 0.090f, 0.169f, 0.95f }; // #10172B Primary Card
inline constexpr D2D1_COLOR_F CardSurfaceHover  = { 0.086f, 0.122f, 0.224f, 1.00f }; // #161F39 Hover Card
inline constexpr D2D1_COLOR_F CardBorder        = { 0.141f, 0.188f, 0.314f, 0.60f }; // #243050 Subtle Border
inline constexpr D2D1_COLOR_F CardBorderHover   = { 0.231f, 0.510f, 0.965f, 0.50f }; // Blue-tinted border on hover
inline constexpr D2D1_COLOR_F InnerSurface      = { 0.047f, 0.067f, 0.129f, 0.85f }; // #0C1121 Inset background

// Sidebar & Rails
inline constexpr D2D1_COLOR_F SidebarBg         = { 0.035f, 0.051f, 0.106f, 1.0f }; // #090D1B
inline constexpr D2D1_COLOR_F NavActiveBg       = { 0.118f, 0.200f, 0.451f, 0.70f }; // Blue active pill
inline constexpr D2D1_COLOR_F NavHoverBg        = { 0.086f, 0.125f, 0.235f, 0.50f }; // Nav item hover

// Accents & Gradients
inline constexpr D2D1_COLOR_F BrandBlue         = { 0.231f, 0.510f, 0.965f, 1.0f }; // #3B82F6 Electric Blue
inline constexpr D2D1_COLOR_F BrandPurple       = { 0.486f, 0.227f, 0.929f, 1.0f }; // #7C3AED Vivid Violet
inline constexpr D2D1_COLOR_F BrandCyan         = { 0.024f, 0.714f, 0.831f, 1.0f }; // #06B6D4 Cyan
inline constexpr D2D1_COLOR_F AccentGlow        = { 0.231f, 0.510f, 0.965f, 0.25f }; // Soft blue glow

// Status Indicators
inline constexpr D2D1_COLOR_F StatusGreen       = { 0.133f, 0.773f, 0.369f, 1.0f }; // #22C55E Online/Connected
inline constexpr D2D1_COLOR_F StatusAmber       = { 0.961f, 0.620f, 0.043f, 1.0f }; // #F59E0B Connecting
inline constexpr D2D1_COLOR_F StatusRed         = { 0.937f, 0.267f, 0.267f, 1.0f }; // #EF4444 Disconnected/Error
inline constexpr D2D1_COLOR_F StatusBlue        = { 0.376f, 0.647f, 0.980f, 1.0f }; // #60A5FA Info

// Typography
inline constexpr D2D1_COLOR_F TextPrimary       = { 0.953f, 0.965f, 1.000f, 1.0f }; // #F3F6FF White/Ice
inline constexpr D2D1_COLOR_F TextSecondary     = { 0.580f, 0.639f, 0.722f, 1.0f }; // #94A3B8 Cool Grey
inline constexpr D2D1_COLOR_F TextMuted         = { 0.392f, 0.455f, 0.545f, 1.0f }; // #64748B Slate Grey
inline constexpr D2D1_COLOR_F TextAccent        = { 0.380f, 0.655f, 1.000f, 1.0f }; // #60A5FA Light Blue

// Device Frame (iPhone mockup)
inline constexpr D2D1_COLOR_F PhoneBezelOuter   = { 0.125f, 0.145f, 0.180f, 1.0f }; // Titanium edge
inline constexpr D2D1_COLOR_F PhoneBezelInner   = { 0.051f, 0.055f, 0.067f, 1.0f }; // Black inner rim
inline constexpr D2D1_COLOR_F PhoneScreenBg     = { 0.020f, 0.027f, 0.047f, 1.0f }; // Screen glass
inline constexpr D2D1_COLOR_F DynamicIsland     = { 0.000f, 0.000f, 0.000f, 1.0f }; // Pure black

} // namespace colors

// Layout and sizing constants
namespace metrics {

inline constexpr float HeaderHeight      = 60.0f;
inline constexpr float SidebarWidth      = 220.0f;
inline constexpr float RightPanelWidth   = 360.0f;
inline constexpr float StatusBarHeight   = 34.0f;

inline constexpr float CardRadius        = 12.0f;
inline constexpr float ButtonRadius      = 8.0f;
inline constexpr float PillRadius        = 14.0f;
inline constexpr float PhoneCornerRadius = 32.0f;

inline constexpr float SpacingXs         = 4.0f;
inline constexpr float SpacingSm         = 8.0f;
inline constexpr float SpacingMd         = 16.0f;
inline constexpr float SpacingLg         = 24.0f;

} // namespace metrics

} // namespace duwn::ui
