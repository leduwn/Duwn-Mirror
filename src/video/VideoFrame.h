#pragma once
// VideoFrame — a decoded video frame backed by a D3D11 texture.
// Carries PTS and source metadata. Reference-counted via shared_ptr.
// The texture stays on GPU throughout the pipeline.
//
// Coded vs visible dimensions
// ----------------------------
// width/height = D3D11_TEXTURE2D_DESC.Width/Height = coded (padded) texture dims.
// The H.264 encoder pads the picture to macroblock boundaries (multiple of 16).
// The visible image is smaller; its position within the coded texture is
// given by the display aperture in the MF output media type:
//   MF_MT_MINIMUM_DISPLAY_APERTURE  (preferred — tight crop)
//   MF_MT_GEOMETRIC_APERTURE        (fallback)
//   MF_MT_PAN_SCAN_APERTURE / MF_MT_PAN_SCAN_ENABLED (optional pan/scan override)
//
// visible_width, visible_height, visible_x, visible_y are extracted from the
// aperture attribute after SetOutputType succeeds. If the attribute is absent
// (or read fails), they default to the coded dims with offset (0,0).
//
// Geometry functions (ComputeFitDestRect / ComputeFillSrcRect) must operate
// on visible_width × visible_height, not width × height.
//
// Video processor source rect for Fit mode = (visible_x, visible_y,
//   visible_x + visible_width, visible_y + visible_height).
// Video processor source rect for Fill mode = ComputeFillSrcRect offset by
//   (visible_x, visible_y) so cropping is within the visible area.

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <chrono>

namespace duwn::video {

using Microsoft::WRL::ComPtr;

struct VideoFrame {
    ComPtr<ID3D11Texture2D> texture;       // NV12 or BGRA depending on pipeline stage
    UINT                    subresource{0};
    ComPtr<IUnknown>        sample_retention; // Retains IMFMediaSample so MFT allocator does not recycle surface mid-presentation

    // Coded (padded) texture dimensions from D3D11_TEXTURE2D_DESC.
    uint32_t                width{0};
    uint32_t                height{0};

    // Visible (display aperture) dimensions within the coded texture.
    // visible_x, visible_y: top-left offset of the visible region (pixels).
    // Both are even-aligned (NV12 4:2:0 chroma requirement).
    // Default: visible_width=width, visible_height=height, offsets=0.
    uint32_t                visible_width{0};
    uint32_t                visible_height{0};
    uint32_t                visible_x{0};
    uint32_t                visible_y{0};
    uint8_t                 color_matrix{0}; // 0 unknown, 1 BT.601, 2 BT.709, 3 BT.2020
    uint8_t                 color_range{0};  // 0 unknown, 1 limited, 2 full

    DXGI_FORMAT             format{DXGI_FORMAT_UNKNOWN};

    // Monotonically increasing generation ID incremented on every real MF output stream change.
    // VideoProcessor, FrameScheduler, and OutputWindow use this to ensure coherent geometry
    // and reject stale frames from previous generations.
    uint64_t                format_generation{0};

    // Monotonically increasing sequence number assigned at decode time.
    uint64_t                sequence_number{0};

    // Decode arrival time in MonotonicClock nanoseconds.
    int64_t                 arrival_ns{0};

    // Presentation timestamp in MonotonicClock nanoseconds.
    // Set from RTP timestamp + clock anchor. Never wall-clock time.
    std::int64_t            pts_ns{0};

    // RTP sequence number of the last NAL unit in this frame (for diagnostics).
    uint16_t                rtp_sequence{0};

    // Whether pts_ns was synthesised from arrival time (anchor not yet available).
    bool                    pts_estimated{false};

    // End-to-End QPC lifecycle timestamps (Phase 12 / LatencyTelemetry T0-T7)
    int64_t                 rtp_arrival_qpc{0};    // T0: RTP packet arrival
    int64_t                 au_received_qpc{0};    // T1: AU assembled / received
    int64_t                 process_input_qpc{0};  // T2: Decoder input submitted
    int64_t                 process_output_qpc{0}; // T3: Decoder output sample available
    int64_t                 queue_push_qpc{0};     // T4: Decoded frame inserted into queue
    int64_t                 queue_pop_qpc{0};      // T5: Frame selected for presentation
    int64_t                 vp_begin_qpc{0};
    int64_t                 vp_end_qpc{0};        // T6: VideoProcessor blit completed
    int64_t                 present_begin_qpc{0};
    int64_t                 present_end_qpc{0};    // T7: Present() invoked / completed
};

} // namespace duwn::video
