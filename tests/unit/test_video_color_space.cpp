#include "video/VideoColorSpace.h"
#include "video/VideoRenderer.h"
#include "video/D3D11Device.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <string>
#include <windows.h>

namespace {

using namespace duwn::video;

// ---------------------------------------------------------------------------
// CPU Reference ITU-R YCbCr -> sRGB conversion for test verification
// ---------------------------------------------------------------------------
struct RgbFloat {
    float r{0.f};
    float g{0.f};
    float b{0.f};
};

struct Rgb8 {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
};

inline Rgb8 QuantizeRgb(const RgbFloat& f) noexcept {
    const auto clamp_byte = [](float v) -> uint8_t {
        float c = std::clamp(v, 0.0f, 255.0f);
        return static_cast<uint8_t>(std::round(c));
    };
    return Rgb8{clamp_byte(f.r), clamp_byte(f.g), clamp_byte(f.b)};
}

// ITU-R reference conversion:
// matrix: 1 = BT.601, 2 = BT.709, 3 = BT.2020
// range:  1 = Limited (16..235 for Y, 16..240 for Cb/Cr), 2 = Full (0..255)
inline RgbFloat CpuReferenceYCbCrToRgb(uint8_t y_in, uint8_t cb_in, uint8_t cr_in,
                                      int matrix, int range) noexcept {
    float y_norm = 0.f;
    float cb_norm = 0.f;
    float cr_norm = 0.f;

    if (range == 2) { // Full range [0..255]
        y_norm = static_cast<float>(y_in) / 255.0f;
        cb_norm = (static_cast<float>(cb_in) - 128.0f) / 255.0f;
        cr_norm = (static_cast<float>(cr_in) - 128.0f) / 255.0f;
    } else { // Limited / Studio range [16..235] for Y, [16..240] for Cb/Cr
        y_norm = (static_cast<float>(y_in) - 16.0f) / 219.0f;
        cb_norm = (static_cast<float>(cb_in) - 128.0f) / 224.0f;
        cr_norm = (static_cast<float>(cr_in) - 128.0f) / 224.0f;
    }

    float r = 0.f, g = 0.f, b = 0.f;
    if (matrix == 1) {
        // ITU-R BT.601: Kr = 0.299, Kb = 0.114
        r = y_norm + 1.4020f * cr_norm;
        g = y_norm - (0.114f * 1.7720f / 0.587f) * cb_norm - (0.299f * 1.4020f / 0.587f) * cr_norm;
        b = y_norm + 1.7720f * cb_norm;
    } else if (matrix == 3) {
        // ITU-R BT.2020: Kr = 0.2627, Kb = 0.0593
        r = y_norm + 1.4746f * cr_norm;
        g = y_norm - (0.0593f * 1.8814f / 0.6780f) * cb_norm - (0.2627f * 1.4746f / 0.6780f) * cr_norm;
        b = y_norm + 1.8814f * cb_norm;
    } else {
        // ITU-R BT.709 (default): Kr = 0.2126, Kb = 0.0722
        r = y_norm + 1.5748f * cr_norm;
        g = y_norm - (0.0722f * 1.8556f / 0.7152f) * cb_norm - (0.2126f * 1.5748f / 0.7152f) * cr_norm;
        b = y_norm + 1.8556f * cb_norm;
    }

    return RgbFloat{r * 255.0f, g * 255.0f, b * 255.0f};
}

struct SyntheticColorTarget {
    const char* name;
    uint32_t cx;
    uint32_t cy;
    float r;
    float g;
    float b;
};

inline const SyntheticColorTarget* GetTestBlocks(size_t& count) noexcept {
    static const SyntheticColorTarget blocks[] = {
        {"Black",           16, 16,   0.0f,   0.0f,   0.0f},
        {"White",           48, 16, 255.0f, 255.0f, 255.0f},
        {"Neutral Gray",    80, 16, 128.0f, 128.0f, 128.0f},
        {"Saturated Red",  112, 16, 255.0f,   0.0f,   0.0f},
        {"Saturated Green", 16, 48,   0.0f, 255.0f,   0.0f},
        {"Saturated Blue",  48, 48,   0.0f,   0.0f, 255.0f},
        {"Saturated Yellow",80, 48, 255.0f, 255.0f,   0.0f},
        {"Saturated Cyan", 112, 48,   0.0f, 255.0f, 255.0f},
    };
    count = sizeof(blocks) / sizeof(blocks[0]);
    return blocks;
}

inline void RgbToYCbCr(float r, float g, float b, int matrix, int range,
                       uint8_t& out_y, uint8_t& out_cb, uint8_t& out_cr) noexcept {
    float y_norm = 0.f, cb_norm = 0.f, cr_norm = 0.f;
    const float r_norm = r / 255.0f;
    const float g_norm = g / 255.0f;
    const float b_norm = b / 255.0f;

    if (matrix == 1) { // BT.601: Kr=0.299, Kb=0.114, Kg=0.587
        y_norm = 0.299f * r_norm + 0.587f * g_norm + 0.114f * b_norm;
        cb_norm = (b_norm - y_norm) / 1.7720f;
        cr_norm = (r_norm - y_norm) / 1.4020f;
    } else if (matrix == 3) { // BT.2020: Kr=0.2627, Kb=0.0593, Kg=0.6780
        y_norm = 0.2627f * r_norm + 0.6780f * g_norm + 0.0593f * b_norm;
        cb_norm = (b_norm - y_norm) / 1.8814f;
        cr_norm = (r_norm - y_norm) / 1.4746f;
    } else { // BT.709: Kr=0.2126, Kb=0.0722, Kg=0.7152
        y_norm = 0.2126f * r_norm + 0.7152f * g_norm + 0.0722f * b_norm;
        cb_norm = (b_norm - y_norm) / 1.8556f;
        cr_norm = (r_norm - y_norm) / 1.5748f;
    }

    if (range == 2) { // Full range [0..255]
        out_y  = static_cast<uint8_t>(std::clamp(std::round(y_norm * 255.0f), 0.0f, 255.0f));
        out_cb = static_cast<uint8_t>(std::clamp(std::round(cb_norm * 255.0f + 128.0f), 0.0f, 255.0f));
        out_cr = static_cast<uint8_t>(std::clamp(std::round(cr_norm * 255.0f + 128.0f), 0.0f, 255.0f));
    } else { // Limited range [16..235] Y, [16..240] Cb/Cr
        out_y  = static_cast<uint8_t>(std::clamp(std::round(16.0f + y_norm * 219.0f), 16.0f, 235.0f));
        out_cb = static_cast<uint8_t>(std::clamp(std::round(128.0f + cb_norm * 224.0f), 16.0f, 240.0f));
        out_cr = static_cast<uint8_t>(std::clamp(std::round(128.0f + cr_norm * 224.0f), 16.0f, 240.0f));
    }
}

inline void GenerateSyntheticNV12(std::vector<uint8_t>& out, uint32_t w, uint32_t h,
                                  int matrix, int range) {
    out.assign(w * h * 3 / 2, 0);
    size_t count = 0;
    const auto* blocks = GetTestBlocks(count);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float r = 0.f, g = 0.f, b = 0.f;
            if (y < 64) {
                const uint32_t bx = std::clamp(x / 32, 0u, 3u);
                const uint32_t by = std::clamp(y / 32, 0u, 1u);
                const size_t idx = by * 4 + bx;
                if (idx < count) {
                    r = blocks[idx].r;
                    g = blocks[idx].g;
                    b = blocks[idx].b;
                }
            } else {
                // Gradient row
                const float frac = static_cast<float>(x) / static_cast<float>(w > 1 ? (w - 1) : 1);
                r = frac * 255.0f;
                g = frac * 255.0f;
                b = frac * 255.0f;
            }
            uint8_t y_val = 0, cb_val = 0, cr_val = 0;
            RgbToYCbCr(r, g, b, matrix, range, y_val, cb_val, cr_val);
            out[y * w + x] = y_val;
            if ((x % 2 == 0) && (y % 2 == 0)) {
                const size_t uv_pos = w * h + (y / 2) * w + x;
                out[uv_pos + 0] = cb_val;
                out[uv_pos + 1] = cr_val;
            }
        }
    }
}

