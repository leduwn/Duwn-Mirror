// Tests for RtpPacket::Parse

#include "network/RtpReceiver.h"

DUWN_TEST(rtp_parse_minimal_valid) {
    // Minimal valid RTP: 12 bytes header + 4 payload
    uint8_t buf[] = {
        0x80, 0x60,        // version=2, no padding/ext, CC=0, marker=0, PT=96
        0x00, 0x01,        // seq=1
        0x00, 0x00, 0x2B, 0xD0, // timestamp=11216
        0xCA, 0xFE, 0xBA, 0xBE, // SSRC
        0xDE, 0xAD, 0xBE, 0xEF  // payload
    };
    duwn::network::RtpPacket pkt;
    bool ok = duwn::network::RtpPacket::Parse(
        std::span{buf, sizeof(buf)}, 0LL, pkt);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(pkt.version == 2);
    DUWN_ASSERT(pkt.payload_type == 96);
    DUWN_ASSERT(pkt.sequence == 1);
    DUWN_ASSERT(pkt.timestamp == 11216);
    DUWN_ASSERT(pkt.ssrc == 0xCAFEBABE);
    DUWN_ASSERT(pkt.payload.size() == 4);
    DUWN_ASSERT(pkt.payload[0] == 0xDE);
}

DUWN_TEST(rtp_parse_too_short) {
    uint8_t buf[] = {0x80, 0x60, 0x00}; // only 3 bytes
    duwn::network::RtpPacket pkt;
    bool ok = duwn::network::RtpPacket::Parse(
        std::span{buf, sizeof(buf)}, 0LL, pkt);
    DUWN_ASSERT(!ok);
}

DUWN_TEST(rtp_parse_wrong_version) {
    uint8_t buf[16]{};
    buf[0] = 0x40; // version=1, invalid
    duwn::network::RtpPacket pkt;
    bool ok = duwn::network::RtpPacket::Parse(
        std::span{buf, sizeof(buf)}, 0LL, pkt);
    DUWN_ASSERT(!ok);
}

DUWN_TEST(rtp_parse_marker_bit) {
    uint8_t buf[12]{};
    buf[0] = 0x80; // version=2
    buf[1] = 0x80 | 96; // marker=1, PT=96
    duwn::network::RtpPacket pkt;
    bool ok = duwn::network::RtpPacket::Parse(
        std::span{buf, sizeof(buf)}, 0LL, pkt);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(pkt.marker == true);
}
