#include "VideoDecoder.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include <cstring>
#include <format>
#include <chrono>
#include <string>
#include <vector>

namespace duwn::video {

// Annex-B start code: 0x00 0x00 0x00 0x01
static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};

struct NALAnalysis {
    std::vector<uint8_t> nal_types;
    uint32_t count_nal1{0}; // non-IDR slice
    uint32_t count_nal5{0}; // IDR slice
    uint32_t count_nal6{0}; // SEI
    uint32_t count_nal7{0}; // SPS
    uint32_t count_nal8{0}; // PPS
    uint32_t count_nal9{0}; // AUD
    bool has_sps{false};
    bool has_pps{false};
    bool has_vps{false};
    bool has_idr{false};
};

static NALAnalysis InspectAccessUnitNals(const uint8_t* data, size_t size,
                                          VideoCodecType codec) noexcept {
    NALAnalysis result{};
    if (size < 4) return result;

    for (size_t i = 0; i + 4 <= size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 &&
            ((data[i + 2] == 1) || (data[i + 2] == 0 && i + 4 <= size && data[i + 3] == 1))) {
            size_t header_idx = (data[i + 2] == 1) ? (i + 3) : (i + 4);
            if (header_idx < size) {
                uint8_t nal_type = codec == VideoCodecType::H265
                    ? static_cast<uint8_t>((data[header_idx] >> 1) & 0x3F)
                    : static_cast<uint8_t>(data[header_idx] & 0x1F);
                result.nal_types.push_back(nal_type);
                if (codec == VideoCodecType::H265) {
                    if (nal_type == 32) result.has_vps = true;
                    if (nal_type == 33) result.has_sps = true;
                    if (nal_type == 34) result.has_pps = true;
                    if (nal_type == 19 || nal_type == 20) result.has_idr = true;
                } else switch (nal_type) {
                case 1: result.count_nal1++; break;
                case 5: result.count_nal5++; result.has_idr = true; break;
                case 6: result.count_nal6++; break;
                case 7: result.count_nal7++; result.has_sps = true; break;
                case 8: result.count_nal8++; result.has_pps = true; break;
                case 9: result.count_nal9++; break;
                default: break;
                }
            }
            if (data[i + 2] == 0) {
                i += 3;
            } else {
                i += 2;
            }
        }
    }
    return result;
}

VideoDecoder::VideoDecoder(D3D11Device& device, FrameReadyCallback on_frame) noexcept
    : m_mf_decoder(device, std::move(on_frame)) {
    m_au_buf.reserve(512 * 1024); // 512 KiB initial capacity
    m_hevc_assembler = std::make_unique<HevcRtpAssembler>([this](EncodedAccessUnit au) {
        FeedAccessUnit(std::move(au));
    });
}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::Init(uint32_t width, uint32_t height, VideoCodecType codec) noexcept {
    if (width == 0 || height == 0) return false;
    if (codec != VideoCodecType::H264 && codec != VideoCodecType::H265) return false;
    DecoderConfig cfg;
    cfg.width  = width;
    cfg.height = height;
    cfg.codec  = codec;
    cfg.preference = m_preference;
    const bool ready = m_initialized ? m_mf_decoder.Reset(cfg) : m_mf_decoder.Init(cfg);
    m_initialized = ready;
    if (ready) {
        m_active_codec = codec;
        m_width = width;
        m_height = height;
        m_source_generation = 0;
        if (codec == VideoCodecType::H264) {
            m_last_h264_width = width;
            m_last_h264_height = height;
        } else if (codec == VideoCodecType::H265 && m_hevc_assembler) {
            m_hevc_assembler->SetFormatHints(width, height, m_source_generation);
        }
    }
    return ready;
}

