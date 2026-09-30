#pragma once
// MasterClock — single monotonic reference for A/V sync.
// Backed by MonotonicClock (QPC). All pipeline components
// convert their timestamps to MasterClock nanoseconds.

#include "common/clock/MonotonicClock.h"
#include <atomic>
#include <cstdint>

namespace duwn::sync {

class MasterClock {
public:
    static void Initialize() noexcept {
        duwn::clock::MonotonicClock::Initialize();
    }

    // Current master time in nanoseconds.
    static int64_t NowNs() noexcept {
        return duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    }
};

} // namespace duwn::sync
