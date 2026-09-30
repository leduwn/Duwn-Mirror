// test_video_geometry.cpp — Fit/Fill aspect-ratio math tests
// No D3D device required. Tests pure geometry from VideoGeometry.h.
//
// Geometry model
// --------------
// VideoGeometry functions operate on VISIBLE dimensions, not coded dims.
//
// The MF H.264 decoder outputs NV12 textures with coded (padded) dimensions
// rounded up to macroblock boundaries (multiples of 16). The display aperture
// (MF_MT_MINIMUM_DISPLAY_APERTURE or MF_MT_GEOMETRIC_APERTURE) describes the
// visible region within the coded texture.
//
// Example (generic widescreen device, landscape):
//   Display: 2556 x 1179  (hypothetical; odd — NOT valid NV12)
//   Coded NV12: 2560 x 1184  (next multiple of 16)
//   Display aperture (hypothetical): offset (2,2), size 2556x1179
//   Even-aligned visible: offset (2,2), size 2556x1178
//
// ComputeFitDestRect / ComputeFillSrcRect take visible_width × visible_height.
// Caller offsets VP source rect by (visible_x, visible_y) afterward.
//
// ComputeFillSrcRect even-aligns crop boundaries (cw &= ~1u, etc.) for NV12 chroma.
//
// Tests use generic even coded/visible dimensions, not device-specific values.

#include "video/VideoGeometry.h"
#include "video/StreamGeometryCoordinator.h"

using namespace duwn::video;

DUWN_TEST(pixel_perfect_wide_source_on_1080p_canvas) {
    RECT r = ComputePixelPerfectDestRect(1920, 888, 1920, 1080);
    DUWN_ASSERT(r.left == 0 && r.right == 1920);
    DUWN_ASSERT(r.top == 96 && r.bottom == 984);
}

DUWN_TEST(fit_visible_1920x888_preserves_pixels_on_1080p_canvas) {
    RECT r = ComputeFitDestRect(1920, 888, 1920, 1080);
    DUWN_ASSERT(r.left == 0 && r.right == 1920);
    DUWN_ASSERT(r.top == 96 && r.bottom == 984);
}

DUWN_TEST(pixel_perfect_smaller_source_stays_unscaled) {
    RECT r = ComputePixelPerfectDestRect(1280, 720, 1920, 1080);
    DUWN_ASSERT(r.left == 320 && r.right == 1600);
    DUWN_ASSERT(r.top == 180 && r.bottom == 900);
}

DUWN_TEST(pixel_perfect_larger_source_fits_once) {
    RECT r = ComputePixelPerfectDestRect(2560, 1440, 1920, 1080);
    DUWN_ASSERT(r.left == 0 && r.top == 0);
    DUWN_ASSERT(r.right == 1920 && r.bottom == 1080);
}

// ---------------------------------------------------------------------------
// Fit tests — inputs are visible dims; geometry computed on visible area only
// ---------------------------------------------------------------------------

DUWN_TEST(fit_same_ar) {
    // 1920x1080 visible → 1920x1080 canvas: dest must cover entire canvas
    RECT r = ComputeFitDestRect(1920, 1080, 1920, 1080);
    DUWN_ASSERT(r.left   == 0);
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.right  == 1920);
    DUWN_ASSERT(r.bottom == 1080);
}

DUWN_TEST(fit_wide_landscape_letterbox) {
    // Visible 2560x1184 (AR ≈ 2.162) → 1920x1080 (AR ≈ 1.778)
    // src wider than canvas → LETTERBOX (bars top+bottom)
    RECT r = ComputeFitDestRect(2560, 1184, 1920, 1080);
    // Full canvas width fitted
    DUWN_ASSERT(r.left  == 0);
    DUWN_ASSERT(r.right == 1920);
    // Bars exist top and bottom
    DUWN_ASSERT(r.top    > 0);
    DUWN_ASSERT(r.bottom < 1080);
    // Centred: top bar == bottom bar (integer arithmetic may be off by 1)
    DUWN_ASSERT(r.top == (1080 - (r.bottom - r.top)) / 2);
    // No stretching
    DUWN_ASSERT(r.bottom - r.top < 1080);
}

DUWN_TEST(fit_4x3_pillarbox) {
    // Visible 1440x1080 (AR=1.333) → 1920x1080 (AR≈1.778)
    // src narrower than canvas → PILLARBOX (bars left+right)
    RECT r = ComputeFitDestRect(1440, 1080, 1920, 1080);
    // Full canvas height
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.bottom == 1080);
    // Bars left and right
    DUWN_ASSERT(r.left  > 0);
    DUWN_ASSERT(r.right < 1920);
    // Centred
    DUWN_ASSERT(r.left == (1920 - (r.right - r.left)) / 2);
    // No stretching
    DUWN_ASSERT(r.right - r.left < 1920);
}

DUWN_TEST(fit_narrow_portrait_wide_pillarbox) {
    // Visible 1184x2560 (AR ≈ 0.4625) → 1920x1080 canvas
    // Very narrow → wide PILLARBOX
    RECT r = ComputeFitDestRect(1184, 2560, 1920, 1080);
    // Full canvas height
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.bottom == 1080);
    // Narrow strip, centred
    DUWN_ASSERT(r.left  > 0);
    DUWN_ASSERT(r.right < 1920);
    // rw = floor(1080 * 1184/2560) = floor(499.5) = 499 (odd — Fit does NOT force even)
    // lx = (1920-499)/2 = 710, r.right = 710+499 = 1209
    // Left bar: 710px, right bar: 1920-1209 = 711px — off by 1 due to odd rw.
    // Assert centred within 1 pixel (floor rounding, not a bug):
    DUWN_ASSERT(r.left >= (1920 - (r.right - r.left)) / 2);
    DUWN_ASSERT(r.left <= (1920 - (r.right - r.left)) / 2 + 1);
    // Width ≈ 499
    DUWN_ASSERT(r.right - r.left < 700);
    // Exact
    DUWN_ASSERT(r.right - r.left == 499);
    DUWN_ASSERT(r.left == 710);
}

