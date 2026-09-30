#include "common/streaming/StreamingPolicy.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

DUWN_TEST(StreamingPolicy_FastestSupersedesEvenFreshBurst) {
    const auto policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::LowLatency);
    DUWN_ASSERT(policy.max_decoded_frames == 1);
    DUWN_ASSERT(duwn::ShouldPresentNewest(policy, 3, 0.1, 16.6667));
    // An isolated/static frame must still be shown, even if old.
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 1, 10000.0, 16.6667));
}

DUWN_TEST(StreamingPolicy_BalancedFollowsSourceCadence) {
    const auto policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::SmoothLive);
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 2, 20.0, 1000.0 / 60.0));
    DUWN_ASSERT(duwn::ShouldPresentNewest(policy, 2, 25.0, 1000.0 / 60.0));
    // A 30 FPS source needs a different freshness budget; do not fake 60 FPS.
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 2, 25.0, 1000.0 / 30.0));
    DUWN_ASSERT(duwn::ShouldPresentNewest(policy, 2, 45.0, 1000.0 / 30.0));
}

DUWN_TEST(StreamingPolicy_SmoothRemainsBounded) {
    const auto policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Compatibility);
    DUWN_ASSERT(policy.max_decoded_frames <= 3);
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 3, 25.0, 1000.0 / 60.0));
    DUWN_ASSERT(duwn::ShouldPresentNewest(policy, 3, 40.0, 1000.0 / 60.0));
}

DUWN_TEST(StreamingPolicy_CustomThresholdDoesNotAddDelay) {
    const auto policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 10, 2);
    // Before the threshold, consume FIFO immediately; no prefill requirement.
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 2, 10.0, 33.3333));
    DUWN_ASSERT(duwn::ShouldPresentNewest(policy, 2, 10.1, 33.3333));
    DUWN_ASSERT(!duwn::ShouldPresentNewest(policy, 1, 1000.0, 33.3333));
}

DUWN_TEST(StreamingPolicy_InvalidInputsCannotCreateUnboundedQueue) {
    for (uint32_t frames : {0u, 1u, 3u, 500u, std::numeric_limits<uint32_t>::max()}) {
        for (uint32_t ms : {0u, 5u, 100u, 500u, std::numeric_limits<uint32_t>::max()}) {
            const auto policy = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, ms, frames);
            DUWN_ASSERT(policy.max_decoded_frames >= 1 && policy.max_decoded_frames <= 3);
            DUWN_ASSERT(policy.max_residence_ms >= 5 && policy.max_residence_ms <= 100);
        }
    }
    const auto fallback = duwn::ResolveStreamingPolicy(static_cast<duwn::StreamingMode>(99));
    for (double cadence : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        DUWN_ASSERT(std::isfinite(duwn::FreshnessThresholdMs(fallback, cadence)));
        DUWN_ASSERT(duwn::ShouldPresentNewest(fallback, 2, 1000.0, cadence));
    }
}

DUWN_TEST(StreamingPolicy_LiveChangesUseConsistentSnapshot) {
    const auto fastest = duwn::ResolveStreamingPolicy(duwn::StreamingMode::LowLatency);
    const auto custom = duwn::ResolveStreamingPolicy(duwn::StreamingMode::Custom, 100, 3);
    std::atomic<duwn::StreamingPolicy> current{fastest};
    std::atomic<bool> finished{false};
    std::jthread writer([&] {
        for (int i = 0; i < 50000; ++i) {
            current.store(i % 2 ? fastest : custom, std::memory_order_release);
        }
        finished.store(true, std::memory_order_release);
    });
    do {
        const auto p = current.load(std::memory_order_acquire);
        const bool is_fastest = p.max_decoded_frames == 1 && p.max_residence_ms == 0 && p.always_latest;
        const bool is_custom = p.max_decoded_frames == 3 && p.max_residence_ms == 100 && !p.always_latest;
        DUWN_ASSERT(is_fastest || is_custom);
    } while (!finished.load(std::memory_order_acquire));
}
