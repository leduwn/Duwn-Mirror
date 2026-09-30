#include "DriftController.h"
#include "common/clock/MonotonicClock.h"
#include "common/metrics/Metrics.h"

namespace duwn::sync {

void DriftController::Update(double av_offset_ms) noexcept {
    int64_t now = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    if (m_prev_update_ns == 0) {
        m_prev_update_ns = now;
        m_prev_offset_ms = av_offset_ms;
        return;
    }
    double elapsed_min = static_cast<double>(now - m_prev_update_ns)
                         / 60'000'000'000.0; // ns → minutes
    if (elapsed_min < 0.016) return; // update at most 1/min rate after first second

    double drift = (av_offset_ms - m_prev_offset_ms) / elapsed_min;
    m_drift_ms_per_min.store(drift, std::memory_order_relaxed);
    duwn::GlobalMetrics().drift_ms_per_min.store(drift, std::memory_order_relaxed);

    m_prev_update_ns = now;
    m_prev_offset_ms = av_offset_ms;
}

double DriftController::DriftMsPerMin() const noexcept {
    return m_drift_ms_per_min.load(std::memory_order_relaxed);
}

} // namespace duwn::sync