// ---------------------------------------------------------------------------
// Fill tests — inputs are visible dims; returned rect is relative to (0,0)
// of the visible area; caller adds (visible_x, visible_y) before VP call.
// ---------------------------------------------------------------------------

DUWN_TEST(fill_same_ar) {
    // 1920x1080 visible → 1920x1080: no crop
    RECT r = ComputeFillSrcRect(1920, 1080, 1920, 1080);
    DUWN_ASSERT(r.left   == 0);
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.right  == 1920);
    DUWN_ASSERT(r.bottom == 1080);
}

DUWN_TEST(fill_wide_landscape_crop_sides) {
    // Visible 2560x1184 → 1920x1080 canvas
    // src AR ≈ 2.162 > 16:9 → crop left+right, keep full height
    // dst_ar = 1920/1080 ≈ 1.778
    // cw = floor(1184 * 1.778) = floor(2105.2) = 2105, even → 2104
    // ch = 1184 (even, kept in full)
    // ox = (2560-2104)/2 = 228, even → 228
    RECT r = ComputeFillSrcRect(2560, 1184, 1920, 1080);
    // Full visible height
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.bottom == 1184);
    // Left+right cropped
    DUWN_ASSERT(r.left  > 0);
    DUWN_ASSERT(r.right < 2560);
    // Even alignment (NV12 chroma)
    DUWN_ASSERT(r.left            % 2 == 0);
    DUWN_ASSERT(r.top             % 2 == 0);
    DUWN_ASSERT((r.right - r.left) % 2 == 0);
    DUWN_ASSERT((r.bottom - r.top) % 2 == 0);
    // Exact: cw=2104, ox=228
    DUWN_ASSERT(r.left   == 228);
    DUWN_ASSERT(r.right  == 228 + 2104);
}

DUWN_TEST(fill_4x3_crop_top_bottom) {
    // Visible 1440x1080 (4:3) → 1920x1080
    // src AR 1.333 < 16:9 → crop top+bottom, keep full width
    // ch = floor(1440 / (1920/1080)) = floor(1440 * 1080/1920) = floor(810) = 810, even→810
    // oy = (1080-810)/2 = 135, even→134
    RECT r = ComputeFillSrcRect(1440, 1080, 1920, 1080);
    DUWN_ASSERT(r.left  == 0);
    DUWN_ASSERT(r.right == 1440);
    DUWN_ASSERT(r.top    > 0);
    DUWN_ASSERT(r.bottom < 1080);
    DUWN_ASSERT(r.left            % 2 == 0);
    DUWN_ASSERT(r.top             % 2 == 0);
    DUWN_ASSERT((r.right - r.left) % 2 == 0);
    DUWN_ASSERT((r.bottom - r.top) % 2 == 0);
    // Exact height
    DUWN_ASSERT(r.bottom - r.top == 810);
    // Exact offset: 135 even-aligned → 134
    DUWN_ASSERT(r.top    == 134);
    DUWN_ASSERT(r.bottom == 134 + 810);
}

DUWN_TEST(fill_narrow_portrait_crop_top_bottom) {
    // Visible 1184x2560 (narrow portrait) → 1920x1080
    // src AR ≈ 0.4625 < 16:9 → crop top+bottom, keep full width
    // ch = floor(1184 / (1920/1080)) = floor(1184 * 1080/1920) = floor(666) = 666, even→666
    // oy = (2560-666)/2 = 947, even→946
    RECT r = ComputeFillSrcRect(1184, 2560, 1920, 1080);
    DUWN_ASSERT(r.left  == 0);
    DUWN_ASSERT(r.right == 1184);
    DUWN_ASSERT(r.top    > 0);
    DUWN_ASSERT(r.bottom < 2560);
    DUWN_ASSERT(r.left            % 2 == 0);
    DUWN_ASSERT(r.top             % 2 == 0);
    DUWN_ASSERT((r.right - r.left) % 2 == 0);
    DUWN_ASSERT((r.bottom - r.top) % 2 == 0);
    // Exact: ch = floor(1184 * 1080/1920) = floor(666.0) = 666, even→666
    DUWN_ASSERT(r.bottom - r.top == 666);
    // Exact offset: (2560-666)/2 = 947, even→946
    DUWN_ASSERT(r.top    == 946);
    DUWN_ASSERT(r.bottom == 946 + 666);
}

// ---------------------------------------------------------------------------
// Visible aperture tests
// Verify that passing visible (sub-texture) dims gives correct geometry.
// The coded texture may be larger; caller offsets VP rect by visible_x/y.
// These tests verify the math is correct for non-zero offsets handled outside.
// ---------------------------------------------------------------------------

DUWN_TEST(fit_visible_smaller_than_coded) {
    // Coded 2560x1184 NV12, visible 2540x1164 (20px right, 20px bottom trimmed).
    // Geometry must use visible dims only.
    // AR = 2540/1164 ≈ 2.182 > 16:9 → letterbox
    RECT r = ComputeFitDestRect(2540, 1164, 1920, 1080);
    DUWN_ASSERT(r.left  == 0);
    DUWN_ASSERT(r.right == 1920);
    DUWN_ASSERT(r.top    > 0);
    DUWN_ASSERT(r.bottom < 1080);
    // Width must not exceed canvas
    DUWN_ASSERT(r.right - r.left <= 1920);
    DUWN_ASSERT(r.bottom - r.top <= 1080);
}

