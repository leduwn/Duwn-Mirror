// VideoGeometry.h — pure aspect-ratio math; no D3D dependency.
// Used by VideoRenderer and tested independently.
//
// Coordinate convention:
//   src (w,h) = VISIBLE image dimensions (VideoFrame.visible_width/visible_height)
//               NOT the coded (padded) texture dimensions (VideoFrame.width/height).
//               The MF H.264 decoder pads to macroblock boundaries; the visible
//               region is given by MF_MT_MINIMUM_DISPLAY_APERTURE (or GEOMETRIC_APERTURE).
//   dst (w,h) = swap chain / canvas dimensions (physical pixels)
//
// Fit  — source fully visible; black bars if AR mismatch
// Fill — canvas fully covered; source centre-cropped if AR mismatch
//
// Caller responsibility:
//   Fit:  VP source rect = (visible_x, visible_y,
//                           visible_x + visible_width, visible_y + visible_height)
//         VP dest rect   = ComputeFitDestRect(visible_w, visible_h, dst_w, dst_h)
//   Fill: VP source rect = ComputeFillSrcRect(visible_w, visible_h, dst_w, dst_h)
//                          offset by (visible_x, visible_y)
//         VP dest rect   = full canvas {0, 0, dst_w, dst_h}

#pragma once
#include <windows.h>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace duwn::video {

enum class StreamOrientation : uint8_t {
    Unknown = 0,
    Landscape = 1,
    Portrait = 2,
    Square = 3
};

inline StreamOrientation DeriveStreamOrientation(uint32_t width, uint32_t height) noexcept {
    if (width == 0 || height == 0) return StreamOrientation::Unknown;
    if (width > height) return StreamOrientation::Landscape;
    if (height > width) return StreamOrientation::Portrait;
    return StreamOrientation::Square;
}

// Fit: letter/pillarbox.
// Returns the DEST rect on the canvas where the (full) source should be drawn.
// Canvas areas outside this rect should be cleared to black (letterbox bars).
//
// src_ar > dst_ar  → source is wider → fit to canvas width  → LETTERBOX (bars top+bottom)
// src_ar < dst_ar  → source is narrower → fit to canvas height → PILLARBOX (bars left+right)
// src_ar == dst_ar → exact fit → dest == full canvas
inline RECT ComputeFitDestRect(uint32_t src_w, uint32_t src_h,
                               uint32_t dst_w, uint32_t dst_h) noexcept {
    uint32_t rw, rh;
    if (static_cast<uint64_t>(src_w) * dst_h > static_cast<uint64_t>(dst_w) * src_h) {
        rw = dst_w;
        rh = static_cast<uint32_t>(static_cast<uint64_t>(dst_w) * src_h / src_w);
    } else {
        rh = dst_h;
        rw = static_cast<uint32_t>(static_cast<uint64_t>(dst_h) * src_w / src_h);
    }
    rw = std::min(rw, dst_w);
    rh = std::min(rh, dst_h);
    LONG lx = static_cast<LONG>((dst_w - rw) / 2);
    LONG ly = static_cast<LONG>((dst_h - rh) / 2);
    return {lx, ly, lx + static_cast<LONG>(rw), ly + static_cast<LONG>(rh)};
}

inline RECT ComputePixelPerfectDestRect(uint32_t src_w, uint32_t src_h,
                                       uint32_t dst_w, uint32_t dst_h) noexcept {
    if (src_w > dst_w || src_h > dst_h)
        return ComputeFitDestRect(src_w, src_h, dst_w, dst_h);
    LONG x = static_cast<LONG>((dst_w - src_w) / 2);
    LONG y = static_cast<LONG>((dst_h - src_h) / 2);
    return {x, y, x + static_cast<LONG>(src_w), y + static_cast<LONG>(src_h)};
}

