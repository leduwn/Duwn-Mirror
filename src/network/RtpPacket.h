#pragma once
// RtpPacket — parsed RTP header + payload view.
// Owns no memory; payload points into the receive buffer.
// Caller must ensure the source buffer outlives the RtpPacket.

#include <cstdint>
#include <span>

namespace duwn::network {

struct RtpPacket {
    // RTP header fields
    uint8_t  version{};       // must be 2
    bool     padding{};
    bool     extension{};
    bool     marker{};
    uint8_t  payload_type{};
    uint16_t sequence{};
    uint32_t timestamp{};     // RTP timestamp (media clock units)
    uint32_t ssrc{};

    // Payload (points into receive buffer — zero-copy)
    std::span<const uint8_t> payload{};

    // Wall-clock arrival time (nanoseconds, MonotonicClock)
    int64_t arrival_ns{};

    // Parse raw UDP datagram. Returns false if malformed.
    static bool Parse(std::span<const uint8_t> datagram,
                      int64_t arrival_ns,
                      RtpPacket& out) noexcept;
};

} // namespace duwn::network