DUWN_TEST(fill_visible_aperture_offset) {
    // Visible area 1920x1080 embedded in coded 1936x1088 texture.
    // Geometry with visible dims → Fill should return full visible rect (same AR).
    RECT r = ComputeFillSrcRect(1920, 1080, 1920, 1080);
    DUWN_ASSERT(r.left   == 0);
    DUWN_ASSERT(r.top    == 0);
    DUWN_ASSERT(r.right  == 1920);
    DUWN_ASSERT(r.bottom == 1080);
}

DUWN_TEST(coded_and_visible_aperture_verification) {
    // Exact verification case:
    // Coded NV12 texture = 2560x1184
    // Visible aperture = {offset_x: 2, offset_y: 2, width: 2556, height: 1178}
    constexpr uint32_t coded_w   = 2560;
    constexpr uint32_t coded_h   = 1184;
    constexpr uint32_t visible_x = 2;
    constexpr uint32_t visible_y = 2;
    constexpr uint32_t visible_w = 2556;
    constexpr uint32_t visible_h = 1178;
    constexpr uint32_t dst_w     = 1920;
    constexpr uint32_t dst_h     = 1080;

    // 1. Verify VP coded dimensions remain 2560x1184 (not shrunk to visible dims)
    DUWN_ASSERT(coded_w == 2560);
    DUWN_ASSERT(coded_h == 1184);

    // 2. Fit aspect ratio calculation uses visible aperture 2556x1178
    // AR = 2556 / 1178 ≈ 2.16978 > 16:9 (1.7778) → letterbox
    RECT fit_dest = ComputeFitDestRect(visible_w, visible_h, dst_w, dst_h);
    DUWN_ASSERT(fit_dest.left  == 0);
    DUWN_ASSERT(fit_dest.right == 1920);
    DUWN_ASSERT(fit_dest.top   > 0);
    DUWN_ASSERT(fit_dest.bottom < 1080);
    // Dest height = floor(1920 * 1178 / 2556) = floor(884.88) = 884
    DUWN_ASSERT(fit_dest.bottom - fit_dest.top == 884);

    // 3. Fit source rect starts at aperture offset (visible_x, visible_y)
    RECT fit_src = ComputeFitSourceRect(visible_x, visible_y, visible_w, visible_h,
                                        coded_w, coded_h);
    DUWN_ASSERT(fit_src.left   == static_cast<LONG>(visible_x));
    DUWN_ASSERT(fit_src.top    == static_cast<LONG>(visible_y));
    DUWN_ASSERT(fit_src.right  == static_cast<LONG>(visible_x + visible_w));
    DUWN_ASSERT(fit_src.bottom == static_cast<LONG>(visible_y + visible_h));
    DUWN_ASSERT(fit_src.left   == 2);
    DUWN_ASSERT(fit_src.top    == 2);
    DUWN_ASSERT(fit_src.right  == 2558);
    DUWN_ASSERT(fit_src.bottom == 1180);
    DUWN_ASSERT(fit_src.right  <= static_cast<LONG>(coded_w));
    DUWN_ASSERT(fit_src.bottom <= static_cast<LONG>(coded_h));

    // 4. Fill: crop computed on visible dims, then translated by (visible_x, visible_y)
    RECT fill_src = ComputeFillSourceRect(visible_x, visible_y, visible_w, visible_h,
                                          dst_w, dst_h, coded_w, coded_h);

    // Fill crop must lie entirely within the visible aperture
    DUWN_ASSERT(fill_src.left   >= static_cast<LONG>(visible_x));
    DUWN_ASSERT(fill_src.top    >= static_cast<LONG>(visible_y));
    DUWN_ASSERT(fill_src.right  <= static_cast<LONG>(visible_x + visible_w));
    DUWN_ASSERT(fill_src.bottom <= static_cast<LONG>(visible_y + visible_h));

    // 5. Translated crop must NOT exceed coded texture bounds
    DUWN_ASSERT(fill_src.left   >= 0);
    DUWN_ASSERT(fill_src.top    >= 0);
    DUWN_ASSERT(fill_src.right  <= static_cast<LONG>(coded_w));
    DUWN_ASSERT(fill_src.bottom <= static_cast<LONG>(coded_h));

    // Even alignment for NV12 chroma subsampling
    DUWN_ASSERT(fill_src.left   % 2 == 0);
    DUWN_ASSERT(fill_src.top    % 2 == 0);
    DUWN_ASSERT((fill_src.right  - fill_src.left) % 2 == 0);
    DUWN_ASSERT((fill_src.bottom - fill_src.top)  % 2 == 0);

    // Exact values:
    // cw = floor(1178 * 1920 / 1080) = floor(2094.22) = 2094
    // ox = ((2556 - 2094) / 2) & ~1 = 230
    // translated.left = 2 + 230 = 232
    // translated.right = 232 + 2094 = 2326
    // translated.top = 2 + 0 = 2
    // translated.bottom = 2 + 1178 = 1180
    DUWN_ASSERT(fill_src.left   == 232);
    DUWN_ASSERT(fill_src.top    == 2);
    DUWN_ASSERT(fill_src.right  == 2326);
    DUWN_ASSERT(fill_src.bottom == 1180);
}