// Fill: centre-crop source so the canvas is fully covered.
// Returns the SOURCE rect relative to (0,0) of the visible area.
// Dest rect = full canvas {0, 0, dst_w, dst_h}.
//
// NV12 constraint: crop boundaries are even-aligned (4:2:0 chroma subsampling).
//
// src_ar > dst_ar → source wider → crop left+right sides
// src_ar < dst_ar → source taller → crop top+bottom
// src_ar == dst_ar → no crop → {0, 0, src_w, src_h}
inline RECT ComputeFillSrcRect(uint32_t src_w, uint32_t src_h,
                               uint32_t dst_w, uint32_t dst_h) noexcept {
    float src_ar = static_cast<float>(src_w) / static_cast<float>(src_h);
    float dst_ar = static_cast<float>(dst_w) / static_cast<float>(dst_h);
    uint32_t cw, ch;
    if (src_ar > dst_ar) {
        ch = src_h;
        cw = static_cast<uint32_t>(static_cast<float>(src_h) * dst_ar);
    } else {
        cw = src_w;
        ch = static_cast<uint32_t>(static_cast<float>(src_w) / dst_ar);
    }
    cw &= ~1u;  // even for NV12 chroma
    ch &= ~1u;
    cw = std::min(cw, src_w);
    ch = std::min(ch, src_h);
    LONG ox = static_cast<LONG>(((src_w - cw) / 2) & ~1u);
    LONG oy = static_cast<LONG>(((src_h - ch) / 2) & ~1u);
    return {ox, oy, ox + static_cast<LONG>(cw), oy + static_cast<LONG>(ch)};
}

// ComputeFitSourceRect:
// Source rect for Fit mode: full visible aperture translated into coded texture
// coordinates and clamped to coded bounds.
inline RECT ComputeFitSourceRect(uint32_t visible_x, uint32_t visible_y,
                                 uint32_t visible_w, uint32_t visible_h,
                                 uint32_t coded_w,   uint32_t coded_h) noexcept {
    LONG l = static_cast<LONG>(visible_x);
    LONG t = static_cast<LONG>(visible_y);
    LONG r = static_cast<LONG>(visible_x + visible_w);
    LONG b = static_cast<LONG>(visible_y + visible_h);
    l = std::clamp(l, 0L, static_cast<LONG>(coded_w));
    t = std::clamp(t, 0L, static_cast<LONG>(coded_h));
    r = std::clamp(r, l,  static_cast<LONG>(coded_w));
    b = std::clamp(b, t,  static_cast<LONG>(coded_h));
    return {l, t, r, b};
}

// ComputeFillSourceRect:
// Source rect for Fill mode: ComputeFillSrcRect on visible dims, translated by
// (visible_x, visible_y) into coded texture coordinates, clamped to coded bounds.
inline RECT ComputeFillSourceRect(uint32_t visible_x, uint32_t visible_y,
                                  uint32_t visible_w, uint32_t visible_h,
                                  uint32_t dst_w,     uint32_t dst_h,
                                  uint32_t coded_w,   uint32_t coded_h) noexcept {
    RECT fill = ComputeFillSrcRect(visible_w, visible_h, dst_w, dst_h);
    LONG l = static_cast<LONG>(visible_x) + fill.left;
    LONG t = static_cast<LONG>(visible_y) + fill.top;
    LONG r = static_cast<LONG>(visible_x) + fill.right;
    LONG b = static_cast<LONG>(visible_y) + fill.bottom;
    l = std::clamp(l, 0L, static_cast<LONG>(coded_w));
    t = std::clamp(t, 0L, static_cast<LONG>(coded_h));
    r = std::clamp(r, l,  static_cast<LONG>(coded_w));
    b = std::clamp(b, t,  static_cast<LONG>(coded_h));
    return {l, t, r, b};
}

// Inward NV12 alignment for display aperture within coded texture bounds.
struct AlignedAperture {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
};

