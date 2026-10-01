#pragma once
// RtpCodecClassifier.h — RTP payload classifier for video codec detection (H.264 vs H.265/HEVC).
// Inspects initial bytes of video RTP payloads (RFC 6184 for H.264, RFC 7798 for H.265).
// Enforces unambiguous parameter set / keyframe detection; ambiguous packets return Unknown.

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>

namespace duwn::video {

enum class DetectedCodec {
    Unknown,
    H264,
    H265
};

struct RtpClassification {
    DetectedCodec codec{DetectedCodec::Unknown};
    const char*   sender_codec{"UNKNOWN"};
    uint8_t       rtp_payload_type{0};
    uint32_t      clock_rate{90000};
    const char*   evidence{""};
};

// Inspects the first bytes of an RTP payload to identify H.264 vs H.265.
inline RtpClassification ClassifyRtpPayload(std::span<const uint8_t> payload, uint8_t pt = 96) noexcept {
    RtpClassification result{};
    result.rtp_payload_type = pt;
    result.clock_rate = 90000;

    if (payload.empty()) {
        result.codec = DetectedCodec::Unknown;
        result.sender_codec = "UNKNOWN";
        result.evidence = "EmptyPayload";
        return result;
    }

    // Byte 0: forbidden_zero_bit must be 0 in both H.264 and H.265
    if ((payload[0] & 0x80) != 0) {
        result.codec = DetectedCodec::Unknown;
        result.sender_codec = "UNKNOWN";
        result.evidence = "InvalidForbiddenBit";
        return result;
    }

    // HEVC has a 2-byte NAL header:
    // byte 0: [F:1][Type:6][LayerId_hi:1]
    // byte 1: [LayerId_lo:5][TID_plus1:3]
    // Standard base layer: LayerId == 0, TID_plus1 in [1, 7].
    bool valid_hevc_nuh = false;
    uint8_t h265_type = 0;
    if (payload.size() >= 2) {
        h265_type = (payload[0] >> 1) & 0x3F;
        uint8_t layer_id = ((payload[0] & 0x01) << 5) | ((payload[1] >> 3) & 0x1F);
        uint8_t tid_plus1 = payload[1] & 0x07;
        valid_hevc_nuh = (layer_id == 0 && tid_plus1 >= 1 && tid_plus1 <= 7);
    }

    // H.264 NAL unit type: bits 3-7 (payload[0] & 0x1F)
    const uint8_t h264_type = payload[0] & 0x1F;
    const uint8_t h264_nri  = (payload[0] >> 5) & 0x03;

    // 1. HEVC FU (type 49) — Fragmentation Unit in RFC 7798
    if (valid_hevc_nuh && h265_type == 49 && payload.size() >= 3) {
        uint8_t fu_header = payload[2];
        bool fu_s = (fu_header & 0x80) != 0;
        bool fu_e = (fu_header & 0x40) != 0;
        uint8_t fu_type = fu_header & 0x3F;
        if (!(fu_s && fu_e) && fu_type <= 40) {
            result.codec = DetectedCodec::H265;
            result.sender_codec = "H265";
            result.evidence = "RtpPayload (HEVC FU-49)";
            return result;
        }
    }

    // 2. HEVC Parameter Sets: VPS (32), SPS (33), PPS (34)
    if (valid_hevc_nuh && (h265_type == 32 || h265_type == 33 || h265_type == 34)) {
        result.codec = DetectedCodec::H265;
        result.sender_codec = "H265";
        result.evidence = (h265_type == 32) ? "RtpPayload (HEVC VPS-32)" :
                          (h265_type == 33) ? "RtpPayload (HEVC SPS-33)" :
                                              "RtpPayload (HEVC PPS-34)";
        return result;
    }

    // 3. HEVC Keyframe Slices: IDR_W_RADL (19), IDR_N_LP (20), CRA_NUT (21)
    // Resolves conflict: e.g. 0x28 0x01 is HEVC IDR-20 (NUH layer=0, tid=1), NOT H264 PPS-8.
    if (valid_hevc_nuh && (h265_type == 19 || h265_type == 20 || h265_type == 21)) {
        result.codec = DetectedCodec::H265;
        result.sender_codec = "H265";
        result.evidence = (h265_type == 19) ? "RtpPayload (HEVC IDR-19)" :
                          (h265_type == 20) ? "RtpPayload (HEVC IDR-20)" :
                                              "RtpPayload (HEVC CRA-21)";
        return result;
    }

    // 4. H.264 FU-A (type 28) — Fragmentation Unit in RFC 6184
    if (h264_type == 28 && payload.size() >= 2) {
        uint8_t fu_header = payload[1];
        bool fu_s = (fu_header & 0x80) != 0;
        bool fu_e = (fu_header & 0x40) != 0;
        uint8_t fu_type = fu_header & 0x1F;
        if (!(fu_s && fu_e) && fu_type >= 1 && fu_type <= 23) {
            result.codec = DetectedCodec::H264;
            result.sender_codec = "H264";
            result.evidence = "RtpPayload (H264 FU-A-28)";
            return result;
        }
    }

    // 5. H.264 Parameter Sets: SPS (7) and PPS (8)
    if (h264_type == 7 && payload.size() >= 4) {
        uint8_t profile = payload[1];
        if (profile == 66 || profile == 77 || profile == 88 || profile == 100 ||
            profile == 110 || profile == 122 || profile == 244) {
            result.codec = DetectedCodec::H264;
            result.sender_codec = "H264";
            result.evidence = "RtpPayload (H264 SPS-7)";
            return result;
        }
    }

    if (h264_type == 8 && (payload[0] == 0x68 || (!valid_hevc_nuh && h264_nri != 0))) {
        result.codec = DetectedCodec::H264;
        result.sender_codec = "H264";
        result.evidence = "RtpPayload (H264 PPS-8)";
        return result;
    }

    // 6. H.264 IDR Slice (type 5)
    if (h264_type == 5 && h264_nri != 0 && !valid_hevc_nuh) {
        result.codec = DetectedCodec::H264;
        result.sender_codec = "H264";
        result.evidence = "RtpPayload (H264 IDR-5)";
        return result;
    }

    // Ambiguous packets do NOT lock codec.
    result.codec = DetectedCodec::Unknown;
    result.sender_codec = "UNKNOWN";
    result.evidence = "RtpPayload (Ambiguous)";
    return result;
}

} // namespace duwn::video
