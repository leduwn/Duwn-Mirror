#pragma once
#include <atomic>
#include <cstdint>

namespace duwn::video {
struct VideoStatistics {
    std::atomic<uint64_t> decoded{0};
    std::atomic<uint64_t> rendered{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> late{0};
    std::atomic<double>   decode_ms{0.0};
    std::atomic<double>   render_ms{0.0};
};
} // namespace duwn::video