inline AlignedAperture AlignApertureInward(int32_t raw_x, int32_t raw_y,
                                          int32_t raw_w, int32_t raw_h,
                                          uint32_t coded_w, uint32_t coded_h) noexcept {
    // 1. Initial clamp of raw aperture to coded texture bounds
    int32_t orig_left   = std::clamp(raw_x, 0, static_cast<int32_t>(coded_w));
    int32_t orig_top    = std::clamp(raw_y, 0, static_cast<int32_t>(coded_h));
    int32_t orig_right  = std::clamp(raw_x + std::max(0, raw_w), orig_left, static_cast<int32_t>(coded_w));
    int32_t orig_bottom = std::clamp(raw_y + std::max(0, raw_h), orig_top,  static_cast<int32_t>(coded_h));

    // 2. Inward alignment to even coordinates for NV12 chroma subsampling:
    // left: align up even ((v + 1) & ~1)
    // top: align up even ((v + 1) & ~1)
    // right: align down even (v & ~1)
    // bottom: align down even (v & ~1)
    int32_t left   = (orig_left + 1) & ~1;
    int32_t top    = (orig_top + 1) & ~1;
    int32_t right  = orig_right & ~1;
    int32_t bottom = orig_bottom & ~1;

    // 3. Invariant check: never expand outward, must stay within bounds
    if (left < orig_left || top < orig_top || right > orig_right || bottom > orig_bottom ||
        left < 0 || top < 0 || right > static_cast<int32_t>(coded_w) || bottom > static_cast<int32_t>(coded_h) ||
        right <= left || bottom <= top) {
        // Safe fallback to full coded texture aligned to NV12 even bounds
        uint32_t fb_w = coded_w & ~1u;
        uint32_t fb_h = coded_h & ~1u;
        return {0, 0, fb_w, fb_h};
    }

    return {
        static_cast<uint32_t>(left),
        static_cast<uint32_t>(top),
        static_cast<uint32_t>(right - left),
        static_cast<uint32_t>(bottom - top)
    };
}

// Check if aperture orientation matches coded texture orientation
inline bool IsApertureOrientationMatching(uint32_t vis_w, uint32_t vis_h,
                                          uint32_t coded_w, uint32_t coded_h) noexcept {
    if (vis_w == 0 || vis_h == 0 || coded_w == 0 || coded_h == 0) return false;
    if (vis_w == vis_h || coded_w == coded_h) return false;
    bool vis_is_portrait = (vis_h > vis_w);
    bool coded_is_portrait = (coded_h > coded_w);
    return vis_is_portrait == coded_is_portrait;
}

// Pre-Blt geometry validation: bounds, non-empty, and NV12 even alignment.
inline bool ValidateBltGeometry(const RECT& src, const RECT& dst, const RECT& canvas,
                                uint32_t coded_w, uint32_t coded_h) noexcept {
    // Check non-negative coordinates
    if (src.left < 0 || src.top < 0 || dst.left < 0 || dst.top < 0 || canvas.left < 0 || canvas.top < 0) {
        return false;
    }
    // Check non-empty dimensions
    if (src.right <= src.left || src.bottom <= src.top) return false;
    if (dst.right <= dst.left || dst.bottom <= dst.top) return false;
    if (canvas.right <= canvas.left || canvas.bottom <= canvas.top) return false;

    // Check bounds against texture and canvas
    if (src.right > static_cast<LONG>(coded_w) || src.bottom > static_cast<LONG>(coded_h)) return false;
    if (dst.right > canvas.right || dst.bottom > canvas.bottom) return false;

    // Check NV12 even alignment for source rectangle coordinates (4:2:0 chroma)
    if ((src.left & 1) != 0 || (src.top & 1) != 0 || (src.right & 1) != 0 || (src.bottom & 1) != 0) {
        return false;
    }

    return true;
}

struct OutputDimensions {
    uint32_t width{0};
    uint32_t height{0};

    constexpr bool operator==(const OutputDimensions& other) const noexcept {
        return width == other.width && height == other.height;
    }
    constexpr bool operator!=(const OutputDimensions& other) const noexcept {
        return !(*this == other);
    }
};

struct PreviewDimensions {
    uint32_t width{0};
    uint32_t height{0};

    constexpr bool operator==(const PreviewDimensions& other) const noexcept {
        return width == other.width && height == other.height;
    }
    constexpr bool operator!=(const PreviewDimensions& other) const noexcept {
        return !(*this == other);
    }
};

