// Tests for RtpPacket::Parse

#include "network/RtpReceiver.h"
#include <winsock2.h>
#include <ws2tcpip.h>

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

DUWN_TEST(rtp_receiver_raw_udp_and_stats) {
    using namespace duwn::network;
    std::atomic<size_t> received_valid{0};
    RtpReceiver receiver([&](const RtpPacket& pkt) {
        received_valid.fetch_add(1, std::memory_order_relaxed);
    });

    uint16_t port = receiver.Start();
    DUWN_ASSERT(port != 0);
    DUWN_ASSERT(receiver.IsRunning());

    SOCKET send_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DUWN_ASSERT(send_sock != INVALID_SOCKET);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = ::htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    // Send raw non-RTP datagram (e.g. 5 bytes malformed)
    const uint8_t raw_bad[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    ::sendto(send_sock, reinterpret_cast<const char*>(raw_bad), sizeof(raw_bad), 0,
             reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

    // Send valid RTP packet
    const uint8_t valid_rtp[] = {
        0x80, 0x60, 0x00, 0x01,
        0x00, 0x00, 0x2B, 0xD0,
        0xCA, 0xFE, 0xBA, 0xBE,
        0xDE, 0xAD, 0xBE, 0xEF
    };
    ::sendto(send_sock, reinterpret_cast<const char*>(valid_rtp), sizeof(valid_rtp), 0,
             reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

    ::closesocket(send_sock);

    // Wait briefly for receiver loop to process
    for (int i = 0; i < 20 && receiver.Stats().raw_udp.load() < 2; ++i) {
        ::Sleep(10);
    }

    DUWN_ASSERT(receiver.Stats().raw_udp.load() >= 2);
    DUWN_ASSERT(receiver.Stats().malformed.load() >= 1);
    DUWN_ASSERT(receiver.Stats().received.load() >= 1);
    DUWN_ASSERT(received_valid.load() >= 1);
    DUWN_ASSERT(receiver.IsRunning());

    receiver.Stop();
    DUWN_ASSERT(!receiver.IsRunning());
}
