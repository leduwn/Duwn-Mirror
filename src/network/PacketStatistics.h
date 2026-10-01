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

} // namespace duwn::network