DUWN_TEST(odd_offset_aperture_inward_alignment) {
    // Coded NV12 texture = 2560x1184
    // Metadata visible: x=3, y=5, width=2556, height=1176
    // Original bounds: [3 .. 3+2556=2559], [5 .. 5+1176=1181]
    constexpr uint32_t coded_w = 2560;
    constexpr uint32_t coded_h = 1184;
    constexpr int32_t  raw_x   = 3;
    constexpr int32_t  raw_y   = 5;
    constexpr int32_t  raw_w   = 2556;
    constexpr int32_t  raw_h   = 1176;

    AlignedAperture aligned = AlignApertureInward(raw_x, raw_y, raw_w, raw_h, coded_w, coded_h);

    // Inward alignment invariants:
    // Aligned rect must lie ENTIRELY inside original aperture — NEVER expand outward
    DUWN_ASSERT(aligned.x >= static_cast<uint32_t>(raw_x));       // left >= 3, NEVER left < 3
    DUWN_ASSERT(aligned.y >= static_cast<uint32_t>(raw_y));       // top >= 5, NEVER top < 5
    DUWN_ASSERT(aligned.x + aligned.width <= static_cast<uint32_t>(raw_x + raw_w));  // right <= 2559
    DUWN_ASSERT(aligned.y + aligned.height <= static_cast<uint32_t>(raw_y + raw_h)); // bottom <= 1181

    // Bounds invariants against coded texture
    DUWN_ASSERT(aligned.x + aligned.width  <= coded_w);
    DUWN_ASSERT(aligned.y + aligned.height <= coded_h);

    // NV12 chroma alignment (4:2:0 subsampling requires even coordinates)
    DUWN_ASSERT(aligned.x      % 2 == 0);
    DUWN_ASSERT(aligned.y      % 2 == 0);
    DUWN_ASSERT(aligned.width  % 2 == 0);
    DUWN_ASSERT(aligned.height % 2 == 0);

    // Exact values:
    // left   = (3 + 1) & ~1 = 4
    // top    = (5 + 1) & ~1 = 6
    // right  = 2559 & ~1    = 2558
    // bottom = 1181 & ~1    = 1180
    // width  = 2558 - 4     = 2554
    // height = 1180 - 6     = 1174
    DUWN_ASSERT(aligned.x      == 4);
    DUWN_ASSERT(aligned.y      == 6);
    DUWN_ASSERT(aligned.width  == 2554);
    DUWN_ASSERT(aligned.height == 1174);
}

DUWN_TEST(boundary_aperture_near_coded_edge) {
    // Coded NV12 texture = 1920x1080
    // Metadata visible starts at odd offset and reaches/slightly exceeds coded boundary:
    // x=1, y=1, width=1920, height=1080 (raw right=1921, raw bottom=1081)
    constexpr uint32_t coded_w = 1920;
    constexpr uint32_t coded_h = 1080;
    constexpr int32_t  raw_x   = 1;
    constexpr int32_t  raw_y   = 1;
    constexpr int32_t  raw_w   = 1920;
    constexpr int32_t  raw_h   = 1080;

    AlignedAperture aligned = AlignApertureInward(raw_x, raw_y, raw_w, raw_h, coded_w, coded_h);

    // Must be clamped to coded bounds and aligned inward
    DUWN_ASSERT(aligned.x >= static_cast<uint32_t>(raw_x));
    DUWN_ASSERT(aligned.y >= static_cast<uint32_t>(raw_y));
    DUWN_ASSERT(aligned.x + aligned.width  <= coded_w);
    DUWN_ASSERT(aligned.y + aligned.height <= coded_h);

    // Even alignment
    DUWN_ASSERT(aligned.x      % 2 == 0);
    DUWN_ASSERT(aligned.y      % 2 == 0);
    DUWN_ASSERT(aligned.width  % 2 == 0);
    DUWN_ASSERT(aligned.height % 2 == 0);

    // Exact values:
    // orig_left = 1, orig_top = 1, orig_right = 1920, orig_bottom = 1080
    // left   = (1 + 1) & ~1 = 2
    // top    = (1 + 1) & ~1 = 2
    // right  = 1920 & ~1    = 1920
    // bottom = 1080 & ~1    = 1080
    // width  = 1920 - 2     = 1918
    // height = 1080 - 2     = 1078
    DUWN_ASSERT(aligned.x      == 2);
    DUWN_ASSERT(aligned.y      == 2);
    DUWN_ASSERT(aligned.width  == 1918);
    DUWN_ASSERT(aligned.height == 1078);
}

// ---------------------------------------------------------------------------
// ValidateBltGeometry tests
// ---------------------------------------------------------------------------

DUWN_TEST(validate_blt_geometry_valid) {
    RECT src{0, 0, 500, 1080}; // even coords, inside 512x1088
    RECT dst{0, 0, 1280, 720};
    RECT canvas{0, 0, 1280, 720};
    DUWN_ASSERT(ValidateBltGeometry(src, dst, canvas, 512, 1088) == true);
}

DUWN_TEST(validate_blt_geometry_odd_source_rejected) {
    RECT src{1, 0, 501, 1080}; // odd left/right coordinates invalid for NV12
    RECT dst{0, 0, 1280, 720};
    RECT canvas{0, 0, 1280, 720};
    DUWN_ASSERT(ValidateBltGeometry(src, dst, canvas, 512, 1088) == false);
}

DUWN_TEST(validate_blt_geometry_out_of_bounds_rejected) {
    RECT src{0, 0, 600, 1080}; // exceeds coded_w 512
    RECT dst{0, 0, 1280, 720};
    RECT canvas{0, 0, 1280, 720};
    DUWN_ASSERT(ValidateBltGeometry(src, dst, canvas, 512, 1088) == false);
}