void VideoDecoder::FeedAccessUnit(EncodedAccessUnit au) noexcept {
    if (au.data.empty()) return;
    if (au.codec != VideoCodecType::H264 && au.codec != VideoCodecType::H265) return;
    if (au.codec == VideoCodecType::H265 &&
        !(au.data.size() >= 4 && au.data[0] == 0 && au.data[1] == 0 &&
          (au.data[2] == 1 || (au.data[2] == 0 && au.data[3] == 1)))) {
        DUWN_LOG_ERROR("VideoDecoder", "HEVC access unit must use Annex-B start codes");
        return;
    }
    const bool hevc_format_changed = au.codec == VideoCodecType::H265 &&
        ((au.width_hint != 0 && au.height_hint != 0 &&
          (au.width_hint != m_width || au.height_hint != m_height)) ||
         (au.format_generation != 0 && au.format_generation != m_source_generation));
    if (!m_initialized || au.codec != m_active_codec || hevc_format_changed) {
        const uint32_t width = (au.width_hint != 0) ? au.width_hint :
            (au.codec == VideoCodecType::H264 ? m_last_h264_width : m_width);
        const uint32_t height = (au.height_hint != 0) ? au.height_hint :
            (au.codec == VideoCodecType::H264 ? m_last_h264_height : m_height);
        if (width == 0 || height == 0 || !Init(width, height, au.codec)) {
            DUWN_LOG_ERROR("VideoDecoder", "Codec route unavailable or missing stream dimensions");
            return;
        }
        m_source_generation = au.format_generation;
    }

    NALAnalysis analysis = InspectAccessUnitNals(au.data.data(), au.data.size(), au.codec);
    if (au.has_sps) analysis.has_sps = true;
    if (au.has_pps) analysis.has_pps = true;
    if (au.has_idr) analysis.has_idr = true;

    static std::atomic<bool> s_first_au{false};
    if (!s_first_au.exchange(true, std::memory_order_relaxed)) {
        DUWN_LOG_INFOF("Diagnostics",
            "FIRST EVENT: Unified Access Unit received (size={} B, pts={})",
            au.data.size(), au.pts_ns);
    }

    duwn::GlobalMetrics().video_access_units.fetch_add(1, std::memory_order_relaxed);
    duwn::GlobalMetrics().video_access_units_submitted.fetch_add(1, std::memory_order_relaxed);

    if (au.au_received_qpc == 0) {
        au.au_received_qpc = duwn::clock::MonotonicClock::NowQpcTicks();
    }
    if (au.rtp_arrival_qpc == 0) {
        au.rtp_arrival_qpc = au.au_received_qpc;
    }

    auto decode_start = std::chrono::steady_clock::now();
    m_mf_decoder.Feed(au.data.data(), au.data.size(), au.pts_ns, static_cast<uint16_t>(au.sequence_number & 0xFFFF),
                      analysis.has_sps, analysis.has_pps, analysis.has_idr,
                      au.au_received_qpc, au.rtp_arrival_qpc);
    auto decode_end = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(decode_end - decode_start).count();
    m_decode_samples.push_back(ms);

    const int64_t now_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    if (m_last_decode_stats_time_ns == 0) {
        m_last_decode_stats_time_ns = now_ns;
    } else if (now_ns - m_last_decode_stats_time_ns >= 1'000'000'000LL) {
        if (!m_decode_samples.empty()) {
            std::sort(m_decode_samples.begin(), m_decode_samples.end());
            double sum = 0.0;
            for (double s : m_decode_samples) sum += s;
            duwn::GlobalMetrics().video_decode_time_ms.store(sum / static_cast<double>(m_decode_samples.size()), std::memory_order_relaxed);
            duwn::GlobalMetrics().video_decode_p50_ms.store(m_decode_samples[m_decode_samples.size() / 2], std::memory_order_relaxed);
            size_t idx95 = static_cast<size_t>(static_cast<double>(m_decode_samples.size()) * 0.95);
            if (idx95 >= m_decode_samples.size()) idx95 = m_decode_samples.size() - 1;
            duwn::GlobalMetrics().video_decode_p95_ms.store(m_decode_samples[idx95], std::memory_order_relaxed);
            duwn::GlobalMetrics().video_decode_sample_count.store(m_decode_samples.size(), std::memory_order_relaxed);
            m_decode_samples.clear();
        }
        m_last_decode_stats_time_ns = now_ns;
    }
}

void VideoDecoder::EmitAccessUnit(int64_t pts_ns, uint16_t seq) noexcept {
    if (m_au_buf.empty()) return;

    EncodedAccessUnit au;
    au.data = std::move(m_au_buf);
    au.pts_ns = pts_ns;
    au.sequence_number = ++m_au_sequence;
    au.au_received_qpc = duwn::clock::MonotonicClock::NowQpcTicks();
    au.rtp_arrival_qpc = m_first_packet_qpc > 0 ? m_first_packet_qpc : au.au_received_qpc;
    m_first_packet_qpc = 0;
    FeedAccessUnit(std::move(au));

    m_au_buf.clear();
    m_au_buf.reserve(512 * 1024);
}

