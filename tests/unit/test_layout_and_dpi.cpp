// test_layout_and_dpi.cpp — Unit tests for responsive layout tiers, DPI scaling metrics, and breakpoints.

#include "ui/Theme.h"
#include "ui/MainWindowView.h"
#include <cmath>

DUWN_TEST(Layout_ResponsiveTiersBreakpoints) {
    using namespace duwn::ui;

    // Large tier: >= 1180 DIP
    DUWN_ASSERT(MainWindowView::CalculateTier(1920.0f) == LayoutTier::Large);
    DUWN_ASSERT(MainWindowView::CalculateTier(1280.0f) == LayoutTier::Large);
    DUWN_ASSERT(MainWindowView::CalculateTier(1180.0f) == LayoutTier::Large);

    // Medium tier: 900 <= width < 1180 DIP
    DUWN_ASSERT(MainWindowView::CalculateTier(1179.9f) == LayoutTier::Medium);
    DUWN_ASSERT(MainWindowView::CalculateTier(1050.0f) == LayoutTier::Medium);
    DUWN_ASSERT(MainWindowView::CalculateTier(900.0f)  == LayoutTier::Medium);

    // Small tier: < 900 DIP
    DUWN_ASSERT(MainWindowView::CalculateTier(899.9f) == LayoutTier::Small);
    DUWN_ASSERT(MainWindowView::CalculateTier(800.0f) == LayoutTier::Small);
    DUWN_ASSERT(MainWindowView::CalculateTier(640.0f) == LayoutTier::Small);
}

DUWN_TEST(Layout_MetricsConstants) {
    using namespace duwn::ui::metrics;

    DUWN_ASSERT(SidebarWidth == 200.0f);
    DUWN_ASSERT(SidebarCompact == 68.0f);
    DUWN_ASSERT(HeaderHeight == 60.0f);
    DUWN_ASSERT(StatusBarHeight == 34.0f);

    DUWN_ASSERT(CardRadius == 12.0f);
    DUWN_ASSERT(ButtonRadius == 8.0f);
    DUWN_ASSERT(PillRadius == 14.0f);
    DUWN_ASSERT(ControlRadius == 6.0f);
}

DUWN_TEST(Layout_DpiCoordinateScaling) {
    auto DipToPx = [](float dip, float dpi) -> int {
        return static_cast<int>(std::round(dip * (dpi / 96.0f)));
    };

    // At 96 DPI (100% scaling): 1 DIP == 1 pixel
    DUWN_ASSERT(DipToPx(200.0f, 96.0f) == 200);
    DUWN_ASSERT(DipToPx(44.0f, 96.0f)  == 44);

    // At 120 DPI (125% scaling)
    DUWN_ASSERT(DipToPx(200.0f, 120.0f) == 250);

    // At 144 DPI (150% scaling)
    DUWN_ASSERT(DipToPx(200.0f, 144.0f) == 300);

    // At 192 DPI (200% scaling)
    DUWN_ASSERT(DipToPx(200.0f, 192.0f) == 400);
    DUWN_ASSERT(DipToPx(32.0f, 192.0f)  == 64);
}
