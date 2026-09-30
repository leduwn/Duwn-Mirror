#pragma once
// RtpCodecClassifier.h — Diagnostic-only RTP payload classifier for Phase 15C WiredCodecProbe.
// Inspects the initial bytes of video RTP payloads to detect H.264 (RFC 6184) vs H.265/HEVC (RFC 7798).

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

    // H.264 NAL unit type: bits 3-7 (payload[0] & 0x1F)
    const uint8_t h264_type = payload[0] & 0x1F;

    // H.265 NAL unit type: bits 1-6 ((payload[0] >> 1) & 0x3F)
    const uint8_t h265_type = (payload[0] >> 1) & 0x3F;

    // HEVC has a 2-byte NAL header:
    // byte 0: [F:1][Type:6][LayerId_hi:1]
    // byte 1: [LayerId_lo:5][TID_plus1:3]
    // Standard base layer: LayerId == 0, TID_plus1 >= 1.
    bool valid_hevc_nuh = false;
    if (payload.size() >= 2) {
        uint8_t layer_id = ((payload[0] & 0x01) << 5) | ((payload[1] >> 3) & 0x1F);
        uint8_t tid_plus1 = payload[1] & 0x07;
        valid_hevc_nuh = (layer_id == 0 && tid_plus1 >= 1);
    }

    // 1. HEVC FU (type 49) — Fragmentation Unit in RFC 7798
    if (h265_type == 49 && valid_hevc_nuh && payload.size() >= 3) {
        result.codec = DetectedCodec::H265;
        result.sender_codec = "H265";
        result.evidence = "RtpPayload (HEVC FU-49)";
        return result;
    }

    // 2. HEVC Parameter Sets: VPS (32), SPS (33), PPS (34)
    if ((h265_type == 32 || h265_type == 33 || h265_type == 34) && valid_hevc_nuh) {
        result.codec = DetectedCodec::H265;
        result.sender_codec = "H265";
        result.evidence = (h265_type == 32) ? "RtpPayload (HEVC VPS-32)" :
                          (h265_type == 33) ? "RtpPayload (HEVC SPS-33)" :
                                              "RtpPayload (HEVC PPS-34)";
        return result;
    }

    // 3. H.264 FU-A (type 28) — Fragmentation Unit in RFC 6184
    if (h264_type == 28 && payload.size() >= 2) {
        result.codec = DetectedCodec::H264;
        result.sender_codec = "H264";
        result.evidence = "RtpPayload (H264 FU-A-28)";
        return result;
    }

    // 4. H.264 Parameter Sets and Slices: SPS (7), PPS (8), IDR (5), Non-IDR (1)
    if (h264_type == 7 || h264_type == 8 || h264_type == 5 || h264_type == 1) {
        result.codec = DetectedCodec::H264;
        result.sender_codec = "H264";
        result.evidence = (h264_type == 7) ? "RtpPayload (H264 SPS-7)" :
                          (h264_type == 8) ? "RtpPayload (H264 PPS-8)" :
                          (h264_type == 5) ? "RtpPayload (H264 IDR-5)" :
                                             "RtpPayload (H264 Slice-1)";
        return result;
    }

    // 5. HEVC Slice types (0-9, 19-21)
    if (((h265_type <= 9) || (h265_type >= 19 && h265_type <= 21)) && valid_hevc_nuh) {
        result.codec = DetectedCodec::H265;
        result.sender_codec = "H265";
        result.evidence = "RtpPayload (HEVC Slice)";
        return result;
    }

    result.codec = DetectedCodec::Unknown;
    result.sender_codec = "UNKNOWN";
    result.evidence = "RtpPayload (Unrecognized)";
    return result;
}

} // namespace duwn::video
