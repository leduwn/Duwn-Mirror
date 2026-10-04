#include "MFVideoDecoder.h"
#include "VideoGeometry.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include "common/telemetry/ConnectionTelemetry.h"
#include "common/telemetry/ConnectionTimeline.h"
#include <mfapi.h>
#include <mferror.h>
#include <strmif.h>
#include <codecapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <avrt.h>
#include <format>
#include <string>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "avrt.lib")

namespace duwn::video {

// MF time is in 100-nanosecond units.
constexpr LONGLONG kMFTimeUnitsPerSecond = 10'000'000LL;
// Convert MonotonicClock nanoseconds → MF LONGLONG (100-ns units)
constexpr LONGLONG NsToMF(int64_t ns) noexcept { return ns / 100LL; }

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

// Monotonic sequence counter for decoded video frames
static std::atomic<uint64_t> s_seq_counter{0};

MFVideoDecoder::MFVideoDecoder(D3D11Device& device, FrameReadyCallback on_frame) noexcept
    : m_device(device)
    , m_on_frame(std::move(on_frame)) {}

MFVideoDecoder::~MFVideoDecoder() {
    Flush();
    if (m_transform) m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
}

bool MFVideoDecoder::IsCodecSupported(VideoCodecType codec, bool require_hardware) noexcept {
    if (codec != VideoCodecType::H264 && codec != VideoCodecType::H265) return false;
    MFT_REGISTER_TYPE_INFO input_type{ MFMediaType_Video,
        codec == VideoCodecType::H265 ? MFVideoFormat_HEVC : MFVideoFormat_H264 };
    UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
    if (require_hardware) {
        flags |= MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_ASYNCMFT;
    }
    IMFActivate** activate_arr = nullptr;
    UINT32 activate_count = 0;
    HRESULT hr = ::MFTEnumEx(
        MFT_CATEGORY_VIDEO_DECODER,
        flags,
        &input_type,
        nullptr,
        &activate_arr,
        &activate_count
    );
    bool supported = SUCCEEDED(hr) && activate_count > 0;
    if (activate_arr) {
        for (UINT32 i = 0; i < activate_count; ++i) {
            if (activate_arr[i]) activate_arr[i]->Release();
        }
        ::CoTaskMemFree(activate_arr);
    }
    return supported;
}

bool MFVideoDecoder::Init(const DecoderConfig& cfg) noexcept {
    return InitInternal(cfg, cfg.preference == DecoderPreference::SoftwareOnly);
}

bool MFVideoDecoder::InitInternal(const DecoderConfig& cfg, bool force_software) noexcept {
    m_config = cfg;
    m_width  = cfg.width;
    m_height = cfg.height;
    m_info   = {};

    const bool hevc = cfg.codec == VideoCodecType::H265;
    MFT_REGISTER_TYPE_INFO input_type{ MFMediaType_Video,
        hevc ? MFVideoFormat_HEVC : MFVideoFormat_H264 };

    // Enumerate decoders
    UINT32 flags = 0;
    if (force_software) {
        flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
    } else {
        flags = MFT_ENUM_FLAG_HARDWARE
              | MFT_ENUM_FLAG_SYNCMFT
              | MFT_ENUM_FLAG_ASYNCMFT
              | MFT_ENUM_FLAG_LOCALMFT
              | MFT_ENUM_FLAG_SORTANDFILTER;
    }

    IMFActivate** activate_arr = nullptr;
    UINT32        activate_count = 0;

    HRESULT hr = ::MFTEnumEx(
        MFT_CATEGORY_VIDEO_DECODER,
        flags,
        &input_type,
        nullptr,
        &activate_arr,
        &activate_count
    );
    DUWN_LOG_INFOF("MFVideoDecoder",
        "MFTEnumEx(flags={:#x}, force_sw={}): hr={:#010x}, activate_count={}",
        flags, force_software ? "yes" : "no", static_cast<unsigned>(hr), activate_count);

    auto try_activate_transform = [this, &cfg, hevc](IMFActivate** arr, UINT32 count, bool is_hw) -> bool {
        for (UINT32 i = 0; i < count; ++i) {
            std::wstring mft_name = is_hw ? L"Hardware Video Decoder MFT" : L"Software Video Decoder MFT";
            WCHAR* name_buf = nullptr;
            UINT32 name_cch = 0;
            if (SUCCEEDED(arr[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name_buf, &name_cch)) && name_buf) {
                mft_name = name_buf;
                ::CoTaskMemFree(name_buf);
            }

            arr[i]->SetUINT32(MF_LOW_LATENCY, TRUE);

            ComPtr<IMFTransform> transform;
            HRESULT act_hr = arr[i]->ActivateObject(IID_PPV_ARGS(&transform));
            DUWN_LOG_INFOF("MFVideoDecoder",
                "MFT candidate [{}]: '{}', is_hw={}, ActivateObject hr={:#010x}",
                i, WideToUtf8(mft_name), is_hw ? "yes" : "no", static_cast<unsigned>(act_hr));
            if (FAILED(act_hr)) continue;

            // Query attributes & async status
            bool d3d_aware = false;
            bool is_async_mft = false;
            bool mf_low_latency_set = false;
            ComPtr<IMFAttributes> attrs;
            HRESULT attr_hr = transform->GetAttributes(attrs.GetAddressOf());
            DUWN_LOG_INFOF("MFVideoDecoder",
                "  GetAttributes hr={:#010x}", static_cast<unsigned>(attr_hr));

            if (attrs) {
                HRESULT ll_hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
                mf_low_latency_set = SUCCEEDED(ll_hr);
                DUWN_LOG_INFOF("MFVideoDecoder",
                    "  Set MF_LOW_LATENCY: hr={:#010x}", static_cast<unsigned>(ll_hr));

                UINT32 d3d11_val = 0;
                HRESULT d3d11_aware_hr = attrs->GetUINT32(MF_SA_D3D11_AWARE, &d3d11_val);
                DUWN_LOG_INFOF("MFVideoDecoder",
                    "  Query MF_SA_D3D11_AWARE: hr={:#010x}, val={}",
                    static_cast<unsigned>(d3d11_aware_hr), d3d11_val);

                UINT32 is_async_val = 0;
                HRESULT async_hr = attrs->GetUINT32(MF_TRANSFORM_ASYNC, &is_async_val);
                if (SUCCEEDED(async_hr) && is_async_val != 0) {
                    is_async_mft = true;
                    HRESULT unlock_hr = attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
                    DUWN_LOG_INFOF("MFVideoDecoder",
                        "  Transform is ASYNC; MF_TRANSFORM_ASYNC_UNLOCK hr={:#010x}",
                        static_cast<unsigned>(unlock_hr));
                } else {
                    DUWN_LOG_INFO("MFVideoDecoder", "  Transform is SYNCHRONOUS");
                }
            }

            // Wire up D3D11 device manager if available (applies to both hardware MFTs and DXVA-capable software/store MFTs)
            if (m_device.MFDevManager()) {
                HRESULT d3d_hr = transform->ProcessMessage(
                    MFT_MESSAGE_SET_D3D_MANAGER,
                    reinterpret_cast<ULONG_PTR>(m_device.MFDevManager()));
                d3d_aware = SUCCEEDED(d3d_hr);
                DUWN_LOG_INFOF("MFVideoDecoder",
                    "  ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER): hr={:#010x}, d3d_aware={}",
                    static_cast<unsigned>(d3d_hr), d3d_aware ? "yes" : "no");
            }

            if ((cfg.preference == DecoderPreference::HardwareOnly || hevc) && !d3d_aware) {
                if (hevc) {
                    DUWN_LOG_ERROR("MFVideoDecoder",
                        "HEVC decoder is not D3D11 aware; rejecting software/CPU-copy fallback per zero-copy release invariant");
                }
                continue;
            }
            m_transform = transform;
            if (!SetInputType(cfg)) {
                DUWN_LOG_WARN("MFVideoDecoder", "  SetInputType failed on candidate; trying next");
                m_transform.Reset();
                continue;
            }
            if (!SetOutputType()) {
                DUWN_LOG_WARN("MFVideoDecoder", "  SetOutputType failed on candidate; trying next");
                m_transform.Reset();
                continue;
            }

            m_info.name           = mft_name;
            m_info.is_hardware    = is_hw;
            m_info.is_async       = is_async_mft;
            m_info.is_d3d11_aware = d3d_aware;
            m_info.is_zero_copy   = d3d_aware;
            GlobalMetrics().video_decoder_kind.store(is_hw ? 1 : 2, std::memory_order_relaxed);
            GlobalMetrics().video_decoder_zero_copy.store(m_info.is_zero_copy, std::memory_order_relaxed);

            // Phase 3: Enable low latency mode via MF_LOW_LATENCY and ICodecAPI
            std::wstring low_lat_status = mf_low_latency_set ? L"enabled" : L"unsupported";
            ComPtr<ICodecAPI> codec_api;
            HRESULT hr_api = transform.As(&codec_api);
            if (SUCCEEDED(hr_api) && codec_api) {
                VARIANT val{};
                ::VariantInit(&val);
                val.vt = VT_UI4;
                val.ulVal = 1;
                HRESULT hr_low = codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &val);
                if (FAILED(hr_low)) {
                    val.vt = VT_BOOL;
                    val.boolVal = VARIANT_TRUE;
                    hr_low = codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &val);
                }
                if (SUCCEEDED(hr_low)) {
                    low_lat_status = L"enabled";
                    DUWN_LOG_INFOF("MFVideoDecoder",
                        "  CODECAPI_AVLowLatencyMode enabled successfully (hr={:#010x})",
                        static_cast<unsigned>(hr_low));
                } else {
                    if (!mf_low_latency_set) low_lat_status = L"supported_failed";
                    DUWN_LOG_WARNF("MFVideoDecoder",
                        "  SetValue(CODECAPI_AVLowLatencyMode) failed hr={:#010x} (MF_LOW_LATENCY={})",
                        static_cast<unsigned>(hr_low), mf_low_latency_set ? "ok" : "fail");
                }
            } else {
                DUWN_LOG_INFOF("MFVideoDecoder", "  ICodecAPI not supported on this transform (MF_LOW_LATENCY={})",
                               mf_low_latency_set ? "ok" : "fail");
            }
            m_info.low_latency_status = low_lat_status;

            DUWN_LOG_INFOF("MFVideoDecoder",
                "Decoder initialized: decoder_low_latency={}, decoder_d3d11_aware={}, decoder_zero_copy={}",
                WideToUtf8(m_info.low_latency_status),
                m_info.is_d3d11_aware ? "true" : "false",
                m_info.is_zero_copy ? "true" : "false");

            return true;
        }
        return false;
    };

    bool found = false;
    if (SUCCEEDED(hr) && activate_count > 0) {
        found = try_activate_transform(activate_arr, activate_count, /*is_hw=*/!force_software);
        for (UINT32 i = 0; i < activate_count; ++i) activate_arr[i]->Release();
        CoTaskMemFree(activate_arr);
        activate_arr = nullptr;
        activate_count = 0;
    }

    if (!found && !force_software && cfg.preference != DecoderPreference::HardwareOnly && !hevc) {
        DUWN_LOG_WARN("MFVideoDecoder",
                "Hardware video decoder unavailable; falling back to software decoder (Tier 2 Compatibility)");
        flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
        hr = ::MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            flags,
            &input_type,
            nullptr,
            &activate_arr,
            &activate_count
        );
        DUWN_LOG_INFOF("MFVideoDecoder",
            "Software MFTEnumEx: hr={:#010x}, activate_count={}",
            static_cast<unsigned>(hr), activate_count);
        if (SUCCEEDED(hr) && activate_count > 0) {
            found = try_activate_transform(activate_arr, activate_count, /*is_hw=*/false);
            for (UINT32 i = 0; i < activate_count; ++i) activate_arr[i]->Release();
            CoTaskMemFree(activate_arr);
            activate_arr = nullptr;
            activate_count = 0;
        }
    }

    if (!found || !m_transform) {
        DUWN_LOG_ERRORF("MFVideoDecoder", "No usable {} decoder MFT found", hevc ? "HEVC" : "H.264");
        return false;
    }

    HRESULT bstr_hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    HRESULT sstr_hr = m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    DUWN_LOG_INFOF("MFVideoDecoder",
        "Stream messages: BEGIN_STREAMING hr={:#010x}, START_OF_STREAM hr={:#010x}",
        static_cast<unsigned>(bstr_hr), static_cast<unsigned>(sstr_hr));

    DUWN_LOG_INFOF("MFVideoDecoder",
        "Decoder selected: {} (Codec={}, Type={}, Async={}, D3D11-Aware={}, Zero-Copy={}, Format={}, Resolution={}x{})",
        WideToUtf8(m_info.name),
        hevc ? "HEVC" : "H.264",
        m_info.is_hardware ? "Hardware" : "Software",
        m_info.is_async ? "Yes" : "No",
        m_info.is_d3d11_aware ? "Yes" : "No",
        m_info.is_zero_copy ? "Yes" : "No",
        m_info.output_format == DXGI_FORMAT_P010 ? "P010" : "NV12",
        m_width, m_height);

    airplay::ConnectionTelemetry::Get().RecordPhase(
        airplay::ConnectionPhase::DecoderCreated,
        std::format("{} ({})", WideToUtf8(m_info.name), m_info.is_hardware ? "Hardware" : "Software"));

    return true;
}