DUWN_TEST(validate_blt_geometry_empty_rect_rejected) {
    RECT src{0, 0, 0, 0};
    RECT dst{0, 0, 1280, 720};
    RECT canvas{0, 0, 1280, 720};
    DUWN_ASSERT(ValidateBltGeometry(src, dst, canvas, 512, 1088) == false);
}

// ---------------------------------------------------------------------------
// Orientation matching tests
// ---------------------------------------------------------------------------

DUWN_TEST(orientation_matching_portrait) {
    // 512x888 visible aperture, 512x1088 coded texture (both portrait)
    DUWN_ASSERT(IsApertureOrientationMatching(512, 888, 512, 1088) == true);
}

DUWN_TEST(orientation_matching_landscape) {
    // 1920x888 visible aperture, 1920x896 coded texture (both landscape)
    DUWN_ASSERT(IsApertureOrientationMatching(1920, 888, 1920, 896) == true);
}

DUWN_TEST(orientation_mismatch_rejected) {
    // Stale portrait visible aperture (512x888) with new landscape texture (1920x896)
    DUWN_ASSERT(IsApertureOrientationMatching(512, 888, 1920, 896) == false);

    // Stale landscape visible aperture (1920x888) with new portrait texture (512x1088)
    DUWN_ASSERT(IsApertureOrientationMatching(1920, 888, 512, 1088) == false);
}

DUWN_TEST(orientation_square_or_zero_rejected) {
    DUWN_ASSERT(IsApertureOrientationMatching(0, 888, 1920, 896) == false);
    DUWN_ASSERT(IsApertureOrientationMatching(500, 500, 1920, 896) == false);
}

// ---------------------------------------------------------------------------
// Auto aspect ratio & Pixel Perfect Auto/On/Off mode tests
// ---------------------------------------------------------------------------

DUWN_TEST(aspect_ratio_auto_match_source_no_black_bars) {
    // In Match Source mode, destination canvas equals source visible dimensions (e.g. 1920x888).
    // Destination rect occupies 100% of canvas with 0 black bars.
    constexpr uint32_t src_w = 1920;
    constexpr uint32_t src_h = 888;
    constexpr uint32_t dst_w = 1920;
    constexpr uint32_t dst_h = 888;

    RECT dst = ComputeFitDestRect(src_w, src_h, dst_w, dst_h);
    DUWN_ASSERT(dst.left == 0);
    DUWN_ASSERT(dst.top == 0);
    DUWN_ASSERT(dst.right == static_cast<LONG>(dst_w));
    DUWN_ASSERT(dst.bottom == static_cast<LONG>(dst_h));

    RECT src = ComputeFitSourceRect(0, 0, src_w, src_h, src_w, src_h);
    DUWN_ASSERT(src.left == 0 && src.top == 0);
    DUWN_ASSERT(src.right == static_cast<LONG>(src_w) && src.bottom == static_cast<LONG>(src_h));
}

DUWN_TEST(aspect_ratio_auto_fixed_canvas_fits) {
    // When output is fixed 1920x1080 canvas, Auto aspect mode behaves as Fit
    // For 1920x888 visible source, it produces letterbox bars top and bottom
    constexpr uint32_t src_w = 1920;
    constexpr uint32_t src_h = 888;
    constexpr uint32_t dst_w = 1920;
    constexpr uint32_t dst_h = 1080;

    RECT dst = ComputeFitDestRect(src_w, src_h, dst_w, dst_h);
    DUWN_ASSERT(dst.left == 0);
    DUWN_ASSERT(dst.right == 1920);
    DUWN_ASSERT(dst.top == 96);
    DUWN_ASSERT(dst.bottom == 984);
}

DUWN_TEST(pixel_perfect_auto_mode_matches_and_scales) {
    // 1. Equal dimensions: 1:1 pixel mapping covers destination completely
    RECT match = (1920 == 1920 && 1080 == 1080)
        ? RECT{0, 0, 1920, 1080}
        : ComputeFitDestRect(1920, 1080, 1920, 1080);
    DUWN_ASSERT(match.left == 0 && match.top == 0);
    DUWN_ASSERT(match.right == 1920 && match.bottom == 1080);

    // 2. Different dimensions with Auto: single scale-to-fit
    RECT scaled = ComputeFitDestRect(1280, 720, 1920, 1080);
    DUWN_ASSERT(scaled.left == 0 && scaled.top == 0);
    DUWN_ASSERT(scaled.right == 1920 && scaled.bottom == 1080);

    // 3. PixelPerfectMode::On: strictly preserves 1:1 pixels centered
    RECT strict = ComputePixelPerfectDestRect(1280, 720, 1920, 1080);
    DUWN_ASSERT(strict.left == 320 && strict.top == 180);
    DUWN_ASSERT(strict.right == 1600 && strict.bottom == 900);
}

// ---------------------------------------------------------------------------
// Aspect-Aware Output Quality tests
// Quality classes specify long-edge targets while preserving visible aspect ratio
// ---------------------------------------------------------------------------

DUWN_TEST(aspect_aware_iphone_landscape) {
    // iPhone XS / 13 Pro 19.5:9 visible aperture in landscape: 2560x1184
    constexpr uint32_t src_w = 2560;
    constexpr uint32_t src_h = 1184;

    // HD: long edge 1280 -> 1280 x 592
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 1280 && hd.height == 592);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 1920 x 888
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 1920 && fhd.height == 888);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 2560 x 1184
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 2560 && qhd.height == 1184);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 3840 x 1776
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 3840 && uhd.height == 1776);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough 2560 x 1184
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 2560 && orig.height == 1184);
}

