#pragma once
// DriftController — long-term A/V drift correction. Milestone 3 full impl.
// Milestone 0: tracks drift measurement only. No correction applied yet.

#include <atomic>
#include <cstdint>

namespace duwn::sync {

class DriftController {
public:
    // Called periodically (e.g. every second) with current A/V offset.
    void Update(double av_offset_ms) noexcept;

    // Drift rate in ms/minute. Positive = video running fast relative to audio.
    double DriftMsPerMin() const noexcept;

private:
    std::atomic<double> m_drift_ms_per_min{0.0};
    double m_prev_offset_ms{0.0};
    int64_t m_prev_update_ns{0};
};

} // namespace duwn::sync