// Aspect-ratio-aware output dimension derivation.
// Long-edge class: 0 (Original/Auto -> 1:1 direct passthrough of visible dims),
// 1280 (HD), 1920 (Full HD), 2560 (2K), 3840 (4K).
// Derives the secondary dimension dynamically from the actual visible source aspect ratio.
// Dimensions are minimally aligned to even numbers for NV12 chroma subsampling and D3D11.
inline OutputDimensions ComputeAspectAwareOutputDimensions(
    uint32_t src_w,
    uint32_t src_h,
    uint32_t long_edge_target) noexcept
{
    if (src_w == 0 || src_h == 0) {
        if (long_edge_target == 0) return {1920, 1080};
        uint32_t def_h = static_cast<uint32_t>(long_edge_target * 9ULL / 16ULL);
        return {long_edge_target, (def_h + 1u) & ~1u};
    }

    if (long_edge_target == 0) {
        // Original / Follow Source: 1:1 direct passthrough of visible aperture
        return {src_w, src_h};
    }

    uint32_t target_w = 0;
    uint32_t target_h = 0;

    if (src_w >= src_h) {
        target_w = long_edge_target;
        double unaligned_h = static_cast<double>(long_edge_target) * static_cast<double>(src_h) / static_cast<double>(src_w);
        uint32_t rounded = static_cast<uint32_t>(std::round(unaligned_h));
        if (rounded & 1u) {
            rounded = (unaligned_h < static_cast<double>(rounded)) ? (rounded - 1u) : (rounded + 1u);
        }
        target_h = rounded;
    } else {
        target_h = long_edge_target;
        double unaligned_w = static_cast<double>(long_edge_target) * static_cast<double>(src_w) / static_cast<double>(src_h);
        uint32_t rounded = static_cast<uint32_t>(std::round(unaligned_w));
        if (rounded & 1u) {
            rounded = (unaligned_w < static_cast<double>(rounded)) ? (rounded - 1u) : (rounded + 1u);
        }
        target_w = rounded;
    }

    target_w = std::max(2u, target_w & ~1u);
    target_h = std::max(2u, target_h & ~1u);

    return {target_w, target_h};
}

// Comfortable preview auto-sizing for user desktop preview window:
// Landscape: 50% of work area width (clamped between 200px and 70% of work area).
// Portrait: 60% of work area height (clamped between 200px and 70% of work area).
// Dimensions are minimally aligned to even numbers for clean rendering.
inline PreviewDimensions ComputeComfortablePreviewDimensions(
    uint32_t src_w,
    uint32_t src_h,
    uint32_t work_area_w,
    uint32_t work_area_h) noexcept
{
    if (work_area_w == 0) work_area_w = 1920;
    if (work_area_h == 0) work_area_h = 1080;

    if (src_w == 0 || src_h == 0) {
        return {640, 360};
    }

    const double max_w = static_cast<double>(work_area_w) * 0.70;
    const double max_h = static_cast<double>(work_area_h) * 0.70;
    const double min_dim = 200.0;

    double w = 0.0;
    double h = 0.0;

    if (src_w >= src_h) {
        // Landscape: target 50% of work area width
        w = static_cast<double>(work_area_w) * 0.50;
        w = std::clamp(w, min_dim, max_w);
        h = w * static_cast<double>(src_h) / static_cast<double>(src_w);

        if (h > max_h) {
            h = max_h;
            w = h * static_cast<double>(src_w) / static_cast<double>(src_h);
        } else if (h < min_dim) {
            h = min_dim;
            w = h * static_cast<double>(src_w) / static_cast<double>(src_h);
        }
    } else {
        // Portrait: target 60% of work area height
        h = static_cast<double>(work_area_h) * 0.60;
        h = std::clamp(h, min_dim, max_h);
        w = h * static_cast<double>(src_w) / static_cast<double>(src_h);

        if (w > max_w) {
            w = max_w;
            h = w * static_cast<double>(src_h) / static_cast<double>(src_w);
        } else if (w < min_dim) {
            w = min_dim;
            h = w * static_cast<double>(src_h) / static_cast<double>(src_w);
        }
    }

    w = std::clamp(w, min_dim, max_w);
    h = std::clamp(h, min_dim, max_h);

    uint32_t final_w = std::max(200u, static_cast<uint32_t>(std::round(w)) & ~1u);
    uint32_t final_h = std::max(200u, static_cast<uint32_t>(std::round(h)) & ~1u);

    return {final_w, final_h};
}