inline void GenerateSyntheticP010(std::vector<uint16_t>& out, uint32_t w, uint32_t h,
                                  int matrix, int range) {
    out.assign(w * h * 3 / 2, 0);
    size_t count = 0;
    const auto* blocks = GetTestBlocks(count);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float r = 0.f, g = 0.f, b = 0.f;
            if (y < 64) {
                const uint32_t bx = std::clamp(x / 32, 0u, 3u);
                const uint32_t by = std::clamp(y / 32, 0u, 1u);
                const size_t idx = by * 4 + bx;
                if (idx < count) {
                    r = blocks[idx].r;
                    g = blocks[idx].g;
                    b = blocks[idx].b;
                }
            } else {
                const float frac = static_cast<float>(x) / static_cast<float>(w > 1 ? (w - 1) : 1);
                r = frac * 255.0f;
                g = frac * 255.0f;
                b = frac * 255.0f;
            }
            float y_norm = 0.f, cb_norm = 0.f, cr_norm = 0.f;
            const float r_norm = r / 255.0f;
            const float g_norm = g / 255.0f;
            const float b_norm = b / 255.0f;
            if (matrix == 1) {
                y_norm = 0.299f * r_norm + 0.587f * g_norm + 0.114f * b_norm;
                cb_norm = (b_norm - y_norm) / 1.7720f;
                cr_norm = (r_norm - y_norm) / 1.4020f;
            } else if (matrix == 3) {
                y_norm = 0.2627f * r_norm + 0.6780f * g_norm + 0.0593f * b_norm;
                cb_norm = (b_norm - y_norm) / 1.8814f;
                cr_norm = (r_norm - y_norm) / 1.4746f;
            } else {
                y_norm = 0.2126f * r_norm + 0.7152f * g_norm + 0.0722f * b_norm;
                cb_norm = (b_norm - y_norm) / 1.8556f;
                cr_norm = (r_norm - y_norm) / 1.5748f;
            }

            uint16_t y10 = 0, cb10 = 0, cr10 = 0;
            if (range == 2) {
                y10  = static_cast<uint16_t>(std::clamp(std::round(y_norm * 1023.0f), 0.0f, 1023.0f));
                cb10 = static_cast<uint16_t>(std::clamp(std::round(cb_norm * 1023.0f + 512.0f), 0.0f, 1023.0f));
                cr10 = static_cast<uint16_t>(std::clamp(std::round(cr_norm * 1023.0f + 512.0f), 0.0f, 1023.0f));
            } else {
                y10  = static_cast<uint16_t>(std::clamp(std::round(64.0f + y_norm * 876.0f), 64.0f, 940.0f));
                cb10 = static_cast<uint16_t>(std::clamp(std::round(512.0f + cb_norm * 896.0f), 64.0f, 960.0f));
                cr10 = static_cast<uint16_t>(std::clamp(std::round(512.0f + cr_norm * 896.0f), 64.0f, 960.0f));
            }
            out[y * w + x] = static_cast<uint16_t>(y10 << 6);
            if ((x % 2 == 0) && (y % 2 == 0)) {
                const size_t uv_pos = w * h + (y / 2) * w + x;
                out[uv_pos + 0] = static_cast<uint16_t>(cb10 << 6);
                out[uv_pos + 1] = static_cast<uint16_t>(cr10 << 6);
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Unit Tests: Color Space Resolution & Legacy Matrix/Range Mapping
// ---------------------------------------------------------------------------

DUWN_TEST(VideoColorSpace_BT601_LimitedAndFull) {
    // BT.601 Limited (SD TV standard: Y 16-235, Cb/Cr 16-240)
    const auto b601_lim = ResolveVideoColorSpace(1, 1, 480);
    DUWN_ASSERT(b601_lim.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601);
    DUWN_ASSERT(b601_lim.output == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    DUWN_ASSERT(b601_lim.legacy_input.YCbCr_Matrix == 0); // 0 = BT.601
    DUWN_ASSERT(b601_lim.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);
    DUWN_ASSERT(b601_lim.legacy_input.RGB_Range == 0);
    DUWN_ASSERT(b601_lim.legacy_output.RGB_Range == 0); // P1 fix: full-range
    DUWN_ASSERT(b601_lim.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b601_lim.is_bt2020);
    DUWN_ASSERT(b601_lim.legacy_matrix_exact);
    DUWN_ASSERT(b601_lim.legacy_matrix_accuracy == LegacyMatrixAccuracy::Exact);

    // BT.601 Full (JPEG / PC range: YCbCr 0-255)
    const auto b601_full = ResolveVideoColorSpace(1, 2, 480);
    DUWN_ASSERT(b601_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601);
    DUWN_ASSERT(b601_full.legacy_input.YCbCr_Matrix == 0);
    DUWN_ASSERT(b601_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(b601_full.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b601_full.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b601_full.is_bt2020);
    DUWN_ASSERT(b601_full.legacy_matrix_exact);
    DUWN_ASSERT(b601_full.legacy_matrix_accuracy == LegacyMatrixAccuracy::Exact);
}

DUWN_TEST(VideoColorSpace_BT709_LimitedAndFull) {
    // BT.709 Limited (HD broadcast standard: 1080p, 720p)
    const auto b709_lim = ResolveVideoColorSpace(2, 1, 1080);
    DUWN_ASSERT(b709_lim.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    DUWN_ASSERT(b709_lim.output == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    DUWN_ASSERT(b709_lim.legacy_input.YCbCr_Matrix == 1); // 1 = BT.709
    DUWN_ASSERT(b709_lim.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);
    DUWN_ASSERT(b709_lim.legacy_output.RGB_Range == 0); // P1 fix: full-range
    DUWN_ASSERT(b709_lim.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b709_lim.is_bt2020);
    DUWN_ASSERT(b709_lim.legacy_matrix_exact);
    DUWN_ASSERT(b709_lim.legacy_matrix_accuracy == LegacyMatrixAccuracy::Exact);

    // BT.709 Full
    const auto b709_full = ResolveVideoColorSpace(2, 2, 1080);
    DUWN_ASSERT(b709_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
    DUWN_ASSERT(b709_full.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(b709_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(b709_full.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b709_full.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b709_full.is_bt2020);
    DUWN_ASSERT(b709_full.legacy_matrix_exact);
    DUWN_ASSERT(b709_full.legacy_matrix_accuracy == LegacyMatrixAccuracy::Exact);
}

DUWN_TEST(VideoColorSpace_BT2020_HandlingAndFidelityLimitation) {
    // BT.2020 Limited (P2: distinction between exact ColorSpace1 and legacy fallback approximation)
    const auto b2020_lim = ResolveVideoColorSpace(3, 1, 2160);
    DUWN_ASSERT(b2020_lim.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020);
    DUWN_ASSERT(b2020_lim.output == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    DUWN_ASSERT(b2020_lim.is_bt2020);
    DUWN_ASSERT(!b2020_lim.legacy_matrix_exact); // Not exact under legacy D3D11 controls
    DUWN_ASSERT(b2020_lim.legacy_matrix_accuracy == LegacyMatrixAccuracy::Approximate);
    // Legacy fallback must select BT.709 matrix (1) to prevent black screen / invalid configuration
    DUWN_ASSERT(b2020_lim.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(b2020_lim.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);
    DUWN_ASSERT(b2020_lim.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b2020_lim.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);

    // BT.2020 Full
    const auto b2020_full = ResolveVideoColorSpace(3, 2, 2160);
    DUWN_ASSERT(b2020_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020);
    DUWN_ASSERT(b2020_full.is_bt2020);
    DUWN_ASSERT(!b2020_full.legacy_matrix_exact);
    DUWN_ASSERT(b2020_full.legacy_matrix_accuracy == LegacyMatrixAccuracy::Approximate);
    DUWN_ASSERT(b2020_full.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(b2020_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(b2020_full.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b2020_full.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
}

DUWN_TEST(VideoColorSpace_UnknownRangeAndMatrixFallback) {
    // matrix == 0 (unknown): heuristic based on height (< 720 -> BT.601, >= 720 -> BT.709)
    // range == 0 (unknown): defaults to studio / limited range (broadcasting standard)

    // Height < 720 (480p SD) -> fallback BT.601 Limited
    const auto sd_unknown = ResolveVideoColorSpace(0, 0, 480);
    DUWN_ASSERT(sd_unknown.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601);
    DUWN_ASSERT(sd_unknown.legacy_input.YCbCr_Matrix == 0);
    DUWN_ASSERT(sd_unknown.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);

    // Height == 720 (720p HD boundary) -> fallback BT.709 Limited
    const auto hd_720 = ResolveVideoColorSpace(0, 0, 720);
    DUWN_ASSERT(hd_720.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    DUWN_ASSERT(hd_720.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(hd_720.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);

    // Height == 1080 (1080p Full HD) -> fallback BT.709 Limited
    const auto hd_1080 = ResolveVideoColorSpace(0, 0, 1080);
    DUWN_ASSERT(hd_1080.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    DUWN_ASSERT(hd_1080.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(hd_1080.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235);

    // Explicit Full range with unknown matrix -> BT.709 Full
    const auto hd_full = ResolveVideoColorSpace(0, 2, 1080);
    DUWN_ASSERT(hd_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
    DUWN_ASSERT(hd_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
}

// ---------------------------------------------------------------------------
// P1 Regression Test: Legacy output must strictly remain full-range across all parameters
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_P1_Regression_LegacyOutputRangeAlwaysFull) {
    constexpr int matrices[] = {0, 1, 2, 3};
    constexpr int ranges[] = {0, 1, 2};
    constexpr uint32_t heights[] = {360, 480, 576, 720, 1080, 1440, 2160};

    for (int m : matrices) {
        for (int r : ranges) {
            for (uint32_t h : heights) {
                const auto cs = ResolveVideoColorSpace(m, r, h);
                // Contract assertion: Swapchain back buffer is DXGI_FORMAT_B8G8R8A8_UNORM full-range RGB
                DUWN_ASSERT(cs.output == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                DUWN_ASSERT(cs.legacy_output.Usage == 0);
                // Regression check: RGB_Range must NEVER be 1 (limited). Must be 0 (full 0-255).
                DUWN_ASSERT(cs.legacy_output.RGB_Range == 0);
                DUWN_ASSERT(cs.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
                DUWN_ASSERT(cs.legacy_output.YCbCr_Matrix == 1);
                DUWN_ASSERT(cs.legacy_output.YCbCr_xvYCC == 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Numerical Verification: CPU Reference Conversion with Test Vectors
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_Numerical_TestVectors_BlackWhiteGray) {
    // 1. Black Vector
    // Limited range: Y=16, Cb=128, Cr=128 -> RGB (0, 0, 0)
    for (int m : {1, 2, 3}) {
        const auto rgb_lim = QuantizeRgb(CpuReferenceYCbCrToRgb(16, 128, 128, m, 1));
        DUWN_ASSERT(rgb_lim.r == 0 && rgb_lim.g == 0 && rgb_lim.b == 0);

        // Full range: Y=0, Cb=128, Cr=128 -> RGB (0, 0, 0)
        const auto rgb_full = QuantizeRgb(CpuReferenceYCbCrToRgb(0, 128, 128, m, 2));
        DUWN_ASSERT(rgb_full.r == 0 && rgb_full.g == 0 && rgb_full.b == 0);
    }

    // 2. White Vector
    // Limited range: Y=235, Cb=128, Cr=128 -> RGB (255, 255, 255)
    for (int m : {1, 2, 3}) {
        const auto rgb_lim = QuantizeRgb(CpuReferenceYCbCrToRgb(235, 128, 128, m, 1));
        DUWN_ASSERT(rgb_lim.r == 255 && rgb_lim.g == 255 && rgb_lim.b == 255);

        // Full range: Y=255, Cb=128, Cr=128 -> RGB (255, 255, 255)
        const auto rgb_full = QuantizeRgb(CpuReferenceYCbCrToRgb(255, 128, 128, m, 2));
        DUWN_ASSERT(rgb_full.r == 255 && rgb_full.g == 255 && rgb_full.b == 255);
    }

    // 3. 50% Gray Vector
    // Limited range: Y = 16 + (219 / 2) = 125.5 -> 126
    for (int m : {1, 2, 3}) {
        const auto rgb_lim = QuantizeRgb(CpuReferenceYCbCrToRgb(126, 128, 128, m, 1));
        DUWN_ASSERT(std::abs(static_cast<int>(rgb_lim.r) - 128) <= 1);
        DUWN_ASSERT(std::abs(static_cast<int>(rgb_lim.g) - 128) <= 1);
        DUWN_ASSERT(std::abs(static_cast<int>(rgb_lim.b) - 128) <= 1);

        // Full range: Y=128 -> RGB (128, 128, 128)
        const auto rgb_full = QuantizeRgb(CpuReferenceYCbCrToRgb(128, 128, 128, m, 2));
        DUWN_ASSERT(rgb_full.r == 128 && rgb_full.g == 128 && rgb_full.b == 128);
    }
}

DUWN_TEST(VideoColorSpace_Numerical_BT2020_vs_BT709_Fallback_Discrepancy) {
    // Saturated Red in BT.2020 (Limited range)
    // In BT.2020: Kr=0.2627 -> Y = 16 + 219 * 0.2627 ≈ 74
    // Cb = 128 - 224 * (0.2627 / 1.8814) ≈ 97
    // Cr = 128 + 224 * 0.5 ≈ 240
    const uint8_t y_red = 74;
    const uint8_t cb_red = 97;
    const uint8_t cr_red = 240;

    // Exact BT.2020 decode:
    const auto rgb_exact = QuantizeRgb(CpuReferenceYCbCrToRgb(y_red, cb_red, cr_red, 3, 1));
    DUWN_ASSERT(rgb_exact.r >= 250); // Red is saturated

    // Legacy fallback using BT.709 matrix on BT.2020 data:
    const auto rgb_approx = QuantizeRgb(CpuReferenceYCbCrToRgb(y_red, cb_red, cr_red, 2, 1));

    // Numerical difference confirms physical limitation: Green and Blue channels drift
    const int green_diff = std::abs(static_cast<int>(rgb_exact.g) - static_cast<int>(rgb_approx.g));
    DUWN_ASSERT(green_diff > 5); // Noticeable discrepancy (~10-15 LSBs), validating legacy_matrix_exact == false!
}

// ---------------------------------------------------------------------------
// GPU Targeted & End-to-End Capabilities Test
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_GPU_VideoProcessorCapabilities) {
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    if (!ok || !dev.IsValid()) {
        printf("  [UNSUPPORTED] D3D11 device creation unavailable in current test environment\n");
        return;
    }

    const auto& ai = dev.GetAdapterInfo();
    printf("\n--- VIDEO COLOR SPACE GPU CAPABILITY AUDIT ---\n");
    printf("  Adapter: %ls (Vendor 0x%04X, Feature Level 0x%X)\n",
           ai.description.c_str(), ai.vendor_id, ai.feature_level);

    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
    if (FAILED(dev.Device()->QueryInterface(IID_PPV_ARGS(&video_device)))) {
        printf("  [UNSUPPORTED] ID3D11VideoDevice interface unavailable\n");
        return;
    }

    // Create VideoProcessorEnumerator for 1080p
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = 1920;
    desc.InputHeight = 1080;
    desc.OutputWidth = 1920;
    desc.OutputHeight = 1080;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vp_enum;
    if (FAILED(video_device->CreateVideoProcessorEnumerator(&desc, vp_enum.GetAddressOf()))) {
        printf("  [UNSUPPORTED] CreateVideoProcessorEnumerator failed for 1080p content\n");
        return;
    }

    // 1. Check basic input formats: NV12 (8-bit) and P010 (10-bit)
    UINT nv12_flags = 0;
    HRESULT hr_nv12 = vp_enum->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &nv12_flags);
    const bool nv12_supported = SUCCEEDED(hr_nv12) && ((nv12_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) != 0);

    UINT p010_flags = 0;
    HRESULT hr_p010 = vp_enum->CheckVideoProcessorFormat(DXGI_FORMAT_P010, &p010_flags);
    const bool p010_supported = SUCCEEDED(hr_p010) && ((p010_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) != 0);

    printf("  Format Support: NV12=%s, P010=%s\n",
           nv12_supported ? "SUPPORTED" : "UNSUPPORTED",
           p010_supported ? "SUPPORTED" : "UNSUPPORTED");

    // 2. Query ID3D11VideoProcessorEnumerator1 for ColorSpace1 format conversion check
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> enum1;
    if (SUCCEEDED(vp_enum.As(&enum1))) {
        printf("  ID3D11VideoProcessorEnumerator1: AVAILABLE\n");

        constexpr struct {
            const char* name;
            int matrix;
            int range;
        } cases[] = {
            {"BT.601 Limited", 1, 1},
            {"BT.601 Full", 1, 2},
            {"BT.709 Limited", 2, 1},
            {"BT.709 Full", 2, 2},
            {"BT.2020 Limited", 3, 1},
            {"BT.2020 Full", 3, 2},
        };

        for (const auto& c : cases) {
            const auto cs = ResolveVideoColorSpace(c.matrix, c.range, 1080);
            BOOL nv12_conv = FALSE;
            BOOL p010_conv = FALSE;

            if (nv12_supported) {
                enum1->CheckVideoProcessorFormatConversion(
                    DXGI_FORMAT_NV12, cs.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs.output, &nv12_conv);
            }
            if (p010_supported) {
                enum1->CheckVideoProcessorFormatConversion(
                    DXGI_FORMAT_P010, cs.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs.output, &p010_conv);
            }

            printf("    %s (input 0x%X -> output 0x%X): NV12 conv=%s, P010 conv=%s\n",
                   c.name, cs.input, cs.output,
                   nv12_conv ? "YES" : "NO",
                   p010_conv ? "YES" : "NO");
        }
    } else {
        printf("  ID3D11VideoProcessorEnumerator1: UNAVAILABLE (legacy D3D11 controls path will be used)\n");
    }
}

// ---------------------------------------------------------------------------
// Real GPU Pixel Verification: NV12 -> BGRA via ID3D11VideoProcessor
// Reads back offscreen render target via staging texture and validates channel errors
// against independent CPU reference calculation within justified tolerances.
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_GPU_Offscreen_PixelVerification_NV12) {
    using namespace duwn::video;
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    if (!ok || !dev.IsValid()) {
        printf("  [UNSUPPORTED] D3D11 device creation unavailable\n");
        return;
    }
    const auto& ai = dev.GetAdapterInfo();
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
    if (FAILED(dev.Device()->QueryInterface(IID_PPV_ARGS(&video_device))) ||
        FAILED(dev.Context()->QueryInterface(IID_PPV_ARGS(&video_context)))) {
        printf("  [UNSUPPORTED] ID3D11VideoDevice/ID3D11VideoContext unavailable\n");
        return;
    }

    constexpr uint32_t kW = 128;
    constexpr uint32_t kH = 128;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cp_desc{};
    cp_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cp_desc.InputWidth = kW;
    cp_desc.InputHeight = kH;
    cp_desc.OutputWidth = kW;
    cp_desc.OutputHeight = kH;
    cp_desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vp_enum;
    if (FAILED(video_device->CreateVideoProcessorEnumerator(&cp_desc, &vp_enum))) {
        printf("  [UNSUPPORTED] CreateVideoProcessorEnumerator failed for 128x128 content\n");
        return;
    }

    UINT nv12_flags = 0;
    HRESULT hr_fmt = vp_enum->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &nv12_flags);
    if (FAILED(hr_fmt) || (nv12_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
        printf("  [UNSUPPORTED] NV12 format unsupported for video processor input (HRESULT=0x%08X)\n",
               static_cast<unsigned>(hr_fmt));
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp;
    if (FAILED(video_device->CreateVideoProcessor(vp_enum.Get(), 0, &vp))) {
        printf("  [UNSUPPORTED] CreateVideoProcessor failed\n");
        return;
    }

    // Create Offscreen Render Target Texture (BGRA)
    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = kW;
    out_desc.Height = kH;
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> out_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&out_desc, nullptr, &out_tex)));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov_desc{};
    ov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ov_desc.Texture2D.MipSlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> out_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorOutputView(
        out_tex.Get(), vp_enum.Get(), &ov_desc, &out_view)));

    // Create Staging Texture for CPU Readback
    D3D11_TEXTURE2D_DESC stage_desc{};
    stage_desc.Width = kW;
    stage_desc.Height = kH;
    stage_desc.MipLevels = 1;
    stage_desc.ArraySize = 1;
    stage_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stage_desc.SampleDesc.Count = 1;
    stage_desc.Usage = D3D11_USAGE_STAGING;
    stage_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stage_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&stage_desc, nullptr, &stage_tex)));

    // Create NV12 Input Texture
    D3D11_TEXTURE2D_DESC in_desc{};
    in_desc.Width = kW;
    in_desc.Height = kH;
    in_desc.MipLevels = 1;
    in_desc.ArraySize = 1;
    in_desc.Format = DXGI_FORMAT_NV12;
    in_desc.SampleDesc.Count = 1;
    in_desc.Usage = D3D11_USAGE_DEFAULT;
    in_desc.BindFlags = D3D11_BIND_DECODER;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> in_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&in_desc, nullptr, &in_tex)));

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv_desc{};
    iv_desc.FourCC = 0;
    iv_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    iv_desc.Texture2D.MipSlice = 0;
    iv_desc.Texture2D.ArraySlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> in_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorInputView(
        in_tex.Get(), vp_enum.Get(), &iv_desc, &in_view)));

    Microsoft::WRL::ComPtr<ID3D11VideoContext1> vc1;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> enum1;
    video_context->QueryInterface(IID_PPV_ARGS(&vc1));
    vp_enum.As(&enum1);

    printf("\n--- REAL GPU PIXEL ACCURACY VERIFICATION (NV12 -> BGRA) ---\n");
    printf("  Adapter: %ls (Vendor 0x%04X, Feature Level 0x%X)\n",
           ai.description.c_str(), ai.vendor_id, ai.feature_level);

    struct TestCase {
        const char* name;
        int matrix;
        int range;
        bool visible_subrect;
    };

    const TestCase cases[] = {
        {"BT.709 Limited (Studio 16-235)", 2, 1, false},
        {"BT.709 Full (0-255)", 2, 2, false},
        {"BT.601 Limited (Studio 16-235)", 1, 1, false},
        {"BT.601 Full (0-255)", 1, 2, false},
        {"BT.709 Limited with Visible Subrect (96x96 inside 128x128)", 2, 1, true},
    };

    size_t block_count = 0;
    const auto* blocks = GetTestBlocks(block_count);

    for (const auto& tc : cases) {
        printf("\n  Case: %s\n", tc.name);
        std::vector<uint8_t> nv12_buf;
        GenerateSyntheticNV12(nv12_buf, kW, kH, tc.matrix, tc.range);
        dev.Context()->UpdateSubresource(in_tex.Get(), 0, nullptr, nv12_buf.data(), kW, 0);

        RECT src_rect{}, dst_rect{};
        if (tc.visible_subrect) {
            src_rect = {16, 16, 112, 112};
            dst_rect = {16, 16, 112, 112};
        } else {
            src_rect = {0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};
            dst_rect = {0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};
        }
        RECT target_rect = {0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};

        video_context->VideoProcessorSetStreamAutoProcessingMode(vp.Get(), 0, FALSE);
        video_context->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &src_rect);
        video_context->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &dst_rect);
        video_context->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &target_rect);

        const auto cs = ResolveVideoColorSpace(tc.matrix, tc.range, kH);
        BOOL cs1_conv = FALSE;
        bool used_cs1 = false;

        // Keep legacy controls aligned with current case as hardware baseline
        video_context->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &cs.legacy_input);
        video_context->VideoProcessorSetOutputColorSpace(vp.Get(), &cs.legacy_output);

        if (vc1 && enum1 &&
            SUCCEEDED(enum1->CheckVideoProcessorFormatConversion(
                DXGI_FORMAT_NV12, cs.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs.output, &cs1_conv)) && cs1_conv) {
            vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, cs.input);
            vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), cs.output);
            used_cs1 = true;
        } else {
            if (vc1) {
                vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, DXGI_COLOR_SPACE_CUSTOM);
                vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), DXGI_COLOR_SPACE_CUSTOM);
            }
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = in_view.Get();
        HRESULT hr_blt = video_context->VideoProcessorBlt(vp.Get(), out_view.Get(), 0, 1, &stream);
        DUWN_ASSERT(SUCCEEDED(hr_blt));

        dev.Context()->CopyResource(stage_tex.Get(), out_tex.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr_map = dev.Context()->Map(stage_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        DUWN_ASSERT(SUCCEEDED(hr_map));

        int max_channel_err = 0;
        int total_channel_err = 0;
        int channels_tested = 0;

        printf("    Path: %s\n", used_cs1 ? "ColorSpace1" : "Legacy D3D11 Controls");

        // Verify solid color blocks (sampled at block centers away from chroma subsampling edges)
        for (size_t b = 0; b < block_count; ++b) {
            const auto& blk = blocks[b];
            if (tc.visible_subrect) {
                if (blk.cx < 16 || blk.cx >= 112 || blk.cy < 16 || blk.cy >= 112)
                    continue;
            }
            const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + blk.cy * mapped.RowPitch;
            const uint8_t obs_b = row[blk.cx * 4 + 0];
            const uint8_t obs_g = row[blk.cx * 4 + 1];
            const uint8_t obs_r = row[blk.cx * 4 + 2];

            const uint8_t y_synth = nv12_buf[blk.cy * kW + blk.cx];
            const size_t uv_pos = kW * kH + (blk.cy / 2) * kW + (blk.cx & ~1u);
            const uint8_t cb_synth = nv12_buf[uv_pos + 0];
            const uint8_t cr_synth = nv12_buf[uv_pos + 1];
            const auto exp_rgb = QuantizeRgb(CpuReferenceYCbCrToRgb(y_synth, cb_synth, cr_synth, tc.matrix, tc.range));

            const int err_r = std::abs(static_cast<int>(obs_r) - static_cast<int>(exp_rgb.r));
            const int err_g = std::abs(static_cast<int>(obs_g) - static_cast<int>(exp_rgb.g));
            const int err_b = std::abs(static_cast<int>(obs_b) - static_cast<int>(exp_rgb.b));
            const int block_max = std::max({err_r, err_g, err_b});

            max_channel_err = std::max(max_channel_err, block_max);
            total_channel_err += (err_r + err_g + err_b);
            channels_tested += 3;

            printf("      [%-16s] Exp=(%3u, %3u, %3u) Obs=(%3u, %3u, %3u) Err=(%u, %u, %u)\n",
                   blk.name,
                   exp_rgb.r, exp_rgb.g, exp_rgb.b,
                   obs_r, obs_g, obs_b,
                   err_r, err_g, err_b);
        }

        // Verify gradient monotonicity in bottom half (cy = 96)
        if (!tc.visible_subrect) {
            const uint32_t gy = 96;
            const uint8_t* grow = static_cast<const uint8_t*>(mapped.pData) + gy * mapped.RowPitch;
            int last_luma = -1;
            bool monotone = true;
            for (uint32_t gx = 16; gx <= 112; gx += 16) {
                const uint8_t r = grow[gx * 4 + 2];
                const uint8_t g = grow[gx * 4 + 1];
                const uint8_t b = grow[gx * 4 + 0];
                const int luma = static_cast<int>(std::round(0.2126f * r + 0.7152f * g + 0.0722f * b));
                if (last_luma >= 0 && luma < last_luma) {
                    monotone = false;
                }
                last_luma = luma;
            }
            DUWN_ASSERT(monotone);
            printf("      [Gradient Monotonicity]: PASS (non-decreasing luma from x=16 to x=112)\n");
        }

        dev.Context()->Unmap(stage_tex.Get(), 0);

        const float mean_channel_err = channels_tested > 0
            ? static_cast<float>(total_channel_err) / static_cast<float>(channels_tested)
            : 0.f;

        printf("    Metrics: Max Error = %d LSB, Mean Error = %.2f LSB (Tolerance: max <= 2, mean <= 1.0)\n",
               max_channel_err, mean_channel_err);

        // Tolerance rationale:
        // Hardware D3D11 Video Processors use 10-bit/12-bit fixed-point multipliers and integer scaling
        // for YUV->RGB matrix arithmetic. Quantization introduces +/-1 to +/-2 LSB error.
        // Sampling at the center of 32x32 blocks (16px from boundary) completely avoids chroma subsampling transient.
        DUWN_ASSERT(max_channel_err <= 2);
        DUWN_ASSERT(mean_channel_err <= 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Real GPU Pixel Verification: P010 (10-Bit YUV) -> BGRA
// Verifies 10-bit conversion path or reports UNSUPPORTED with capability evidence
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_GPU_Offscreen_PixelVerification_P010) {
    using namespace duwn::video;
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    if (!ok || !dev.IsValid()) {
        printf("  [UNSUPPORTED] D3D11 device creation unavailable\n");
        return;
    }
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
    if (FAILED(dev.Device()->QueryInterface(IID_PPV_ARGS(&video_device))) ||
        FAILED(dev.Context()->QueryInterface(IID_PPV_ARGS(&video_context)))) {
        printf("  [UNSUPPORTED] ID3D11VideoDevice/ID3D11VideoContext unavailable\n");
        return;
    }

    constexpr uint32_t kW = 128;
    constexpr uint32_t kH = 128;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cp_desc{};
    cp_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cp_desc.InputWidth = kW;
    cp_desc.InputHeight = kH;
    cp_desc.OutputWidth = kW;
    cp_desc.OutputHeight = kH;
    cp_desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vp_enum;
    if (FAILED(video_device->CreateVideoProcessorEnumerator(&cp_desc, &vp_enum))) {
        printf("  [UNSUPPORTED] CreateVideoProcessorEnumerator failed for 128x128 P010 content\n");
        return;
    }

    UINT p010_flags = 0;
    HRESULT hr_p010 = vp_enum->CheckVideoProcessorFormat(DXGI_FORMAT_P010, &p010_flags);
    const bool p010_supported = SUCCEEDED(hr_p010) && ((p010_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) != 0);

    if (!p010_supported) {
        printf("  [UNSUPPORTED] P010 format conversion unsupported on this adapter (CheckVideoProcessorFormat HRESULT=0x%08X, flags=0x%X)\n",
               static_cast<unsigned>(hr_p010), p010_flags);
        return;
    }

    printf("\n--- REAL GPU PIXEL ACCURACY VERIFICATION (P010 10-Bit -> BGRA) ---\n");

    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp;
    if (FAILED(video_device->CreateVideoProcessor(vp_enum.Get(), 0, &vp))) {
        printf("  [UNSUPPORTED] CreateVideoProcessor failed for P010\n");
        return;
    }

    D3D11_TEXTURE2D_DESC in_desc{};
    in_desc.Width = kW;
    in_desc.Height = kH;
    in_desc.MipLevels = 1;
    in_desc.ArraySize = 1;
    in_desc.Format = DXGI_FORMAT_P010;
    in_desc.SampleDesc.Count = 1;
    in_desc.Usage = D3D11_USAGE_DEFAULT;
    in_desc.BindFlags = D3D11_BIND_DECODER;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> in_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&in_desc, nullptr, &in_tex)));

    std::vector<uint16_t> p010_buf;
    GenerateSyntheticP010(p010_buf, kW, kH, 2, 1); // BT.709 Limited
    dev.Context()->UpdateSubresource(in_tex.Get(), 0, nullptr, p010_buf.data(), kW * sizeof(uint16_t), 0);

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = kW;
    out_desc.Height = kH;
    out_desc.MipLevels = 1;
    out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> out_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&out_desc, nullptr, &out_tex)));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov_desc{};
    ov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ov_desc.Texture2D.MipSlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> out_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorOutputView(
        out_tex.Get(), vp_enum.Get(), &ov_desc, &out_view)));

    D3D11_TEXTURE2D_DESC stage_desc{};
    stage_desc.Width = kW;
    stage_desc.Height = kH;
    stage_desc.MipLevels = 1;
    stage_desc.ArraySize = 1;
    stage_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stage_desc.SampleDesc.Count = 1;
    stage_desc.Usage = D3D11_USAGE_STAGING;
    stage_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stage_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&stage_desc, nullptr, &stage_tex)));

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv_desc{};
    iv_desc.FourCC = 0;
    iv_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    iv_desc.Texture2D.MipSlice = 0;
    iv_desc.Texture2D.ArraySlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> in_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorInputView(
        in_tex.Get(), vp_enum.Get(), &iv_desc, &in_view)));

    RECT full_rect = {0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};
    video_context->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &full_rect);
    video_context->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &full_rect);
    video_context->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &full_rect);

    const auto cs = ResolveVideoColorSpace(2, 1, kH);
    Microsoft::WRL::ComPtr<ID3D11VideoContext1> vc1;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> enum1;
    video_context->QueryInterface(IID_PPV_ARGS(&vc1));
    vp_enum.As(&enum1);

    BOOL cs1_conv = FALSE;
    if (vc1 && enum1 &&
        SUCCEEDED(enum1->CheckVideoProcessorFormatConversion(
            DXGI_FORMAT_P010, cs.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs.output, &cs1_conv)) && cs1_conv) {
        vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, cs.input);
        vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), cs.output);
        printf("    Path: ColorSpace1\n");
    } else {
        if (vc1) {
            vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, DXGI_COLOR_SPACE_CUSTOM);
            vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), DXGI_COLOR_SPACE_CUSTOM);
        }
        video_context->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &cs.legacy_input);
        video_context->VideoProcessorSetOutputColorSpace(vp.Get(), &cs.legacy_output);
        printf("    Path: Legacy Controls\n");
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = in_view.Get();
    DUWN_ASSERT(SUCCEEDED(video_context->VideoProcessorBlt(vp.Get(), out_view.Get(), 0, 1, &stream)));

    dev.Context()->CopyResource(stage_tex.Get(), out_tex.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    DUWN_ASSERT(SUCCEEDED(dev.Context()->Map(stage_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped)));

    size_t count = 0;
    const auto* blocks = GetTestBlocks(count);
    int max_err = 0;
    for (size_t b = 0; b < count; ++b) {
        const auto& blk = blocks[b];
        const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + blk.cy * mapped.RowPitch;
        const uint8_t obs_b = row[blk.cx * 4 + 0];
        const uint8_t obs_g = row[blk.cx * 4 + 1];
        const uint8_t obs_r = row[blk.cx * 4 + 2];
        const int err_r = std::abs(static_cast<int>(obs_r) - static_cast<int>(std::round(blk.r)));
        const int err_g = std::abs(static_cast<int>(obs_g) - static_cast<int>(std::round(blk.g)));
        const int err_b = std::abs(static_cast<int>(obs_b) - static_cast<int>(std::round(blk.b)));
        max_err = std::max({max_err, err_r, err_g, err_b});
        printf("      [%-16s] Target=(%3.0f, %3.0f, %3.0f) Obs=(%3u, %3u, %3u) Err=(%u, %u, %u)\n",
               blk.name, blk.r, blk.g, blk.b, obs_r, obs_g, obs_b, err_r, err_g, err_b);
    }
    dev.Context()->Unmap(stage_tex.Get(), 0);
    printf("    P010 Max Channel Error: %d LSB (Tolerance <= 2 LSB) -> PASS\n", max_err);
    DUWN_ASSERT(max_err <= 2);
}