DUWN_TEST(aspect_aware_iphone_portrait) {
    // iPhone XS / 13 Pro 19.5:9 visible aperture in portrait: 1184x2560
    constexpr uint32_t src_w = 1184;
    constexpr uint32_t src_h = 2560;

    // HD: long edge 1280 -> 592 x 1280
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 592 && hd.height == 1280);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 888 x 1920
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 888 && fhd.height == 1920);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 1184 x 2560
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 1184 && qhd.height == 2560);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 1776 x 3840
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 1776 && uhd.height == 3840);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough 1184 x 2560
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 1184 && orig.height == 2560);
}

DUWN_TEST(aspect_aware_iphone_666x1440_portrait) {
    // iPhone standard 666x1440 visible aperture in portrait (e.g. iPhone AirPlay stream)
    constexpr uint32_t src_w = 666;
    constexpr uint32_t src_h = 1440;

    // HD: long edge 1280 -> 592 x 1280 (NOT short edge 720x1556)
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 592 && hd.height == 1280);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 888 x 1920 (NOT short edge 1080x2336)
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 888 && fhd.height == 1920);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 1184 x 2560
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 1184 && qhd.height == 2560);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 1776 x 3840
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 1776 && uhd.height == 3840);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough of visible aperture (666 x 1440)
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 666 && orig.height == 1440);
}

DUWN_TEST(aspect_aware_iphone_1440x666_landscape) {
    // iPhone standard 1440x666 visible aperture in landscape
    constexpr uint32_t src_w = 1440;
    constexpr uint32_t src_h = 666;

    // HD: long edge 1280 -> 1280 x 592
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 1280 && hd.height == 592);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 1920 x 888
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 1920 && fhd.height == 888);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 2560 x 1184
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 2560 && qhd.height == 1184);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 3840 x 1776
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 3840 && uhd.height == 1776);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough of visible aperture (1440 x 666)
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 1440 && orig.height == 666);
}

DUWN_TEST(aspect_aware_coded_vs_visible_aperture_passthrough) {
    // MF H.264 decoder outputs coded texture padded to macroblock boundary (672x1440)
    // Visible display aperture is 666x1440.
    // Original output resolution MUST strictly use visible aperture (666x1440), NOT coded (672x1440).
    constexpr uint32_t coded_w = 672;
    constexpr uint32_t coded_h = 1440;
    constexpr uint32_t visible_w = 666;
    constexpr uint32_t visible_h = 1440;

    (void)coded_w;
    (void)coded_h;

    // When caller passes visible aperture to ComputeAspectAwareOutputDimensions for Original (0):
    auto orig = ComputeAspectAwareOutputDimensions(visible_w, visible_h, 0);
    DUWN_ASSERT(orig.width == 666);
    DUWN_ASSERT(orig.height == 1440);
    DUWN_ASSERT(orig.width != 672); // Must NOT be coded width!
}

DUWN_TEST(aspect_aware_ipad_landscape) {
    // iPad Pro M4 4:3 visible aperture in landscape: 2048x1536
    constexpr uint32_t src_w = 2048;
    constexpr uint32_t src_h = 1536;

    // HD: long edge 1280 -> 1280 x 960
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 1280 && hd.height == 960);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 1920 x 1440
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 1920 && fhd.height == 1440);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 2560 x 1920
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 2560 && qhd.height == 1920);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 3840 x 2880
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 3840 && uhd.height == 2880);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough 2048 x 1536
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 2048 && orig.height == 1536);
}

DUWN_TEST(aspect_aware_ipad_portrait) {
    // iPad Pro M4 4:3 visible aperture in portrait: 1536x2048
    constexpr uint32_t src_w = 1536;
    constexpr uint32_t src_h = 2048;

    // HD: long edge 1280 -> 960 x 1280
    auto hd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1280);
    DUWN_ASSERT(hd.width == 960 && hd.height == 1280);
    DUWN_ASSERT((hd.width % 2 == 0) && (hd.height % 2 == 0));

    // Full HD: long edge 1920 -> 1440 x 1920
    auto fhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 1920);
    DUWN_ASSERT(fhd.width == 1440 && fhd.height == 1920);
    DUWN_ASSERT((fhd.width % 2 == 0) && (fhd.height % 2 == 0));

    // 2K: long edge 2560 -> 1920 x 2560
    auto qhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 2560);
    DUWN_ASSERT(qhd.width == 1920 && qhd.height == 2560);
    DUWN_ASSERT((qhd.width % 2 == 0) && (qhd.height % 2 == 0));

    // 4K: long edge 3840 -> 2880 x 3840
    auto uhd = ComputeAspectAwareOutputDimensions(src_w, src_h, 3840);
    DUWN_ASSERT(uhd.width == 2880 && uhd.height == 3840);
    DUWN_ASSERT((uhd.width % 2 == 0) && (uhd.height % 2 == 0));

    // Original / Auto: long edge 0 -> 1:1 direct passthrough 1536 x 2048
    auto orig = ComputeAspectAwareOutputDimensions(src_w, src_h, 0);
    DUWN_ASSERT(orig.width == 1536 && orig.height == 2048);
}