bool MFVideoDecoder::SetInputType(const DecoderConfig& cfg) noexcept {
    ComPtr<IMFMediaType> input_mt;
    HRESULT hr = ::MFCreateMediaType(input_mt.GetAddressOf());
    if (FAILED(hr)) return false;

    input_mt->SetGUID(MF_MT_MAJOR_TYPE,  MFMediaType_Video);
    input_mt->SetGUID(MF_MT_SUBTYPE,
        cfg.codec == VideoCodecType::H265 ? MFVideoFormat_HEVC : MFVideoFormat_H264);
    ::MFSetAttributeSize(input_mt.Get(), MF_MT_FRAME_SIZE, cfg.width, cfg.height);
    input_mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    input_mt->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, FALSE);

    // Attach SPS/PPS if we have them (AVCC streams from AirPlay)
    if (!cfg.sps_pps_extra_data.empty()) {
        hr = input_mt->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER,
                               cfg.sps_pps_extra_data.data(),
                               static_cast<UINT32>(cfg.sps_pps_extra_data.size()));
    }

    hr = m_transform->SetInputType(0, input_mt.Get(), 0);
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("MFVideoDecoder",
            "SetInputType failed {:#010x}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

bool MFVideoDecoder::SetOutputType() noexcept {
    // Enumerate output types offered by the transform and pick a renderable format.
    DWORD type_index = 0;
    ComPtr<IMFMediaType> candidate;
    while (SUCCEEDED(m_transform->GetOutputAvailableType(0, type_index++, candidate.GetAddressOf()))) {
        GUID subtype{};
        candidate->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype == MFVideoFormat_NV12 ||
            (m_config.codec == VideoCodecType::H265 && subtype == MFVideoFormat_P010)) {
            if (subtype == MFVideoFormat_P010) {
                ComPtr<ID3D11VideoDevice> video_device;
                ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
                D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
                desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
                desc.InputWidth = m_width;
                desc.InputHeight = m_height;
                desc.OutputWidth = m_width;
                desc.OutputHeight = m_height;
                desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
                UINT support = 0;
                if (!m_device.Device() ||
                    FAILED(m_device.Device()->QueryInterface(IID_PPV_ARGS(&video_device))) ||
                    FAILED(video_device->CreateVideoProcessorEnumerator(&desc, &enumerator)) ||
                    FAILED(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_P010, &support)) ||
                    !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
                    DUWN_LOG_WARN("MFVideoDecoder", "P010 VideoProcessor input unsupported; trying another output type");
                    candidate.Reset();
                    continue;
                }
            }
            HRESULT hr = m_transform->SetOutputType(0, candidate.Get(), 0);
            if (SUCCEEDED(hr)) {
                m_info.output_format = subtype == MFVideoFormat_P010
                    ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
                DUWN_LOG_INFOF("MFVideoDecoder", "Output type: {}",
                    m_info.output_format == DXGI_FORMAT_P010 ? "P010" : "NV12");

                // Read display aperture from the negotiated output type per MF display area rules:
                // 1. If MF_MT_PAN_SCAN_ENABLED == TRUE: prioritize MF_MT_PAN_SCAN_APERTURE.
                // 2. Otherwise: prioritize MF_MT_MINIMUM_DISPLAY_APERTURE.
                // 3. Fallback: MF_MT_GEOMETRIC_APERTURE.
                // 4. Default: full coded texture.
                // Decoder does NOT fail if these metadata attributes are absent.
                m_visible_x      = 0;
                m_visible_y      = 0;
                m_visible_width  = m_width;
                m_visible_height = m_height;
                m_color_matrix = 0;
                m_color_range = 0;

                m_has_min_display_aperture = false;
                m_has_geom_aperture        = false;
                m_has_pan_scan_aperture    = false;
                m_pan_scan_enabled         = false;

                // Query the actual output type (decoder may modify it)
                ComPtr<IMFMediaType> actual_out;
                if (SUCCEEDED(m_transform->GetOutputCurrentType(0, actual_out.GetAddressOf()))) {
                    UINT32 output_width = 0, output_height = 0;
                    if (SUCCEEDED(::MFGetAttributeSize(actual_out.Get(), MF_MT_FRAME_SIZE,
                                                       &output_width, &output_height)) &&
                        output_width > 0 && output_height > 0) {
                        m_width = output_width;
                        m_height = output_height;
                    }
                    UINT32 matrix = 0, range = 0;
                    if (SUCCEEDED(actual_out->GetUINT32(MF_MT_YUV_MATRIX, &matrix))) {
                        if (matrix == MFVideoTransferMatrix_BT601) m_color_matrix = 1;
                        else if (matrix == MFVideoTransferMatrix_BT709) m_color_matrix = 2;
                        else if (matrix == MFVideoTransferMatrix_BT2020_10 ||
                                 matrix == MFVideoTransferMatrix_BT2020_12) m_color_matrix = 3;
                    }
                    if (SUCCEEDED(actual_out->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range))) {
                        if (range == MFNominalRange_16_235) m_color_range = 1;
                        else if (range == MFNominalRange_0_255) m_color_range = 2;
                    }
                    UINT32 pan_scan_enabled = 0;
                    if (SUCCEEDED(actual_out->GetUINT32(MF_MT_PAN_SCAN_ENABLED, &pan_scan_enabled))) {
                        m_pan_scan_enabled = (pan_scan_enabled != 0);
                    }

                    if (SUCCEEDED(actual_out->GetBlob(MF_MT_PAN_SCAN_APERTURE,
                                                      reinterpret_cast<UINT8*>(&m_pan_scan_aperture),
                                                      sizeof(m_pan_scan_aperture), nullptr))) {
                        m_has_pan_scan_aperture = true;
                    }
                    if (SUCCEEDED(actual_out->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                                                      reinterpret_cast<UINT8*>(&m_min_display_aperture),
                                                      sizeof(m_min_display_aperture), nullptr))) {
                        m_has_min_display_aperture = true;
                    }
                    if (SUCCEEDED(actual_out->GetBlob(MF_MT_GEOMETRIC_APERTURE,
                                                      reinterpret_cast<UINT8*>(&m_geom_aperture),
                                                      sizeof(m_geom_aperture), nullptr))) {
                        m_has_geom_aperture = true;
                    }

                    AlignedAperture aligned = ResolveAperture(m_width, m_height);
                    m_visible_x      = aligned.x;
                    m_visible_y      = aligned.y;
                    m_visible_width  = aligned.width;
                    m_visible_height = aligned.height;

                    DUWN_LOG_INFOF("MFVideoDecoder",
                        "Aperture metadata negotiated: coded={}x{} visible={}x{}+{},{} (inward NV12 aligned)",
                        m_width, m_height,
                        m_visible_width, m_visible_height,
                        m_visible_x, m_visible_y);
                }

                return true;
            }
        }
        candidate.Reset();
    }
    DUWN_LOG_ERROR("MFVideoDecoder", "No supported NV12/P010 output type found");
    return false;
}

