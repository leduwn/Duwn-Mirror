#include "HevcRtpAssembler.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include <span>

namespace duwn::video {

static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};

HevcRtpAssembler::HevcRtpAssembler(AccessUnitCallback on_au) noexcept
    : m_on_au(std::move(on_au)) {
    m_au_buf.reserve(512 * 1024); // 512 KiB initial capacity
}

void HevcRtpAssembler::SetFormatHints(uint32_t width, uint32_t height, uint32_t generation) noexcept {
    m_width_hint = width;
    m_height_hint = height;
    m_format_generation = generation;
    m_param_cache.SetGeneration(generation);
}

void HevcRtpAssembler::Reset(uint32_t generation) noexcept {
    Flush();
    m_au_sequence = 0;
    m_first_au_emitted = false;
    m_format_generation = generation;
    m_param_cache.Clear();
    m_param_cache.SetGeneration(generation);
}

void HevcRtpAssembler::Flush() noexcept {
    m_au_buf.clear();
    m_fu_started = false;
    m_fu_nal_start = 0;
    m_has_last_rtp_ts = false;
    m_last_rtp_ts = 0;
    m_first_packet_qpc = 0;
    m_current_au_has_vps = false;
    m_current_au_has_sps = false;
    m_current_au_has_pps = false;
    m_current_au_has_idr = false;
}

