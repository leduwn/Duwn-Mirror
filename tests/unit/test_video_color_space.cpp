#include "video/VideoColorSpace.h"
#include "video/D3D11Device.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

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
    DUWN_ASSERT(b601_lim.fidelity_status == ColorFidelityStatus::Exact);

    // BT.601 Full (JPEG / PC range: YCbCr 0-255)
    const auto b601_full = ResolveVideoColorSpace(1, 2, 480);
    DUWN_ASSERT(b601_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601);
    DUWN_ASSERT(b601_full.legacy_input.YCbCr_Matrix == 0);
    DUWN_ASSERT(b601_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(b601_full.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b601_full.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b601_full.is_bt2020);
    DUWN_ASSERT(b601_full.legacy_matrix_exact);
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
    DUWN_ASSERT(b709_lim.fidelity_status == ColorFidelityStatus::Exact);

    // BT.709 Full
    const auto b709_full = ResolveVideoColorSpace(2, 2, 1080);
    DUWN_ASSERT(b709_full.input == DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
    DUWN_ASSERT(b709_full.legacy_input.YCbCr_Matrix == 1);
    DUWN_ASSERT(b709_full.legacy_input.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(b709_full.legacy_output.RGB_Range == 0); // P1 fix
    DUWN_ASSERT(b709_full.legacy_output.Nominal_Range == D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255);
    DUWN_ASSERT(!b709_full.is_bt2020);
    DUWN_ASSERT(b709_full.legacy_matrix_exact);
}

DUWN_TEST(VideoColorSpace_BT2020_HandlingAndFidelityLimitation) {
    // BT.2020 Limited (P2: distinction between exact ColorSpace1 and legacy fallback approximation)
    const auto b2020_lim = ResolveVideoColorSpace(3, 1, 2160);
    DUWN_ASSERT(b2020_lim.input == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020);
    DUWN_ASSERT(b2020_lim.output == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    DUWN_ASSERT(b2020_lim.is_bt2020);
    DUWN_ASSERT(!b2020_lim.legacy_matrix_exact); // Not exact under legacy D3D11 controls
    DUWN_ASSERT(b2020_lim.fidelity_status == ColorFidelityStatus::LegacyApproximation);
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
    DUWN_ASSERT(b2020_full.fidelity_status == ColorFidelityStatus::LegacyApproximation);
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