AlignedAperture MFVideoDecoder::ResolveAperture(uint32_t texture_width, uint32_t texture_height) noexcept {
    // Strict fallback hierarchy:
    // 1. Minimum display aperture (if present and valid against texture_width x texture_height and matching orientation)
    // 2. Geometric aperture (if present and valid against texture_width x texture_height and matching orientation)
    // 3. Pan-scan aperture (if enabled, present, and valid)
    // 4. Default to full texture dimensions (aligned to NV12 even bounds)

    auto try_aperture = [texture_width, texture_height](const MFVideoArea& area, const char* name)
        -> std::pair<bool, AlignedAperture> {
        int32_t raw_x = area.OffsetX.value;
        int32_t raw_y = area.OffsetY.value;
        int32_t raw_w = area.Area.cx;
        int32_t raw_h = area.Area.cy;

        if (raw_w <= 0 || raw_h <= 0) return {false, {}};

        // Validate orientation match: if texture is landscape, aperture must be landscape.
        // If texture is portrait, aperture must be portrait.
        if (!IsApertureOrientationMatching(static_cast<uint32_t>(raw_w), static_cast<uint32_t>(raw_h),
                                           texture_width, texture_height)) {
            DUWN_LOG_WARNF("MFVideoDecoder",
                "Aperture {} ({}x{}) orientation does not match texture ({}x{}); skipping",
                name, raw_w, raw_h, texture_width, texture_height);
            return {false, {}};
        }

        AlignedAperture aligned = AlignApertureInward(raw_x, raw_y, raw_w, raw_h, texture_width, texture_height);
        if (aligned.width == 0 || aligned.height == 0) return {false, {}};
        return {true, aligned};
    };

    if (m_pan_scan_enabled && m_has_pan_scan_aperture) {
        auto [ok, aligned] = try_aperture(m_pan_scan_aperture, "MF_MT_PAN_SCAN_APERTURE");
        if (ok) return aligned;
    }

    if (m_has_min_display_aperture) {
        auto [ok, aligned] = try_aperture(m_min_display_aperture, "MF_MT_MINIMUM_DISPLAY_APERTURE");
        if (ok) return aligned;
    }

    if (m_has_geom_aperture) {
        auto [ok, aligned] = try_aperture(m_geom_aperture, "MF_MT_GEOMETRIC_APERTURE");
        if (ok) return aligned;
    }

    // Fallback: full texture bounds aligned even
    return {0, 0, texture_width & ~1u, texture_height & ~1u};
}

