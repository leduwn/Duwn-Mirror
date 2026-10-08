#pragma once
#include <atomic>
#include <cstdint>

namespace duwn::network {

struct PacketStatistics {
    std::atomic<uint64_t> raw_udp{0};
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> lost{0};
    std::atomic<uint64_t> reordered{0};
    std::atomic<uint64_t> malformed{0};
    std::atomic<double>   jitter_ms{0.0}; // running estimate (RFC 3550 §A.8)
};

struct RtpSequenceDelta {
    uint16_t gaps{0};
    bool duplicate{false};
    bool out_of_order{false};
    bool advances{false};
};

constexpr RtpSequenceDelta ClassifyRtpSequence(uint16_t previous, uint16_t current) noexcept {
    const uint16_t delta = static_cast<uint16_t>(current - previous);
    if (delta == 0) return {.duplicate = true};
    if (delta < 0x8000) {
        return {.gaps = static_cast<uint16_t>(delta - 1), .advances = true};
    }
    return {.out_of_order = true};
}

} // namespace duwn::network