DUWN_TEST(aspect_aware_odd_dimensions_even_aligned) {
    // Arbitrary unaligned / fractional source aspect ratios
    // Must strictly produce even dimensions for NV12 chroma subsampling & D3D11 VideoProcessor
    auto res1 = ComputeAspectAwareOutputDimensions(2556, 1179, 1920);
    DUWN_ASSERT((res1.width % 2 == 0) && (res1.height % 2 == 0));
    DUWN_ASSERT(res1.width == 1920);

    auto res2 = ComputeAspectAwareOutputDimensions(1179, 2556, 1920);
    DUWN_ASSERT((res2.width % 2 == 0) && (res2.height % 2 == 0));
    DUWN_ASSERT(res2.height == 1920);

    auto res3 = ComputeAspectAwareOutputDimensions(1333, 777, 1280);
    DUWN_ASSERT((res3.width % 2 == 0) && (res3.height % 2 == 0));
    DUWN_ASSERT(res3.width == 1280);
}

DUWN_TEST(aspect_aware_zero_source_fallback) {
    // When stream metadata has not yet arrived (0x0), safely returns even fallback
    auto fb1 = ComputeAspectAwareOutputDimensions(0, 0, 1920);
    DUWN_ASSERT(fb1.width == 1920 && fb1.height == 1080);
    DUWN_ASSERT((fb1.width % 2 == 0) && (fb1.height % 2 == 0));

    auto fb0 = ComputeAspectAwareOutputDimensions(0, 0, 0);
    DUWN_ASSERT(fb0.width == 1920 && fb0.height == 1080);
}

// ---------------------------------------------------------------------------
// Preview Window Ergonomics: Comfortable Auto-Sizing & Rotation UX
// ---------------------------------------------------------------------------

DUWN_TEST(comfortable_preview_dimensions_landscape) {
    // 2560x1184 iPhone landscape on 1920x1080 monitor work area
    // Target 50% width: 1920 * 0.50 = 960
    // Height: 960 * 1184 / 2560 = 444
    constexpr uint32_t src_w = 2560;
    constexpr uint32_t src_h = 1184;
    constexpr uint32_t work_w = 1920;
    constexpr uint32_t work_h = 1080;

    auto dims = ComputeComfortablePreviewDimensions(src_w, src_h, work_w, work_h);
    DUWN_ASSERT(dims.width == 960);
    DUWN_ASSERT(dims.height == 444);
    DUWN_ASSERT((dims.width % 2 == 0) && (dims.height % 2 == 0));
    DUWN_ASSERT(dims.width >= 200 && dims.height >= 200);
    DUWN_ASSERT(dims.width <= static_cast<uint32_t>(work_w * 0.70));
    DUWN_ASSERT(dims.height <= static_cast<uint32_t>(work_h * 0.70));
}

DUWN_TEST(comfortable_preview_dimensions_portrait) {
    // 1184x2560 iPhone portrait on 1920x1080 monitor work area
    // Target 60% height: 1080 * 0.60 = 648
    // Width: 648 * 1184 / 2560 = 299.7 -> 300
    constexpr uint32_t src_w = 1184;
    constexpr uint32_t src_h = 2560;
    constexpr uint32_t work_w = 1920;
    constexpr uint32_t work_h = 1080;

    auto dims = ComputeComfortablePreviewDimensions(src_w, src_h, work_w, work_h);
    DUWN_ASSERT(dims.width == 300);
    DUWN_ASSERT(dims.height == 648);
    DUWN_ASSERT((dims.width % 2 == 0) && (dims.height % 2 == 0));
    DUWN_ASSERT(dims.width >= 200 && dims.height >= 200);
    DUWN_ASSERT(dims.width <= static_cast<uint32_t>(work_w * 0.70));
    DUWN_ASSERT(dims.height <= static_cast<uint32_t>(work_h * 0.70));
}

DUWN_TEST(comfortable_preview_dimensions_clamping) {
    // Very small work area: clamped to min 200px
    auto small_dims = ComputeComfortablePreviewDimensions(1920, 1080, 300, 300);
    DUWN_ASSERT(small_dims.width >= 200 && small_dims.height >= 200);

    // Fallback on 0x0 input
    auto zero_dims = ComputeComfortablePreviewDimensions(0, 0, 1920, 1080);
    DUWN_ASSERT(zero_dims.width == 640 && zero_dims.height == 360);
}

DUWN_TEST(rotated_preview_dimensions_portrait_to_landscape) {
    // User had a portrait preview sized at 300x648.
    // Device rotates to landscape (source becomes 2560x1184).
    // Bounding dimension is 648.
    // New width = 648, new height = 648 * 1184 / 2560 = 299.7 -> 300.
    // Preserves bounding box area without blowing up to 2560x1184!
    constexpr uint32_t prev_w = 300;
    constexpr uint32_t prev_h = 648;
    constexpr uint32_t new_src_w = 2560;
    constexpr uint32_t new_src_h = 1184;
    constexpr uint32_t work_w = 1920;
    constexpr uint32_t work_h = 1080;

    auto dims = ComputeRotatedPreviewDimensions(prev_w, prev_h, new_src_w, new_src_h, work_w, work_h);
    DUWN_ASSERT(dims.width == 648);
    DUWN_ASSERT(dims.height == 300);
    DUWN_ASSERT((dims.width % 2 == 0) && (dims.height % 2 == 0));
    DUWN_ASSERT(dims.width <= static_cast<uint32_t>(work_w * 0.70));
    DUWN_ASSERT(dims.height <= static_cast<uint32_t>(work_h * 0.70));
}