bool MFVideoDecoder::Feed(const uint8_t* data, size_t size,
                          int64_t pts_ns, uint16_t rtp_seq,
                          bool has_sps, bool has_pps, bool has_idr,
                          int64_t au_received_qpc,
                          int64_t rtp_arrival_qpc) noexcept {
    GlobalMetrics().decoder_process_input_calls.fetch_add(1, std::memory_order_relaxed);

    if (!m_transform) return false;

    airplay::ConnectionTelemetry::Get().RecordPhase(
        airplay::ConnectionPhase::FirstH264Au,
        std::format("size={} B, seq={}, idr={}", size, rtp_seq, has_idr ? 1 : 0));
    telemetry::ConnectionTimeline::Get().Record(
        telemetry::ConnectionMilestone::C9_FirstCompleteAu,
        std::format("size={} B, seq={}, idr={}", size, rtp_seq, has_idr ? 1 : 0));

    // Join MMCSS "Playback" on decode thread (Phase 9)
    static thread_local bool s_mmcss_joined = false;
    if (!s_mmcss_joined) {
        s_mmcss_joined = true;
        DWORD task_idx = 0;
        HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Playback", &task_idx);
        if (mmcss) {
            DUWN_LOG_INFOF("MFVideoDecoder",
                "Decode thread {} joined MMCSS 'Playback' (task_index={})",
                ::GetCurrentThreadId(), task_idx);
        } else {
            DUWN_LOG_WARNF("MFVideoDecoder",
                "Failed to join MMCSS 'Playback' on decode thread {} (error={})",
                ::GetCurrentThreadId(), ::GetLastError());
        }
    }

    m_cur_au_received_qpc = au_received_qpc > 0 ? au_received_qpc : clock::MonotonicClock::NowQpcTicks();
    m_cur_rtp_arrival_qpc = rtp_arrival_qpc > 0 ? rtp_arrival_qpc : m_cur_au_received_qpc;
    m_cur_process_input_qpc = clock::MonotonicClock::NowQpcTicks();

    // Wrap data in an IMFSample
    ComPtr<IMFSample>       sample;
    ComPtr<IMFMediaBuffer>  buffer;

    HRESULT hr = ::MFCreateMemoryBuffer(static_cast<DWORD>(size), buffer.GetAddressOf());
    if (FAILED(hr)) {
        GlobalMetrics().decoder_process_input_failed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    BYTE* ptr = nullptr;
    DWORD max_len = 0, cur_len = 0;
    buffer->Lock(&ptr, &max_len, &cur_len);
    memcpy(ptr, data, size);
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(size));

    hr = ::MFCreateSample(sample.GetAddressOf());
    if (FAILED(hr)) {
        GlobalMetrics().decoder_process_input_failed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sample->AddBuffer(buffer.Get());

    // Set PTS in MF 100-ns units
    LONGLONG mf_pts = pts_ns ? NsToMF(pts_ns) : m_last_input_time;
    sample->SetSampleTime(mf_pts);
    sample->SetSampleDuration(NsToMF(1'000'000'000LL / 60)); // approx 60fps hint

    // Attach RTP sequence as custom attribute for tracing
    sample->SetUINT32(MFSampleExtension_Token, rtp_seq);

    m_last_input_time = mf_pts + NsToMF(1'000'000'000LL / 60);

    hr = m_transform->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        // Output not drained yet — pull output first
        DrainOutput();
        hr = m_transform->ProcessInput(0, sample.Get(), 0);
    }

    if (FAILED(hr)) {
        GlobalMetrics().decoder_process_input_failed.fetch_add(1, std::memory_order_relaxed);

        static std::atomic<bool> s_first_pi_fail{false};
        if (!s_first_pi_fail.exchange(true, std::memory_order_relaxed)) {
            const char* reason_str = "Unknown failure";
            if (hr == MF_E_NOTACCEPTING) reason_str = "MF_E_NOTACCEPTING (Transform cannot accept more input)";
            else if (hr == MF_E_INVALIDSTREAMNUMBER) reason_str = "MF_E_INVALIDSTREAMNUMBER";
            else if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) reason_str = "MF_E_TRANSFORM_TYPE_NOT_SET";
            else if (hr == E_INVALIDARG) reason_str = "E_INVALIDARG";

            DUWN_LOG_ERRORF("Diagnostics",
                "FIRST EVENT: ProcessInput failure: hr={:#010x} ({}) | AU size={} B, pts={}, has_sps={}, has_pps={}, has_idr={}",
                static_cast<unsigned>(hr), reason_str, size, pts_ns,
                has_sps ? "yes" : "no", has_pps ? "yes" : "no", has_idr ? "yes" : "no");
        }

        // Check if fallback to software decoder is warranted
        if (m_info.is_hardware && m_config.preference != DecoderPreference::HardwareOnly) {
            DUWN_LOG_WARNF("MFVideoDecoder",
                "ProcessInput failed on hardware MFT {:#010x}; triggering fallback to software MFT (Tier 2 Compatibility)",
                static_cast<unsigned>(hr));
            if (InitInternal(m_config, /*force_software=*/true)) {
                // Retry ProcessInput once on software MFT
                hr = m_transform->ProcessInput(0, sample.Get(), 0);
                if (SUCCEEDED(hr)) {
                    GlobalMetrics().decoder_process_input_success.fetch_add(1, std::memory_order_relaxed);
                    DrainOutput();
                    return true;
                }
            }
        }
        return false;
    }

    GlobalMetrics().decoder_process_input_success.fetch_add(1, std::memory_order_relaxed);
    DrainOutput();
    return true;
}

bool MFVideoDecoder::EnsureUploadTexture(uint32_t width, uint32_t height) noexcept {
    const DXGI_FORMAT format = m_info.output_format;
    if (m_upload_textures[0] && m_upload_width == width &&
        m_upload_height == height && m_upload_format == format) {
        m_upload_index = (m_upload_index + 1) % m_upload_textures.size();
        m_upload_texture = m_upload_textures[m_upload_index];
        return true;
    }
    m_upload_texture.Reset();
    for (auto& texture : m_upload_textures) texture.Reset();
    if (!m_device.Device()) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width              = width;
    desc.Height             = height;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = format;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = m_device.IsHardware()
        ? D3D11_BIND_DECODER | D3D11_BIND_RENDER_TARGET
        : D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags     = 0;

    for (auto& texture : m_upload_textures) {
        HRESULT hr = m_device.Device()->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
        if (FAILED(hr)) {
            DUWN_LOG_ERRORF("MFVideoDecoder",
                "CreateTexture2D for upload failed HRESULT={:#010x}", static_cast<unsigned>(hr));
            return false;
        }
    }
    m_upload_index = 0;
    m_upload_texture = m_upload_textures[0];
    m_upload_width  = width;
    m_upload_height = height;
    m_upload_format = format;
    return true;
}

bool MFVideoDecoder::HandleStreamChange() noexcept {
    DUWN_LOG_INFO("MFVideoDecoder", "Stream change detected; renegotiating output type");

    // Increment format generation ID on real stream renegotiation
    ++m_format_generation;
    GlobalMetrics().video_format_generation.store(m_format_generation, std::memory_order_relaxed);

    // Renegotiate output type
    if (!SetOutputType()) {
        DUWN_LOG_ERROR("MFVideoDecoder", "Renegotiating output type failed on stream change");
        return false;
    }

    // Read new current output type to retrieve updated resolution & visible aperture
    ComPtr<IMFMediaType> cur_type;
    if (SUCCEEDED(m_transform->GetOutputCurrentType(0, cur_type.GetAddressOf()))) {
        UINT32 new_w = 0, new_h = 0;
        if (SUCCEEDED(::MFGetAttributeSize(cur_type.Get(), MF_MT_FRAME_SIZE, &new_w, &new_h))) {
            m_width  = new_w;
            m_height = new_h;
        }

        UINT32 fps_num = 0, fps_den = 1;
        ::MFGetAttributeRatio(cur_type.Get(), MF_MT_FRAME_RATE, &fps_num, &fps_den);

        DUWN_LOG_INFOF("MFVideoDecoder",
            "Stream format negotiated: coded={}x{}, fps={}/{} ({:.2f}), visible={}x{} at +{}+{}, format={}",
            m_width, m_height, fps_num, fps_den,
            fps_den > 0 ? static_cast<double>(fps_num) / fps_den : 0.0,
            m_visible_width, m_visible_height, m_visible_x, m_visible_y,
            m_info.output_format == DXGI_FORMAT_P010 ? "P010" : "NV12");

        GlobalMetrics().video_coded_width.store(m_width, std::memory_order_relaxed);
        GlobalMetrics().video_coded_height.store(m_height, std::memory_order_relaxed);
        GlobalMetrics().video_visible_width.store(m_visible_width, std::memory_order_relaxed);
        GlobalMetrics().video_visible_height.store(m_visible_height, std::memory_order_relaxed);

        telemetry::ConnectionTimeline::Get().Record(
            telemetry::ConnectionMilestone::C10_DecoderConfigured,
            std::format("coded={}x{}, visible={}x{} at +{}+{}, format={}",
                        m_width, m_height, m_visible_width, m_visible_height,
                        m_visible_x, m_visible_y,
                        m_info.output_format == DXGI_FORMAT_P010 ? "P010" : "NV12"));
    }
    return true;
}

bool MFVideoDecoder::DrainOutput() noexcept {
    if (!m_transform) return false;

    MFT_OUTPUT_STREAM_INFO stream_info{};
    HRESULT hr = m_transform->GetOutputStreamInfo(0, &stream_info);
    if (FAILED(hr)) return false;

    bool provides_samples = (stream_info.dwFlags &
        (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;

    while (true) {
        GlobalMetrics().decoder_process_output_calls.fetch_add(1, std::memory_order_relaxed);

        MFT_OUTPUT_DATA_BUFFER output_buf{};
        DWORD status = 0;

        ComPtr<IMFSample> custom_sample;
        if (!provides_samples) {
            DWORD cb_size = stream_info.cbSize;
            if (cb_size == 0) {
                cb_size = m_width * m_height * 3u *
                    (m_info.output_format == DXGI_FORMAT_P010 ? 2u : 1u) / 2u;
            }
            ComPtr<IMFMediaBuffer> temp_buf;
            hr = ::MFCreateMemoryBuffer(cb_size, temp_buf.GetAddressOf());
            if (SUCCEEDED(hr)) {
                hr = ::MFCreateSample(custom_sample.GetAddressOf());
                if (SUCCEEDED(hr)) {
                    custom_sample->AddBuffer(temp_buf.Get());
                    output_buf.pSample = custom_sample.Get();
                }
            }
        }

        hr = m_transform->ProcessOutput(0, 1, &output_buf, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            GlobalMetrics().decoder_need_more_input.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            GlobalMetrics().decoder_stream_change.fetch_add(1, std::memory_order_relaxed);
            if (output_buf.pSample && provides_samples) output_buf.pSample->Release();
            if (output_buf.pEvents) output_buf.pEvents->Release();

            if (!HandleStreamChange()) {
                break;
            }
            // Update stream_info after stream change
            hr = m_transform->GetOutputStreamInfo(0, &stream_info);
            if (FAILED(hr)) break;
            provides_samples = (stream_info.dwFlags &
                (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            continue; // Keep draining with new format
        }

        if (FAILED(hr)) {
            GlobalMetrics().decoder_output_failed.fetch_add(1, std::memory_order_relaxed);
            DUWN_LOG_ERRORF("MFVideoDecoder",
                "ProcessOutput failed {:#010x}", static_cast<unsigned>(hr));
            if (output_buf.pSample && provides_samples) output_buf.pSample->Release();
            if (output_buf.pEvents) output_buf.pEvents->Release();
            break;
        }

        GlobalMetrics().decoder_process_output_success.fetch_add(1, std::memory_order_relaxed);

        if (output_buf.pEvents) output_buf.pEvents->Release();

        int64_t process_output_qpc = clock::MonotonicClock::NowQpcTicks();

        // Phase 11: Decode burst instrumentation & output gap bucketing
        if (m_last_decode_output_qpc > 0) {
            double gap_ms = clock::MonotonicClock::QpcDeltaMs(m_last_decode_output_qpc, process_output_qpc);
            GlobalMetrics().last_decode_output_gap_ms.store(gap_ms, std::memory_order_relaxed);

            if (gap_ms < 1.0) {
                GlobalMetrics().decode_gap_lt_1ms.fetch_add(1, std::memory_order_relaxed);
            } else if (gap_ms < 3.0) {
                GlobalMetrics().decode_gap_1_3ms.fetch_add(1, std::memory_order_relaxed);
            } else if (gap_ms < 8.0) {
                GlobalMetrics().decode_gap_3_8ms.fetch_add(1, std::memory_order_relaxed);
            } else if (gap_ms < 14.0) {
                GlobalMetrics().decode_gap_8_14ms.fetch_add(1, std::memory_order_relaxed);
            } else if (gap_ms < 20.0) {
                GlobalMetrics().decode_gap_14_20ms.fetch_add(1, std::memory_order_relaxed);
            } else {
                GlobalMetrics().decode_gap_gt_20ms.fetch_add(1, std::memory_order_relaxed);
            }

            if (gap_ms < 3.0) {
                m_current_burst_run++;
                if (m_current_burst_run == 2) {
                    GlobalMetrics().burst_2_frames.fetch_add(1, std::memory_order_relaxed);
                } else if (m_current_burst_run == 3) {
                    GlobalMetrics().burst_3_frames.fetch_add(1, std::memory_order_relaxed);
                }
                uint64_t cur_max = GlobalMetrics().burst_max.load(std::memory_order_relaxed);
                while (m_current_burst_run > cur_max &&
                       !GlobalMetrics().burst_max.compare_exchange_weak(cur_max, m_current_burst_run, std::memory_order_relaxed)) {
                }
            } else {
                m_current_burst_run = 1;
            }

            m_gap_samples.push_back(gap_ms);
            if (m_gap_samples.size() >= 60) {
                std::vector<double> sorted = m_gap_samples;
                std::sort(sorted.begin(), sorted.end());
                GlobalMetrics().decode_output_gap_p50.store(sorted[sorted.size() / 2], std::memory_order_relaxed);
                size_t p95_idx = static_cast<size_t>(sorted.size() * 0.95);
                if (p95_idx >= sorted.size()) p95_idx = sorted.size() - 1;
                GlobalMetrics().decode_output_gap_p95.store(sorted[p95_idx], std::memory_order_relaxed);
                m_gap_samples.clear();
            }
        } else {
            m_current_burst_run = 1;
        }
        m_last_decode_output_qpc = process_output_qpc;

        ComPtr<IMFSample> out_sample;
        if (provides_samples) {
            if (!output_buf.pSample) continue;
            out_sample.Attach(output_buf.pSample);
        } else {
            out_sample = custom_sample;
            if (!out_sample) continue;
        }

        // Extract buffer from sample
        ComPtr<IMFMediaBuffer> buf;
        out_sample->GetBufferByIndex(0, buf.GetAddressOf());
        if (!buf) continue;

        ComPtr<IMFDXGIBuffer> dxgi_buf;
        if (SUCCEEDED(buf.As(&dxgi_buf))) {
            // Direct GPU texture (Zero-Copy Full Performance path)
            m_info.is_zero_copy = true;
            GlobalMetrics().video_decoder_zero_copy.store(true, std::memory_order_relaxed);

            ComPtr<ID3D11Texture2D> tex;
            UINT subresource = 0;
            dxgi_buf->GetResource(IID_PPV_ARGS(&tex));
            dxgi_buf->GetSubresourceIndex(&subresource);

            LONGLONG sample_time = 0;
            out_sample->GetSampleTime(&sample_time);

            D3D11_TEXTURE2D_DESC desc{};
            tex->GetDesc(&desc);
            if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) {
                DUWN_LOG_ERRORF("MFVideoDecoder", "Unsupported decoded texture format {}",
                    static_cast<unsigned>(desc.Format));
                continue;
            }

            if (desc.Width != m_last_desc_width || desc.Height != m_last_desc_height) {
                m_last_desc_width  = desc.Width;
                m_last_desc_height = desc.Height;
                if (m_width != desc.Width || m_height != desc.Height) {
                    DUWN_LOG_INFOF("MFVideoDecoder",
                        "Media type coded={}x{} | Actual output texture={}x{}",
                        m_width, m_height, desc.Width, desc.Height);
                }
            }

            AlignedAperture aligned = ResolveAperture(desc.Width, desc.Height);

            GlobalMetrics().video_coded_width.store(desc.Width, std::memory_order_relaxed);
            GlobalMetrics().video_coded_height.store(desc.Height, std::memory_order_relaxed);
            GlobalMetrics().video_visible_width.store(aligned.width, std::memory_order_relaxed);
            GlobalMetrics().video_visible_height.store(aligned.height, std::memory_order_relaxed);

            VideoFrame frame;
            frame.texture           = tex;
            frame.subresource       = subresource;
            frame.sample_retention  = out_sample; // Retain surface reference so MFT does not recycle mid-presentation
            frame.width             = desc.Width;
            frame.height            = desc.Height;
            frame.visible_width     = aligned.width;
            frame.visible_height    = aligned.height;
            frame.visible_x         = aligned.x;
            frame.visible_y         = aligned.y;
            frame.color_matrix      = m_color_matrix;
            frame.color_range       = m_color_range;
            frame.format            = desc.Format;
            frame.format_generation = m_format_generation;
            frame.sequence_number   = ++s_seq_counter;
            frame.arrival_ns        = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
            frame.pts_ns            = sample_time * 100LL;
            frame.rtp_arrival_qpc   = m_cur_rtp_arrival_qpc;
            frame.au_received_qpc   = m_cur_au_received_qpc;
            frame.process_input_qpc = m_cur_process_input_qpc;
            frame.process_output_qpc= process_output_qpc;

            GlobalMetrics().video_decoded_frames.fetch_add(1, std::memory_order_relaxed);

            static std::atomic<bool> s_first_decoded{false};
            if (!s_first_decoded.exchange(true, std::memory_order_relaxed)) {
                DUWN_LOG_INFOF("Diagnostics",
                    "FIRST EVENT: Video frame decoded: coded={}x{}, visible={}x{} at +{}+{}, DXGI_FORMAT={}, subresource={}, pts={}, decoder={}, zero_copy={}",
                    desc.Width, desc.Height, aligned.width, aligned.height, aligned.x, aligned.y,
                    desc.Format == DXGI_FORMAT_P010 ? "P010" : "NV12",
                    subresource, frame.pts_ns,
                    m_info.is_hardware ? "Hardware" : "Software",
                    m_info.is_zero_copy ? "yes" : "no");
                airplay::ConnectionTelemetry::Get().RecordPhase(
                    airplay::ConnectionPhase::FirstDecodedFrame,
                    std::format("{}x{} {} Zero-Copy", desc.Width, desc.Height,
                        desc.Format == DXGI_FORMAT_P010 ? "P010" : "NV12"));
                telemetry::ConnectionTimeline::Get().Record(
                    telemetry::ConnectionMilestone::C11_FirstDecodedTexture,
                    std::format("{}x{} {} Zero-Copy", desc.Width, desc.Height,
                        desc.Format == DXGI_FORMAT_P010 ? "P010" : "NV12"));
            }

            if (m_on_frame) m_on_frame(std::move(frame));
        } else {
            // System memory buffer (Software Decode / Compatibility path)
            m_info.is_zero_copy = false;
            GlobalMetrics().video_decoder_zero_copy.store(false, std::memory_order_relaxed);

            BYTE* ptr = nullptr;
            DWORD max_len = 0, cur_len = 0;
            if (SUCCEEDED(buf->Lock(&ptr, &max_len, &cur_len)) && ptr) {
                const DXGI_FORMAT format = m_info.output_format;
                const UINT row_pitch = m_width * (format == DXGI_FORMAT_P010 ? 2u : 1u);
                const UINT expected_bytes = row_pitch * m_height * 3u / 2u;
                if (cur_len >= expected_bytes && EnsureUploadTexture(m_width, m_height)) {
                    {
                        std::lock_guard lock{m_device.ContextMutex()};
                        m_device.Context()->UpdateSubresource(
                            m_upload_texture.Get(), 0, nullptr, ptr, row_pitch, expected_bytes);
                    }
                    buf->Unlock();

                    LONGLONG sample_time = 0;
                    out_sample->GetSampleTime(&sample_time);

                    AlignedAperture aligned = ResolveAperture(m_width, m_height);

                    GlobalMetrics().video_coded_width.store(m_width, std::memory_order_relaxed);
                    GlobalMetrics().video_coded_height.store(m_height, std::memory_order_relaxed);
                    GlobalMetrics().video_visible_width.store(aligned.width, std::memory_order_relaxed);
                    GlobalMetrics().video_visible_height.store(aligned.height, std::memory_order_relaxed);

                    VideoFrame frame;
                    frame.texture           = m_upload_texture;
                    frame.subresource       = 0;
                    frame.sample_retention  = out_sample;
                    frame.width             = m_width;
                    frame.height            = m_height;
                    frame.visible_width     = aligned.width;
                    frame.visible_height    = aligned.height;
                    frame.visible_x         = aligned.x;
                    frame.visible_y         = aligned.y;
                    frame.color_matrix      = m_color_matrix;
                    frame.color_range       = m_color_range;
                    frame.format            = format;
                    frame.format_generation = m_format_generation;
                    frame.sequence_number   = ++s_seq_counter;
                    frame.arrival_ns        = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
                    frame.pts_ns            = sample_time * 100LL;
                    frame.au_received_qpc   = m_cur_au_received_qpc;
                    frame.process_input_qpc = m_cur_process_input_qpc;
                    frame.process_output_qpc= process_output_qpc;

                    GlobalMetrics().video_decoded_frames.fetch_add(1, std::memory_order_relaxed);

                    static std::atomic<bool> s_first_sw_decoded{false};
                    if (!s_first_sw_decoded.exchange(true, std::memory_order_relaxed)) {
                        DUWN_LOG_INFOF("Diagnostics",
                            "FIRST EVENT: Video frame decoded: coded={}x{}, visible={}x{} at +{}+{}, DXGI_FORMAT={}, subresource=0, pts={}, decoder=Software, zero_copy=no [Software Upload]",
                            m_width, m_height, aligned.width, aligned.height, aligned.x, aligned.y,
                            format == DXGI_FORMAT_P010 ? "P010" : "NV12", frame.pts_ns);
                        airplay::ConnectionTelemetry::Get().RecordPhase(
                            airplay::ConnectionPhase::FirstDecodedFrame,
                            std::format("{}x{} {} SW-Upload", m_width, m_height,
                                format == DXGI_FORMAT_P010 ? "P010" : "NV12"));
                        telemetry::ConnectionTimeline::Get().Record(
                            telemetry::ConnectionMilestone::C11_FirstDecodedTexture,
                            std::format("{}x{} {} SW-Upload", m_width, m_height,
                                format == DXGI_FORMAT_P010 ? "P010" : "NV12"));
                    }

                    if (m_on_frame) m_on_frame(std::move(frame));
                } else {
                    DUWN_LOG_ERROR("MFVideoDecoder", "Decoded system-memory buffer is too small or upload texture unavailable");
                    buf->Unlock();
                }
            }
        }
    }
    return true;
}

void MFVideoDecoder::Flush() noexcept {
    if (!m_transform) return;
    m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
}

bool MFVideoDecoder::Reset(const DecoderConfig& cfg) noexcept {
    Flush();
    m_transform.Reset();
    m_upload_texture.Reset();
    for (auto& texture : m_upload_textures) texture.Reset();
    m_upload_format = DXGI_FORMAT_UNKNOWN;
    ++m_format_generation;
    return Init(cfg);
}

} // namespace duwn::video
