#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <cstdint>

namespace duwn::video {

// LegacyMatrixAccuracy describes the mathematical representability of the color matrix
// in legacy D3D11 video processor controls (which only provide BT.601 and BT.709 bits).
// This reflects source metadata capability under legacy mapping, NOT the runtime selected path
// (which may use modern ColorSpace1) nor measured pixel accuracy.
enum class LegacyMatrixAccuracy : uint8_t {
    Exact,       // BT.601 and BT.709 can be represented exactly in legacy D3D11 controls
    Approximate  // BT.2020 has no legacy representation; legacy fallback uses BT.709 approximation
};

struct VideoColorSpace {
    DXGI_COLOR_SPACE_TYPE input{};
    DXGI_COLOR_SPACE_TYPE output{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_input{};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_output{};
    bool is_bt2020{false};
    bool legacy_matrix_exact{true};
    LegacyMatrixAccuracy legacy_matrix_accuracy{LegacyMatrixAccuracy::Exact};
};

inline VideoColorSpace ResolveVideoColorSpace(int matrix, int range, uint32_t source_height) noexcept {
    if (matrix == 0) matrix = source_height >= 720 ? 2 : 1;
    const bool full = range == 2;
    DXGI_COLOR_SPACE_TYPE input = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    bool is_bt2020 = false;
    bool legacy_matrix_exact = true;
    LegacyMatrixAccuracy matrix_accuracy = LegacyMatrixAccuracy::Exact;

    if (matrix == 1) {
        input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601
                     : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    } else if (matrix == 2) {
        input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
                     : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    } else if (matrix == 3) {
        // BT.2020 assumes SDR G22 transfer function and BT.2020 primaries.
        // HDR tone-mapping (PQ / ST.2084 or HLG) is not performed in this pipeline.
        input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020
                     : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
        is_bt2020 = true;
        legacy_matrix_exact = false;
        matrix_accuracy = LegacyMatrixAccuracy::Approximate;
    }

    VideoColorSpace result{};
    result.input = input;
    result.output = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    result.is_bt2020 = is_bt2020;
    result.legacy_matrix_exact = legacy_matrix_exact;
    result.legacy_matrix_accuracy = matrix_accuracy;

    result.legacy_input.Usage = 0;
    result.legacy_input.RGB_Range = 0;
    // D3D11 legacy controls lack a BT.2020 matrix bit (0 = BT.601, 1 = BT.709/240M).
    // Matrix=1 is used as a compatibility fallback (marked by legacy_matrix_exact = false).
    result.legacy_input.YCbCr_Matrix = (matrix == 1) ? 0 : 1;
    result.legacy_input.YCbCr_xvYCC = 0;
    result.legacy_input.Nominal_Range = full ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
                                             : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;

    // P1 remediation: Legacy output must match full-range swapchain contract (DXGI_FORMAT_B8G8R8A8_UNORM).
    // RGB_Range 0 = full-range [0..255] (1 would compress output to limited [16..235], washing out contrast).
    result.legacy_output.Usage = 0;
    result.legacy_output.RGB_Range = 0;
    result.legacy_output.YCbCr_Matrix = 1;
    result.legacy_output.YCbCr_xvYCC = 0;
    result.legacy_output.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    return result;
}

} // namespace duwn::video
