#pragma once
// EncodedAccessUnit.h — Unified representation of a single compressed video frame/access unit.
//
// Both TransportMode::RtpUdpLegacy (RFC 6184 NAL assembly) and
// TransportMode::DirectIpc (shared-memory ring buffer) converge at this type
// before submission to the hardware video decoder (MFVideoDecoder).

#include <vector>
#include <cstdint>
#include <cstddef>

namespace duwn::video {

enum class VideoCodecType : uint8_t {
    Unknown = 0,
    H264 = 1,
    H265 = 2,
};

struct EncodedAccessUnit {
    std::vector<uint8_t> data;                  // Raw Annex-B NAL bitstream (00 00 00 01 prefixed)
    int64_t              pts_ns{0};             // PTS in nanoseconds (MonotonicClock)
    int64_t              dts_ns{0};             // DTS in nanoseconds (0 if equal to PTS)
    uint64_t             sequence_number{0};    // Sequential AU sequence number
    uint32_t             format_generation{0};  // Format generation for geometry changes
    VideoCodecType       codec{VideoCodecType::Unknown};

    bool                 has_sps{false};
    bool                 has_pps{false};
    bool                 has_vps{false};
    bool                 has_idr{false};

    // Metadata hints from source container (0 if unknown)
    uint32_t             width_hint{0};
    uint32_t             height_hint{0};
    int64_t              arrival_ns{0};         // Timestamp when received at ingest boundary
    int64_t              rtp_arrival_qpc{0};    // QPC tick when first RTP packet of AU arrived (T0)
    int64_t              au_received_qpc{0};    // QPC tick when AU was received/assembled (T1)
};

} // namespace duwn::video