void VideoDecoder::FeedRtp(const uint8_t* payload, size_t size,
                            uint32_t rtp_ts, int64_t pts_ns,
                            bool marker, uint16_t seq) noexcept {
    if (size == 0) return;
    if (m_active_codec == VideoCodecType::Unknown) {
        return;
    }

    if (m_active_codec == VideoCodecType::H265) {
        if (m_hevc_assembler) {
            m_hevc_assembler->FeedRtp(payload, size, rtp_ts, pts_ns, marker, seq);
        }
        return;
    }

    if (m_first_packet_qpc == 0) {
        m_first_packet_qpc = duwn::clock::MonotonicClock::NowQpcTicks();
    }

    // Detect timestamp boundary: if RTP timestamp changed and buffer is non-empty,
    // emit prior access unit before beginning new one (RFC 6184 §5.1)
    if (m_has_last_rtp_ts && rtp_ts != m_last_rtp_ts && !m_au_buf.empty()) {
        EmitAccessUnit(m_last_pts_ns, m_last_seq);
    }
    m_last_rtp_ts     = rtp_ts;
    m_last_pts_ns     = pts_ns;
    m_last_seq        = seq;
    m_has_last_rtp_ts = true;

    uint8_t nal_type = payload[0] & 0x1F;

    // ---- Single NAL unit (nal_type 1..23) ----
    if (nal_type >= 1 && nal_type <= 23) {
        m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
        m_au_buf.insert(m_au_buf.end(), payload, payload + size);
        if (marker) EmitAccessUnit(pts_ns, seq);
        return;
    }

    // ---- STAP-A (nal_type 24): multiple NALs in one packet ----
    if (nal_type == 24) {
        size_t offset = 1; // skip STAP-A header
        while (offset + 2 <= size) {
            uint16_t nal_len = static_cast<uint16_t>(
                (payload[offset] << 8) | payload[offset + 1]);
            offset += 2;
            if (offset + nal_len > size) break;
            m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
            m_au_buf.insert(m_au_buf.end(),
                             payload + offset,
                             payload + offset + nal_len);
            offset += nal_len;
        }
        if (marker) EmitAccessUnit(pts_ns, seq);
        return;
    }

    // ---- FU-A (nal_type 28): fragmentation unit ----
    if (nal_type == 28 && size >= 2) {
        uint8_t fu_header = payload[1];
        bool start = (fu_header & 0x80) != 0;
        bool end   = (fu_header & 0x40) != 0;
        uint8_t reconstructed_nal_type = fu_header & 0x1F;

        if (start) {
            // If a previous FU-A was incomplete, roll back only its bytes
            if (m_fu_a_started) {
                if (m_fu_a_nal_start < m_au_buf.size()) {
                    m_au_buf.resize(m_fu_a_nal_start);
                }
                duwn::GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);
            }

            m_fu_a_nal_start = m_au_buf.size();
            m_fu_a_started   = true;
            m_fu_a_pts       = pts_ns;
            m_fu_a_seq       = seq;

            // Write start code + reconstructed NAL header
            m_au_buf.insert(m_au_buf.end(), kStartCode, kStartCode + 4);
            uint8_t nal_hdr = (payload[0] & 0xE0) | reconstructed_nal_type;
            m_au_buf.push_back(nal_hdr);
        } else if (m_fu_a_started) {
            // Sequence continuity check in FU-A
            uint16_t expected_seq = static_cast<uint16_t>(m_fu_a_seq + 1);
            if (seq != expected_seq) {
                // Packet loss inside FU-A — abort corrupted NAL (rollback only this NAL)
                if (m_fu_a_nal_start < m_au_buf.size()) {
                    m_au_buf.resize(m_fu_a_nal_start);
                }
                m_fu_a_started = false;
                duwn::GlobalMetrics().video_dropped_frames.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            m_fu_a_seq = seq;
        }

        if (m_fu_a_started && size > 2) {
            m_au_buf.insert(m_au_buf.end(), payload + 2, payload + size);
        }

        if (end && m_fu_a_started) {
            if (marker) {
                EmitAccessUnit(m_fu_a_pts, m_fu_a_seq);
            }
            m_fu_a_started = false;
        }
        return;
    }

    // Unsupported NAL type — log at trace level only
    DUWN_LOG_DEBUG("VideoDecoder",
        std::format("Unsupported RTP NAL type {}", nal_type));
}

void VideoDecoder::Flush() noexcept {
    m_au_buf.clear();
    m_fu_a_started    = false;
    m_fu_a_nal_start  = 0;
    m_has_last_rtp_ts = false;
    m_last_rtp_ts     = 0;
    m_decode_samples.clear();
    m_last_decode_stats_time_ns = 0;
    if (m_hevc_assembler) {
        m_hevc_assembler->Flush();
    }
    m_mf_decoder.Flush();
}

void VideoDecoder::ResetCodecState() noexcept {
    Flush();
    if (m_hevc_assembler) {
        m_hevc_assembler->Reset(0);
    }
    m_active_codec = VideoCodecType::Unknown;
    m_initialized = false;
    m_width = 0;
    m_height = 0;
    m_source_generation = 0;
    m_last_h264_width = 0;
    m_last_h264_height = 0;
    m_au_sequence = 0;
    m_first_packet_qpc = 0;
}

void VideoDecoder::ResetHevcAssembler(uint32_t generation) noexcept {
    if (m_hevc_assembler) {
        m_hevc_assembler->Reset(generation);
    }
}

} // namespace duwn::video
