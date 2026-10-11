#pragma once

#include <d3d11.h>
#include <d3d11_1.h>

namespace duwn::video {

struct VideoColorSpace {
    DXGI_COLOR_SPACE_TYPE input;
    DXGI_COLOR_SPACE_TYPE output{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_input{};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE legacy_output{};
};

inline VideoColorSpace ResolveVideoColorSpace(int matrix, int range, uint32_t source_height) noexcept {
    if (matrix == 0) matrix = source_height >= 720 ? 2 : 1;
    const bool full = range == 2;
    DXGI_COLOR_SPACE_TYPE input = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    if (matrix == 1) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601
                                  : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    else if (matrix == 2) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
                                       : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    else if (matrix == 3) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020
                                       : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;

    VideoColorSpace result{.input = input};
    result.legacy_input.Usage = 0;
    result.legacy_input.RGB_Range = 0;
    // Legacy controls cannot express BT.2020. Matrix=1 is the documented 709/240 choice.
    result.legacy_input.YCbCr_Matrix = matrix != 1;
    result.legacy_input.YCbCr_xvYCC = 0;
    result.legacy_input.Nominal_Range = full ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
                                               : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    result.legacy_output.Usage = 0;
    result.legacy_output.RGB_Range = 1;
    result.legacy_output.YCbCr_Matrix = 1;
    result.legacy_output.YCbCr_xvYCC = 0;
    result.legacy_output.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    return result;
}

} // namespace duwn::video
