#pragma once
// VideoDecoder — facade coordinating MFVideoDecoder + NAL unit assembly.
// Accepts H.264 RTP payloads or complete H.264/HEVC access units,
// routes them to MFVideoDecoder.
//
// H.264 RTP depacketization per RFC 6184.
// Handles: single NAL, STAP-A, FU-A.

#include "MFVideoDecoder.h"
#include "D3D11Device.h"
#include "EncodedAccessUnit.h"
#include "HevcRtpAssembler.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

namespace duwn::video {

class VideoDecoder {
public:
    VideoDecoder(D3D11Device& device, FrameReadyCallback on_frame) noexcept;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Init(uint32_t width, uint32_t height,
              VideoCodecType codec = VideoCodecType::H264) noexcept;
    void SetPreference(DecoderPreference preference) noexcept { m_preference = preference; }

    // Direct Access Unit Ingest (Unified path: Direct IPC and post-assembly RTP)
    void FeedAccessUnit(EncodedAccessUnit au) noexcept;

    // Called for each RTP packet payload (after jitter buffer ordering).
    // rtp_ts: RTP timestamp; pts_ns: MonotonicClock PTS (0 if not yet anchored).
    void FeedRtp(const uint8_t* payload, size_t size,
                 uint32_t rtp_ts, int64_t pts_ns,
                 bool marker, uint16_t seq) noexcept;

    void Flush() noexcept;
    void ResetCodecState() noexcept;
    void ResetHevcAssembler(uint32_t generation = 0) noexcept;
    HevcRtpAssembler* GetHevcAssembler() const noexcept { return m_hevc_assembler.get(); }
    bool IsHardware() const noexcept { return m_mf_decoder.IsHardware(); }
    const DecoderInfo& GetDecoderInfo() const noexcept { return m_mf_decoder.GetDecoderInfo(); }
    VideoCodecType GetActiveCodec() const noexcept { return m_active_codec; }

private:
    void EmitAccessUnit(int64_t pts_ns, uint16_t seq) noexcept;
    static constexpr uint8_t kStartCode[] = {0, 0, 0, 1};

    MFVideoDecoder                      m_mf_decoder;
    std::unique_ptr<HevcRtpAssembler>   m_hevc_assembler;
    DecoderPreference       m_preference{DecoderPreference::Auto};
    VideoCodecType          m_active_codec{VideoCodecType::Unknown};
    uint32_t                m_width{0};
    uint32_t                m_height{0};
    uint32_t                m_source_generation{0};
    uint32_t                m_last_h264_width{0};
    uint32_t                m_last_h264_height{0};
    bool                    m_initialized{false};
    std::vector<uint8_t>    m_au_buf;    // current access unit accumulation
    size_t                  m_fu_a_nal_start{0}; // offset in m_au_buf where current FU-A began
    bool                    m_fu_a_started{false};
    int64_t                 m_fu_a_pts{0};
    uint16_t                m_fu_a_seq{0};
    bool                    m_has_last_rtp_ts{false};
    uint32_t                m_last_rtp_ts{0};
    int64_t                 m_last_pts_ns{0};
    uint16_t                m_last_seq{0};
    int64_t                 m_first_packet_qpc{0};
    uint64_t                m_au_sequence{0};
};

} // namespace duwn::video