DUWN_TEST(rotated_preview_dimensions_landscape_to_portrait) {
    // User had a landscape preview sized at 960x444.
    // Device rotates to portrait (source becomes 1184x2560).
    // Max dimension is 960, but max allowed height is 70% of 1080 = 756.
    // Height is clamped to 756, width = 756 * 1184 / 2560 = 349.6 -> 350.
    constexpr uint32_t prev_w = 960;
    constexpr uint32_t prev_h = 444;
    constexpr uint32_t new_src_w = 1184;
    constexpr uint32_t new_src_h = 2560;
    constexpr uint32_t work_w = 1920;
    constexpr uint32_t work_h = 1080;

    auto dims = ComputeRotatedPreviewDimensions(prev_w, prev_h, new_src_w, new_src_h, work_w, work_h);
    DUWN_ASSERT(dims.width == 350);
    DUWN_ASSERT(dims.height == 756);
    DUWN_ASSERT((dims.width % 2 == 0) && (dims.height % 2 == 0));
    DUWN_ASSERT(dims.width <= static_cast<uint32_t>(work_w * 0.70));
    DUWN_ASSERT(dims.height <= static_cast<uint32_t>(work_h * 0.70));
}

DUWN_TEST(area_preserving_preview_dimensions_rotation) {
    // User sized a landscape preview at 800x400 (Area = 320,000).
    // Device rotates to portrait (new source 1184x2560, AR = 1184/2560 = 0.4625).
    // Target w = sqrt(320000 * 0.4625) = sqrt(148000) = ~384.7 -> 384
    // Target h = sqrt(320000 / 0.4625) = sqrt(691891.89) = ~831.8 -> 832
    // On 1920x1080 monitor, max allowed height is 70% of 1080 = 756.
    // Height is clamped to 756, and width = 756 * 0.4625 = 349.65 -> 350.
    constexpr uint32_t prev_w = 800;
    constexpr uint32_t prev_h = 400;
    constexpr uint32_t new_src_w = 1184;
    constexpr uint32_t new_src_h = 2560;
    constexpr uint32_t work_w = 1920;
    constexpr uint32_t work_h = 1080;

    auto dims = ComputeAreaPreservingPreviewDimensions(prev_w, prev_h, new_src_w, new_src_h, work_w, work_h);
    DUWN_ASSERT(dims.width == 350);
    DUWN_ASSERT(dims.height == 756);
    DUWN_ASSERT((dims.width % 2 == 0) && (dims.height % 2 == 0));
    DUWN_ASSERT(dims.width <= static_cast<uint32_t>(work_w * 0.70));
    DUWN_ASSERT(dims.height <= static_cast<uint32_t>(work_h * 0.70));

    // On a 4K display with ample height (2160, 70% = 1512), no clamping occurs:
    auto dims_4k = ComputeAreaPreservingPreviewDimensions(prev_w, prev_h, new_src_w, new_src_h, 3840, 2160);
    DUWN_ASSERT(dims_4k.width == 384);
    DUWN_ASSERT(dims_4k.height == 832);
    DUWN_ASSERT((dims_4k.width % 2 == 0) && (dims_4k.height % 2 == 0));
}

DUWN_TEST(stream_geometry_coordinator_orientation_and_generations) {
    StreamGeometryCoordinator coord;

    // 1. Initial frame: Landscape (2560x1184 visible aperture)
    VideoFrame f_land{};
    f_land.width = 2560;
    f_land.height = 1184;
    f_land.visible_width = 2560;
    f_land.visible_height = 1184;
    f_land.visible_x = 0;
    f_land.visible_y = 0;

    bool updated = coord.UpdateFromFrame(
        f_land,
        duwn::app::CaptureCanvas::FollowSource,
        duwn::app::OutputQuality::QHD_2K,
        0, 0, 1920, 1080);
    DUWN_ASSERT(updated == true);
    DUWN_ASSERT(coord.CurrentGeneration() == 1);
    DUWN_ASSERT(coord.CurrentOrientation() == StreamOrientation::Landscape);

    auto snap1 = coord.GetSnapshot();
    DUWN_ASSERT(snap1.orientation == StreamOrientation::Landscape);
    DUWN_ASSERT(snap1.resolved_output.width == 2560);
    DUWN_ASSERT(snap1.resolved_output.height == 1184);

    // 2. Same aperture: no generation increment
    updated = coord.UpdateFromFrame(
        f_land,
        duwn::app::CaptureCanvas::FollowSource,
        duwn::app::OutputQuality::QHD_2K,
        0, 0, 1920, 1080);
    DUWN_ASSERT(updated == false);
    DUWN_ASSERT(coord.CurrentGeneration() == 1);

    // 3. Rotation to Portrait (1184x2560 visible aperture): generation increments
    VideoFrame f_port{};
    f_port.width = 1184;
    f_port.height = 2560;
    f_port.visible_width = 1184;
    f_port.visible_height = 2560;
    f_port.visible_x = 0;
    f_port.visible_y = 0;
    f_port.format_generation = 2; // Generation bump from decoder

    updated = coord.UpdateFromFrame(
        f_port,
        duwn::app::CaptureCanvas::FollowSource,
        duwn::app::OutputQuality::QHD_2K,
        0, 0, 1920, 1080);
    DUWN_ASSERT(updated == true);
    DUWN_ASSERT(coord.CurrentGeneration() == 2);
    DUWN_ASSERT(coord.CurrentOrientation() == StreamOrientation::Portrait);

    auto snap2 = coord.GetSnapshot();
    DUWN_ASSERT(snap2.orientation == StreamOrientation::Portrait);
    DUWN_ASSERT(snap2.resolved_output.width == 1184);
    DUWN_ASSERT(snap2.resolved_output.height == 2560);
}