// Rotation UX: preserves bounding box dimension (std::max(prev_w, prev_h))
// when orientation changes, preventing sudden window expansion to native canvas scale.
inline PreviewDimensions ComputeRotatedPreviewDimensions(
    uint32_t prev_w,
    uint32_t prev_h,
    uint32_t new_src_w,
    uint32_t new_src_h,
    uint32_t work_area_w,
    uint32_t work_area_h) noexcept
{
    if (prev_w == 0 || prev_h == 0) {
        return ComputeComfortablePreviewDimensions(new_src_w, new_src_h, work_area_w, work_area_h);
    }
    if (new_src_w == 0 || new_src_h == 0) {
        return {prev_w, prev_h};
    }

    if (work_area_w == 0) work_area_w = 1920;
    if (work_area_h == 0) work_area_h = 1080;

    const double max_w = static_cast<double>(work_area_w) * 0.70;
    const double max_h = static_cast<double>(work_area_h) * 0.70;
    const double min_dim = 200.0;

    double bound = static_cast<double>(std::max(prev_w, prev_h));
    double ar = static_cast<double>(new_src_w) / static_cast<double>(new_src_h);

    double w = 0.0;
    double h = 0.0;

    if (new_src_w >= new_src_h) {
        // Rotated into landscape: width takes bounding dimension
        w = bound;
        h = w / ar;
    } else {
        // Rotated into portrait: height takes bounding dimension
        h = bound;
        w = h * ar;
    }

    if (w > max_w) {
        w = max_w;
        h = w / ar;
    }
    if (h > max_h) {
        h = max_h;
        w = h * ar;
    }
    if (w < min_dim) {
        w = min_dim;
        h = w / ar;
    }
    if (h < min_dim) {
        h = min_dim;
        w = h * ar;
    }

    w = std::clamp(w, min_dim, max_w);
    h = std::clamp(h, min_dim, max_h);

    uint32_t final_w = std::max(200u, static_cast<uint32_t>(std::round(w)) & ~1u);
    uint32_t final_h = std::max(200u, static_cast<uint32_t>(std::round(h)) & ~1u);

    return {final_w, final_h};
}

// Area-preserving rotation for UserSized preview windows:
// Preserves window surface area (W * H) during rotation while strictly clamping
// within monitor work area bounds (min 200px, max 70% of work area).
inline PreviewDimensions ComputeAreaPreservingPreviewDimensions(
    uint32_t current_w,
    uint32_t current_h,
    uint32_t new_src_w,
    uint32_t new_src_h,
    uint32_t work_area_w,
    uint32_t work_area_h) noexcept
{
    if (current_w == 0 || current_h == 0) {
        return ComputeComfortablePreviewDimensions(new_src_w, new_src_h, work_area_w, work_area_h);
    }
    if (new_src_w == 0 || new_src_h == 0) {
        return {current_w, current_h};
    }
    if (work_area_w == 0) work_area_w = 1920;
    if (work_area_h == 0) work_area_h = 1080;

    const double area = static_cast<double>(current_w) * static_cast<double>(current_h);
    const double target_ar = static_cast<double>(new_src_w) / static_cast<double>(new_src_h);

    double w = std::sqrt(area * target_ar);
    double h = std::sqrt(area / target_ar);

    const double max_w = static_cast<double>(work_area_w) * 0.70;
    const double max_h = static_cast<double>(work_area_h) * 0.70;
    const double min_dim = 200.0;

    if (w > max_w) {
        w = max_w;
        h = w / target_ar;
    }
    if (h > max_h) {
        h = max_h;
        w = h * target_ar;
    }
    if (w < min_dim) {
        w = min_dim;
        h = w / target_ar;
    }
    if (h < min_dim) {
        h = min_dim;
        w = h * target_ar;
    }

    w = std::clamp(w, min_dim, max_w);
    h = std::clamp(h, min_dim, max_h);

    uint32_t final_w = std::max(200u, static_cast<uint32_t>(std::round(w)) & ~1u);
    uint32_t final_h = std::max(200u, static_cast<uint32_t>(std::round(h)) & ~1u);

    return {final_w, final_h};
}

} // namespace duwn::video
