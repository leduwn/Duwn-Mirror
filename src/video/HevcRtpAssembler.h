#pragma once
// HevcRtpAssembler.h — RFC 7798 HEVC RTP payload depacketizer and Annex-B AU assembler.
// Handles:
// - Single NAL unit packets (NAL types 0..47)
// - Aggregation Packets (AP, NAL type 48)
// - Fragmentation Units (FU, NAL type 49): start, middle, end, sequence continuity check, packet loss rollback
// - PACI (NAL type 50): safe detection and rejection
// - Parameter set caching and insertion (VPS=32, SPS=33, PPS=34)

#include "EncodedAccessUnit.h"
#include "HevcParameterSetCache.h"
#include <vector>
#include <cstdint>
#include <cstddef>
#include <functional>

namespace duwn::video {

using AccessUnitCallback = std::function<void(EncodedAccessUnit)>;

class HevcRtpAssembler {
public:
    explicit HevcRtpAssembler(AccessUnitCallback on_au) noexcept;
    ~HevcRtpAssembler() = default;

    HevcRtpAssembler(const HevcRtpAssembler&) = delete;
    HevcRtpAssembler& operator=(const HevcRtpAssembler&) = delete;

    // Feed one RTP packet payload.
    void FeedRtp(const uint8_t* payload, size_t size,
                 uint32_t rtp_ts, int64_t pts_ns,
                 bool marker, uint16_t seq) noexcept;

    void Flush() noexcept;
    void Reset(uint32_t generation = 0) noexcept;
    void SetFormatHints(uint32_t width, uint32_t height, uint32_t generation) noexcept;

    const HevcParameterSetCache& ParameterCache() const noexcept { return m_param_cache; }
    HevcParameterSetCache& ParameterCache() noexcept { return m_param_cache; }

private:
    void EmitAccessUnit(int64_t pts_ns, uint16_t seq) noexcept;

    AccessUnitCallback    m_on_au;
    HevcParameterSetCache m_param_cache;

    std::vector<uint8_t>  m_au_buf;
    size_t                m_fu_nal_start{0};
    bool                  m_fu_started{false};
    int64_t               m_fu_pts{0};
    uint16_t              m_fu_seq{0};

    bool                  m_has_last_rtp_ts{false};
    uint32_t              m_last_rtp_ts{0};
    int64_t               m_last_pts_ns{0};
    uint16_t              m_last_seq{0};
    int64_t               m_first_packet_qpc{0};
    uint64_t              m_au_sequence{0};

    uint32_t              m_width_hint{0};
    uint32_t              m_height_hint{0};
    uint32_t              m_format_generation{0};

    bool                  m_current_au_has_vps{false};
    bool                  m_current_au_has_sps{false};
    bool                  m_current_au_has_pps{false};
    bool                  m_current_au_has_idr{false};
    bool                  m_first_au_emitted{false};
};

} // namespace duwn::video
