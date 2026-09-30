#pragma once
// MFVideoDecoder — Media Foundation H.264/HEVC decoder.
// Input:  Annex-B H.264 or HEVC access units
//         OR AVCC-framed H.264 with SPS/PPS from SDP
// Output: ID3D11Texture2D NV12/P010 (GPU when the transform supports it)
//         via FrameReadyCallback
//
// Runs on the Video Decode Thread. Not thread-safe.

#include "VideoFrame.h"
#include "VideoGeometry.h"
#include "D3D11Device.h"
#include "EncodedAccessUnit.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include <array>
#include <cstdint>
#include <string>

namespace duwn::video {

using Microsoft::WRL::ComPtr;

// Called on the decode thread when a frame is ready. Must NOT block.
using FrameReadyCallback = std::function<void(VideoFrame)>;
enum class DecoderPreference { Auto, HardwareOnly, SoftwareOnly };

struct DecoderInfo {
    std::wstring name{L"Unknown"};
    bool         is_hardware{false};
    bool         is_async{false};
    bool         is_d3d11_aware{false};
    DXGI_FORMAT  output_format{DXGI_FORMAT_NV12};
    bool         is_zero_copy{false};
    std::wstring low_latency_status{L"unsupported"}; // "supported", "enabled", "unsupported"
};

struct DecoderConfig {
    uint32_t           width{1920};
    uint32_t           height{1080};
    VideoCodecType     codec{VideoCodecType::H264};
    DecoderPreference  preference{DecoderPreference::Auto};
    // SPS/PPS from AirPlay SDP (AVCC format). May be empty for Annex-B streams.
    std::vector<uint8_t> sps_pps_extra_data;
};

class MFVideoDecoder {
public:
    MFVideoDecoder(D3D11Device& device, FrameReadyCallback on_frame) noexcept;
    ~MFVideoDecoder();

    MFVideoDecoder(const MFVideoDecoder&) = delete;
    MFVideoDecoder& operator=(const MFVideoDecoder&) = delete;

    // Initialise MF transform. Must be called once before Feed().
    bool Init(const DecoderConfig& cfg) noexcept;

    // Submit one encoded access unit (Annex-B, or AVCC for H.264).
    // pts_ns: presentation timestamp in MonotonicClock nanoseconds (or 0 if unknown).
    // Returns false on unrecoverable error.
    bool Feed(const uint8_t* data, size_t size, int64_t pts_ns, uint16_t rtp_seq,
              bool has_sps = false, bool has_pps = false, bool has_idr = false,
              int64_t au_received_qpc = 0,
              int64_t rtp_arrival_qpc = 0) noexcept;

    // Flush pending frames (e.g. on orientation change / stream restart).
    void Flush() noexcept;

    bool IsHardware() const noexcept { return m_info.is_hardware; }
    const DecoderInfo& GetDecoderInfo() const noexcept { return m_info; }

    static bool IsCodecSupported(VideoCodecType codec, bool require_hardware = true) noexcept;

    // Reset after DXGI_ERROR_DEVICE_REMOVED.
    bool Reset(const DecoderConfig& cfg) noexcept;

private:
    bool DrainOutput() noexcept;
    bool HandleStreamChange() noexcept;
    bool SetInputType(const DecoderConfig& cfg) noexcept;
    bool SetOutputType() noexcept;
    AlignedAperture ResolveAperture(uint32_t texture_width, uint32_t texture_height) noexcept;
    bool EnsureUploadTexture(uint32_t width, uint32_t height) noexcept;
    bool InitInternal(const DecoderConfig& cfg, bool force_software) noexcept;

    D3D11Device&       m_device;
    FrameReadyCallback m_on_frame;
    ComPtr<IMFTransform> m_transform;
    DecoderInfo        m_info;
    DecoderConfig      m_config;
    uint32_t           m_width{0};
    uint32_t           m_height{0};
    LONGLONG           m_last_input_time{0}; // 100-ns MF time units

    // Upload texture for software decode / non-DXGI fallback (Tier 2 compatibility)
    ComPtr<ID3D11Texture2D> m_upload_texture;
    std::array<ComPtr<ID3D11Texture2D>, 6> m_upload_textures;
    size_t                  m_upload_index{0};
    uint32_t                m_upload_width{0};
    uint32_t                m_upload_height{0};
    DXGI_FORMAT             m_upload_format{DXGI_FORMAT_UNKNOWN};

    // Display aperture read from MF output type after SetOutputType().
    // Defines the visible region within the coded (padded) NV12 texture.
    // Both offsets and dims are even-aligned (NV12 4:2:0 chroma requirement).
    // Default: full coded dims (set in SetOutputType, overwritten if MF provides aperture).
    uint32_t           m_visible_x{0};
    uint32_t           m_visible_y{0};
    uint32_t           m_visible_width{0};
    uint32_t           m_visible_height{0};
    uint8_t            m_color_matrix{0};
    uint8_t            m_color_range{0};

    // Stored raw apertures from negotiated media type for fallback validation against actual texture
    MFVideoArea        m_min_display_aperture{};
    bool               m_has_min_display_aperture{false};
    MFVideoArea        m_geom_aperture{};
    bool               m_has_geom_aperture{false};
    MFVideoArea        m_pan_scan_aperture{};
    bool               m_has_pan_scan_aperture{false};
    bool               m_pan_scan_enabled{false};

    // Monotonically increasing format generation ID
    uint64_t           m_format_generation{1};

    // Track last logged texture dimensions to report discrepancies vs MF metadata once per change
    uint32_t           m_last_desc_width{0};
    uint32_t           m_last_desc_height{0};

    // End-to-end QPC and decode burst instrumentation (Phases 11 & 12)
    int64_t             m_cur_rtp_arrival_qpc{0};
    int64_t             m_cur_au_received_qpc{0};
    int64_t             m_cur_process_input_qpc{0};
    int64_t             m_last_decode_output_qpc{0};
    uint64_t            m_current_burst_run{0};
    std::vector<double> m_gap_samples;
};

} // namespace duwn::video