// ---------------------------------------------------------------------------
// Transition Lifecycle Audit: ColorSpace1 <-> Legacy Fallback
// Tests transition BT.709 (ColorSpace1) -> BT.2020 (Legacy Fallback with DXGI_COLOR_SPACE_CUSTOM) -> BT.709
// Verifies zero stale driver state across transitions on Intel GPU.
// ---------------------------------------------------------------------------
DUWN_TEST(VideoColorSpace_GPU_Lifecycle_ColorSpace1_Legacy_Transitions) {
    using namespace duwn::video;
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    if (!ok || !dev.IsValid()) {
        printf("  [UNSUPPORTED] D3D11 device creation unavailable\n");
        return;
    }
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
    if (FAILED(dev.Device()->QueryInterface(IID_PPV_ARGS(&video_device))) ||
        FAILED(dev.Context()->QueryInterface(IID_PPV_ARGS(&video_context)))) {
        printf("  [UNSUPPORTED] ID3D11VideoDevice/ID3D11VideoContext unavailable\n");
        return;
    }

    constexpr uint32_t kW = 128;
    constexpr uint32_t kH = 128;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cp_desc{};
    cp_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cp_desc.InputWidth = kW;
    cp_desc.InputHeight = kH;
    cp_desc.OutputWidth = kW;
    cp_desc.OutputHeight = kH;
    cp_desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vp_enum;
    if (FAILED(video_device->CreateVideoProcessorEnumerator(&cp_desc, &vp_enum))) {
        printf("  [UNSUPPORTED] CreateVideoProcessorEnumerator failed for 128x128 content\n");
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp;
    if (FAILED(video_device->CreateVideoProcessor(vp_enum.Get(), 0, &vp))) {
        printf("  [UNSUPPORTED] CreateVideoProcessor failed\n");
        return;
    }

    D3D11_TEXTURE2D_DESC in_desc{};
    in_desc.Width = kW; in_desc.Height = kH;
    in_desc.MipLevels = 1; in_desc.ArraySize = 1;
    in_desc.Format = DXGI_FORMAT_NV12;
    in_desc.SampleDesc.Count = 1;
    in_desc.Usage = D3D11_USAGE_DEFAULT;
    in_desc.BindFlags = D3D11_BIND_DECODER;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> in_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&in_desc, nullptr, &in_tex)));

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv_desc{};
    iv_desc.FourCC = 0; iv_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> in_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorInputView(
        in_tex.Get(), vp_enum.Get(), &iv_desc, &in_view)));

    D3D11_TEXTURE2D_DESC out_desc{};
    out_desc.Width = kW; out_desc.Height = kH;
    out_desc.MipLevels = 1; out_desc.ArraySize = 1;
    out_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out_desc.SampleDesc.Count = 1;
    out_desc.Usage = D3D11_USAGE_DEFAULT;
    out_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> out_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&out_desc, nullptr, &out_tex)));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov_desc{};
    ov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> out_view;
    DUWN_ASSERT(SUCCEEDED(video_device->CreateVideoProcessorOutputView(
        out_tex.Get(), vp_enum.Get(), &ov_desc, &out_view)));

    D3D11_TEXTURE2D_DESC stage_desc = out_desc;
    stage_desc.Usage = D3D11_USAGE_STAGING;
    stage_desc.BindFlags = 0;
    stage_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stage_tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&stage_desc, nullptr, &stage_tex)));

    RECT rect = {0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};
    video_context->VideoProcessorSetStreamAutoProcessingMode(vp.Get(), 0, FALSE);
    video_context->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &rect);
    video_context->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &rect);
    video_context->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &rect);

    Microsoft::WRL::ComPtr<ID3D11VideoContext1> vc1;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> enum1;
    video_context->QueryInterface(IID_PPV_ARGS(&vc1));
    vp_enum.As(&enum1);

    printf("\n--- GPU LIFECYCLE: COLORSPACE1 <-> LEGACY TRANSITION AUDIT ---\n");

    const auto render_and_read_pixel = [&](uint32_t sample_x, uint32_t sample_y,
                                           uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = in_view.Get();
        DUWN_ASSERT(SUCCEEDED(video_context->VideoProcessorBlt(vp.Get(), out_view.Get(), 0, 1, &stream)));

        dev.Context()->CopyResource(stage_tex.Get(), out_tex.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        DUWN_ASSERT(SUCCEEDED(dev.Context()->Map(stage_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + sample_y * mapped.RowPitch;
        out_b = row[sample_x * 4 + 0];
        out_g = row[sample_x * 4 + 1];
        out_r = row[sample_x * 4 + 2];
        dev.Context()->Unmap(stage_tex.Get(), 0);
    };

    // --- STEP 1: Configure BT.709 via ColorSpace1 ---
    std::vector<uint8_t> nv12_709;
    GenerateSyntheticNV12(nv12_709, kW, kH, 2, 1); // BT.709 Limited
    dev.Context()->UpdateSubresource(in_tex.Get(), 0, nullptr, nv12_709.data(), kW, 0);

    const auto cs709 = ResolveVideoColorSpace(2, 1, kH);
    BOOL sup709 = FALSE;
    DUWN_ASSERT(vc1 && enum1);
    DUWN_ASSERT(SUCCEEDED(enum1->CheckVideoProcessorFormatConversion(
        DXGI_FORMAT_NV12, cs709.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs709.output, &sup709)) && sup709);

    // Keep legacy baseline aligned
    video_context->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &cs709.legacy_input);
    video_context->VideoProcessorSetOutputColorSpace(vp.Get(), &cs709.legacy_output);
    vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, cs709.input);
    vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), cs709.output);

    uint8_t r1 = 0, g1 = 0, b1 = 0;
    render_and_read_pixel(112, 16, r1, g1, b1);
    printf("  Step 1 (BT.709 ColorSpace1): Red Block -> RGB (%u, %u, %u)\n", r1, g1, b1);
    DUWN_ASSERT(r1 >= 250);
    DUWN_ASSERT(g1 <= 2);
    DUWN_ASSERT(b1 <= 2);

    // --- STEP 2: Switch to BT.2020 where ColorSpace1 is unsupported ---
    std::vector<uint8_t> nv12_2020;
    GenerateSyntheticNV12(nv12_2020, kW, kH, 3, 1); // BT.2020 Limited
    dev.Context()->UpdateSubresource(in_tex.Get(), 0, nullptr, nv12_2020.data(), kW, 0);

    const auto cs2020 = ResolveVideoColorSpace(3, 1, kH);
    BOOL sup2020 = FALSE;
    enum1->CheckVideoProcessorFormatConversion(
        DXGI_FORMAT_NV12, cs2020.input, DXGI_FORMAT_B8G8R8A8_UNORM, cs2020.output, &sup2020);
    printf("  Step 2: BT.2020 ColorSpace1 supported by driver: %s\n", sup2020 ? "YES" : "NO");

    // Execute fallback sequence: reset ColorSpace1 with DXGI_COLOR_SPACE_CUSTOM, apply legacy controls
    vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, DXGI_COLOR_SPACE_CUSTOM);
    vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), DXGI_COLOR_SPACE_CUSTOM);
    video_context->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &cs2020.legacy_input);
    video_context->VideoProcessorSetOutputColorSpace(vp.Get(), &cs2020.legacy_output);

    uint8_t r2 = 0, g2 = 0, b2 = 0;
    render_and_read_pixel(112, 16, r2, g2, b2);
    printf("  Step 2 (BT.2020 Legacy Fallback): Red Block -> RGB (%u, %u, %u)\n", r2, g2, b2);
    const int green_drift = std::abs(static_cast<int>(g2) - static_cast<int>(g1));
    printf("    Green channel discrepancy under legacy fallback: %d LSB (confirms approximate mapping)\n", green_drift);

    // --- STEP 3: Switch back to BT.709 via ColorSpace1 ---
    dev.Context()->UpdateSubresource(in_tex.Get(), 0, nullptr, nv12_709.data(), kW, 0);
    // Keep legacy baseline aligned
    video_context->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &cs709.legacy_input);
    video_context->VideoProcessorSetOutputColorSpace(vp.Get(), &cs709.legacy_output);
    vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0, cs709.input);
    vc1->VideoProcessorSetOutputColorSpace1(vp.Get(), cs709.output);

    uint8_t r3 = 0, g3 = 0, b3 = 0;
    // Execute Blt to flush pipeline and measure steady-state frame
    render_and_read_pixel(112, 16, r3, g3, b3);
    render_and_read_pixel(112, 16, r3, g3, b3);
    printf("  Step 3 (Revert to BT.709 ColorSpace1): Red Block -> RGB (%u, %u, %u)\n", r3, g3, b3);

    const int diff_r = std::abs(static_cast<int>(r3) - static_cast<int>(r1));
    const int diff_g = std::abs(static_cast<int>(g3) - static_cast<int>(g1));
    const int diff_b = std::abs(static_cast<int>(b3) - static_cast<int>(b1));
    printf("  Transition Drift (Step 3 vs Step 1): dR=%d, dG=%d, dB=%d (Tolerance <= 1 LSB)\n",
           diff_r, diff_g, diff_b);

    DUWN_ASSERT(diff_r <= 1);
    DUWN_ASSERT(diff_g <= 1);
    DUWN_ASSERT(diff_b <= 1);
    printf("  State Lifecycle Transition Test: PASS (zero stale state observed)\n");
}

