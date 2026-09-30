// ConnectionTimeline.cpp — Unified monotonic connection timeline implementation.

#include "ConnectionTimeline.h"
#include "common/clock/MonotonicClock.h"
#include "common/logging/Logger.h"
#include <format>

namespace duwn::telemetry {

ConnectionTimeline& ConnectionTimeline::Get() noexcept {
    static ConnectionTimeline instance;
    return instance;
}

void ConnectionTimeline::Record(ConnectionMilestone m, std::string_view detail) noexcept {
    int idx = static_cast<int>(m);
    if (idx < 0 || idx >= static_cast<int>(ConnectionMilestone::_COUNT)) return;

    int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_timestamps_ns[idx] != 0) {
        // Already recorded for this session/phase
        return;
    }

    if (m_timestamps_ns[0] == 0) {
        m_timestamps_ns[0] = now_ns;
        m_last_milestone_ns = now_ns;
    }

    m_timestamps_ns[idx] = now_ns;

    double delta_prev_ms = (now_ns - m_last_milestone_ns) / 1'000'000.0;
    double delta_c0_ms   = (now_ns - m_timestamps_ns[0]) / 1'000'000.0;
    m_last_milestone_ns = now_ns;

    if (detail.empty()) {
        DUWN_LOG_INFOF("Timeline", "[TIMELINE] {} {} (+{:.2f} ms from prev, +{:.2f} ms from C0)",
            ConnectionMilestoneCode(m), ConnectionMilestoneName(m), delta_prev_ms, delta_c0_ms);
    } else {
        DUWN_LOG_INFOF("Timeline", "[TIMELINE] {} {} (+{:.2f} ms from prev, +{:.2f} ms from C0) — {}",
            ConnectionMilestoneCode(m), ConnectionMilestoneName(m), delta_prev_ms, delta_c0_ms, detail);
    }
}

int64_t ConnectionTimeline::GetTimestampNs(ConnectionMilestone m) const noexcept {
    int idx = static_cast<int>(m);
    if (idx < 0 || idx >= static_cast<int>(ConnectionMilestone::_COUNT)) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_timestamps_ns[idx];
}

double ConnectionTimeline::GetElapsedMs(ConnectionMilestone from, ConnectionMilestone to) const noexcept {
    int idx_from = static_cast<int>(from);
    int idx_to   = static_cast<int>(to);
    if (idx_from < 0 || idx_from >= static_cast<int>(ConnectionMilestone::_COUNT) ||
        idx_to < 0 || idx_to >= static_cast<int>(ConnectionMilestone::_COUNT)) {
        return -1.0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_timestamps_ns[idx_from] == 0 || m_timestamps_ns[idx_to] == 0) {
        return -1.0;
    }

    return (m_timestamps_ns[idx_to] - m_timestamps_ns[idx_from]) / 1'000'000.0;
}

void ConnectionTimeline::ResetSession() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Reset connection/streaming milestones C6..C15, preserve C0..C5 startup milestones
    for (int i = static_cast<int>(ConnectionMilestone::C6_ControlConnectionAccepted);
         i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        m_timestamps_ns[i] = 0;
    }
    m_last_milestone_ns = m_timestamps_ns[static_cast<int>(ConnectionMilestone::C5_MediaSessionReady)];
}

void ConnectionTimeline::ResetAll() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timestamps_ns.fill(0);
    m_last_milestone_ns = 0;
}

std::string ConnectionTimeline::FormatReport() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string report;
    report.reserve(1024);

    int64_t c0 = m_timestamps_ns[0];
    report += "\n=== CONNECTION TIMELINE MILESTONES ===\n";
    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        auto m = static_cast<ConnectionMilestone>(i);
        int64_t ts = m_timestamps_ns[i];
        if (ts > 0) {
            double ms_from_c0 = c0 > 0 ? (ts - c0) / 1'000'000.0 : 0.0;
            report += std::format("{:3s}: {:<32s} [+{:.2f} ms from C0]\n",
                ConnectionMilestoneCode(m), ConnectionMilestoneName(m), ms_from_c0);
        } else {
            report += std::format("{:3s}: {:<32s} [NOT REACHED]\n",
                ConnectionMilestoneCode(m), ConnectionMilestoneName(m));
        }
    }
    report += "======================================\n";
    return report;
}

} // namespace duwn::telemetry
