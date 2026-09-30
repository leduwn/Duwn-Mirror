#pragma once
#include <atomic>
#include <cstdint>

namespace duwn::audio {
struct AudioStatistics {
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> samples_rendered{0};
    std::atomic<double>   buffer_ms{0.0};
};
} // namespace duwn::audio
