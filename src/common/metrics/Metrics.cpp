#include "Metrics.h"

namespace duwn {

Metrics& GlobalMetrics() noexcept {
    // Static local — zero-initialised, thread-safe construction (C++11+).
    static Metrics instance;
    return instance;
}

} // namespace duwn