void HevcRtpAssembler::FeedRtp(const uint8_t* payload, size_t size,
                               uint32_t rtp_ts, int64_t pts_ns,
                               bool marker, uint16_t seq) noexcept {
    if (size == 0 || !payload) return;

    // Check RFC 7798 forbidden_zero_bit (bit 7 of first byte must be 0)
    if ((payload[0] & 0x80) != 0) {
        DUWN_LOG_WARN("HevcRtpAssembler", "RTP payload has forbidden_zero_bit set; dropping packet");
        return;
    }

    if (m_first_packet_qpc == 0) {
        m_first_packet_qpc = clock::MonotonicClock::NowQpcTicks();
    }

    // Detect timestamp boundary: if RTP timestamp changed and buffer is non-empty,
    // emit prior access unit before beginning new one (RFC 7798 §4.4)
    if (m_has_last_rtp_ts && rtp_ts != m_last_rtp_ts && !m_au_buf.empty()) {
        EmitAccessUnit(m_last_pts_ns, m_last_seq);
    }
    m_last_rtp_ts     = rtp_ts;
    m_last_pts_ns     = pts_ns;
    m_last_seq        = seq;
    m_has_last_rtp_ts = true;

    uint8_t nal_type = (payload[0] >> 1) & 0x3F;

    // ---- PACI (NAL type 50): reject safely ----
    if (nal_type == 50) {
        DUWN_LOG_WARN("HevcRtpAssembler", "PACI (type 50) packet encountered; unsupported, rejecting safely");
        return;
    }

    // ---- Single NAL unit packet (NAL type 0..47) ----
    if (nal_type <= 47) {
        GlobalMetrics().hevc_single_nals.fetch_add(1, std::memory_order_relaxed);

        m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
        m_au_buf.insert(m_au_buf.end(), payload, payload + size);

        if (nal_type == 32) { // VPS
            m_current_au_has_vps = true;
            m_param_cache.Update(32, std::span<const uint8_t>(payload, size), m_format_generation);
            GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
        } else if (nal_type == 33) { // SPS
            m_current_au_has_sps = true;
            m_param_cache.Update(33, std::span<const uint8_t>(payload, size), m_format_generation);
            GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
        } else if (nal_type == 34) { // PPS
            m_current_au_has_pps = true;
            m_param_cache.Update(34, std::span<const uint8_t>(payload, size), m_format_generation);
            GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
        } else if (nal_type == 19 || nal_type == 20 || nal_type == 21) {
            m_current_au_has_idr = true;
        }

        if (marker) EmitAccessUnit(pts_ns, seq);
        return;
    }

    // ---- Aggregation Packet (AP, NAL type 48) ----
    if (nal_type == 48) {
        GlobalMetrics().hevc_ap_packets.fetch_add(1, std::memory_order_relaxed);
        if (size < 2) return;

        size_t offset = 2; // skip 2-byte AP header
        while (offset + 2 <= size) {
            uint16_t nalu_size = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
            offset += 2;
            if (offset + nalu_size > size) break;

            const uint8_t* nalu_ptr = payload + offset;
            if (nalu_size > 0 && (nalu_ptr[0] & 0x80) == 0) {
                uint8_t sub_nal_type = (nalu_ptr[0] >> 1) & 0x3F;
                m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
                m_au_buf.insert(m_au_buf.end(), nalu_ptr, nalu_ptr + nalu_size);

                if (sub_nal_type == 32) {
                    m_current_au_has_vps = true;
                    m_param_cache.Update(32, std::span<const uint8_t>(nalu_ptr, nalu_size), m_format_generation);
                    GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
                } else if (sub_nal_type == 33) {
                    m_current_au_has_sps = true;
                    m_param_cache.Update(33, std::span<const uint8_t>(nalu_ptr, nalu_size), m_format_generation);
                    GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
                } else if (sub_nal_type == 34) {
                    m_current_au_has_pps = true;
                    m_param_cache.Update(34, std::span<const uint8_t>(nalu_ptr, nalu_size), m_format_generation);
                    GlobalMetrics().hevc_parameter_sets.fetch_add(1, std::memory_order_relaxed);
                } else if (sub_nal_type == 19 || sub_nal_type == 20 || sub_nal_type == 21) {
                    m_current_au_has_idr = true;
                }
            }
            offset += nalu_size;
        }

        if (marker) EmitAccessUnit(pts_ns, seq);
        return;
    }

    // ---- Fragmentation Unit (FU, NAL type 49) ----
    if (nal_type == 49 && size >= 3) {
        uint8_t fu_header = payload[2];
        bool start = (fu_header & 0x80) != 0;
        bool end   = (fu_header & 0x40) != 0;
        uint8_t fu_type = fu_header & 0x3F;

        if (start) {
            // If a previous FU was incomplete, rollback its partial bytes
            if (m_fu_started) {
                if (m_fu_nal_start < m_au_buf.size()) {
                    m_au_buf.resize(m_fu_nal_start);
                }
                GlobalMetrics().hevc_fu_aborted.fetch_add(1, std::memory_order_relaxed);
            }

            m_fu_nal_start = m_au_buf.size();
            m_fu_started   = true;
            m_fu_pts       = pts_ns;
            m_fu_seq       = seq;
            GlobalMetrics().hevc_fu_started.fetch_add(1, std::memory_order_relaxed);

            // Reconstruct 2-byte NAL unit header:
            // Byte 0: F bit from indicator, Type = fu_type, LayerId bit 5 from indicator bit 0
            uint8_t byte0 = (payload[0] & 0x81) | static_cast<uint8_t>((fu_type & 0x3F) << 1);
            // Byte 1: LayerId bits 4..0 + TID from indicator byte 1
            uint8_t byte1 = payload[1];

            m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
            m_au_buf.push_back(byte0);
            m_au_buf.push_back(byte1);

            if (fu_type == 19 || fu_type == 20 || fu_type == 21) {
                m_current_au_has_idr = true;
            } else if (fu_type == 32) {
                m_current_au_has_vps = true;
            } else if (fu_type == 33) {
                m_current_au_has_sps = true;
            } else if (fu_type == 34) {
                m_current_au_has_pps = true;
            }
        } else if (m_fu_started) {
            // Sequence continuity check in FU
            uint16_t expected_seq = static_cast<uint16_t>(m_fu_seq + 1);
            if (seq != expected_seq) {
                // Packet loss inside FU — abort corrupted NAL (rollback only this NAL)
                if (m_fu_nal_start < m_au_buf.size()) {
                    m_au_buf.resize(m_fu_nal_start);
                }
                m_fu_started = false;
                GlobalMetrics().hevc_fu_aborted.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            m_fu_seq = seq;
        }

        if (m_fu_started && size > 3) {
            m_au_buf.insert(m_au_buf.end(), payload + 3, payload + size);
        }

        if (end && m_fu_started) {
            m_fu_started = false;
            GlobalMetrics().hevc_fu_completed.fetch_add(1, std::memory_order_relaxed);
            if (marker) {
                EmitAccessUnit(m_fu_pts, m_fu_seq);
            }
        }
        return;
    }

    DUWN_LOG_DEBUG("HevcRtpAssembler", "Unsupported HEVC RTP NAL type " + std::to_string(nal_type));
}

void HevcRtpAssembler::EmitAccessUnit(int64_t pts_ns, uint16_t seq) noexcept {
    if (m_au_buf.empty()) return;

    // Ensure VPS/SPS/PPS parameter sets from cache are present before IDR or on start/discontinuity
    if (!m_first_au_emitted || m_current_au_has_idr || (!m_current_au_has_vps && !m_current_au_has_sps && !m_current_au_has_pps)) {
        m_param_cache.PrependMissing(m_au_buf, m_current_au_has_vps, m_current_au_has_sps, m_current_au_has_pps);
    }
    m_first_au_emitted = true;

    EncodedAccessUnit au;
    au.data = std::move(m_au_buf);
    au.pts_ns = pts_ns;
    au.sequence_number = ++m_au_sequence;
    au.codec = VideoCodecType::H265;
    au.has_vps = m_current_au_has_vps || m_param_cache.HasVps();
    au.has_sps = m_current_au_has_sps || m_param_cache.HasSps();
    au.has_pps = m_current_au_has_pps || m_param_cache.HasPps();
    au.has_idr = m_current_au_has_idr;
    au.width_hint = m_width_hint;
    au.height_hint = m_height_hint;
    au.format_generation = m_format_generation;
    au.au_received_qpc = clock::MonotonicClock::NowQpcTicks();
    au.rtp_arrival_qpc = m_first_packet_qpc > 0 ? m_first_packet_qpc : au.au_received_qpc;

    // Reset per-AU flags
    m_first_packet_qpc = 0;
    m_current_au_has_vps = false;
    m_current_au_has_sps = false;
    m_current_au_has_pps = false;
    m_current_au_has_idr = false;
    m_au_buf.clear();
    m_au_buf.reserve(512 * 1024);

    if (m_on_au) {
        m_on_au(std::move(au));
    }
}

} // namespace duwn::video