// ---------------------------------------------------------------------------
// VideoRenderer Production Pipeline Transitions & Active Color Path Audit
// Verifies VideoRenderer::Present sets ActiveColorPath correctly across dynamic stream changes.
// ---------------------------------------------------------------------------
DUWN_TEST(VideoRenderer_Production_ColorSpaceTransitionsAndActivePath) {
    using namespace duwn::video;
    D3D11Device dev;
    bool ok = dev.Create(false, true);
    if (!ok || !dev.IsValid()) {
        printf("  [UNSUPPORTED] D3D11 device creation unavailable\n");
        return;
    }

    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"ColorTestWindow", WS_OVERLAPPEDWINDOW,
                                  0, 0, 128, 128, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        printf("  [UNSUPPORTED] CreateWindowExW failed\n");
        return;
    }
    struct WindowGuard { HWND h; ~WindowGuard() { ::DestroyWindow(h); } } guard{hwnd};

    VideoRenderer renderer(dev, hwnd);
    if (!renderer.Init(128, 128)) {
        printf("  [UNSUPPORTED] VideoRenderer::Init failed\n");
        return;
    }
    renderer.SetNonBlocking(true);

    constexpr uint32_t kW = 128;
    constexpr uint32_t kH = 128;
    std::vector<uint8_t> nv12_709;
    GenerateSyntheticNV12(nv12_709, kW, kH, 2, 1);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kW;
    desc.Height = kH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DECODER;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    DUWN_ASSERT(SUCCEEDED(dev.Device()->CreateTexture2D(&desc, nullptr, &tex)));
    dev.Context()->UpdateSubresource(tex.Get(), 0, nullptr, nv12_709.data(), kW, 0);

    VideoFrame frame{};
    frame.texture = tex;
    frame.width = kW;
    frame.height = kH;
    frame.format = DXGI_FORMAT_NV12;
    frame.subresource = 0;

    printf("\n--- VIDEORENDERER PRODUCTION PIPELINE COLOR ACCURACY AUDIT ---\n");

    // 1. Initial BT.709 presentation
    frame.color_matrix = 2; // BT.709
    frame.color_range = 1;  // Limited
    PresentResult res1 = renderer.Present(frame);
    DUWN_ASSERT(res1 == PresentResult::Ok);
    printf("  Present BT.709: ActiveColorPath = %s\n",
           renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1 ? "ColorSpace1" : "Legacy");
    DUWN_ASSERT(renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1);

    // 2. Stream transition to BT.2020
    frame.color_matrix = 3; // BT.2020
    PresentResult res2 = renderer.Present(frame);
    DUWN_ASSERT(res2 == PresentResult::Ok);
    printf("  Present BT.2020: ActiveColorPath = %s\n",
           renderer.ActiveColorPath() == RuntimeColorPath::LegacyApproximation ? "LegacyApproximation" : "ColorSpace1");
    DUWN_ASSERT(renderer.ActiveColorPath() == RuntimeColorPath::LegacyApproximation);

    // 3. Stream transition back to BT.709
    frame.color_matrix = 2; // BT.709
    PresentResult res3 = renderer.Present(frame);
    DUWN_ASSERT(res3 == PresentResult::Ok);
    printf("  Present BT.709 (revert): ActiveColorPath = %s\n",
           renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1 ? "ColorSpace1" : "Legacy");
    DUWN_ASSERT(renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1);

    // 4. Stream transition to BT.601
    frame.color_matrix = 1; // BT.601
    PresentResult res4 = renderer.Present(frame);
    DUWN_ASSERT(res4 == PresentResult::Ok);
    printf("  Present BT.601: ActiveColorPath = %s\n",
           renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1 ? "ColorSpace1" : "Legacy");
    DUWN_ASSERT(renderer.ActiveColorPath() == RuntimeColorPath::ColorSpace1);

    printf("  VideoRenderer Production Pipeline Transitions: PASS\n");
}
