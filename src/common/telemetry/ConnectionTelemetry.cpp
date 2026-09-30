#include "ConnectionTelemetry.h"
#include "common/clock/MonotonicClock.h"
#include "common/logging/Logger.h"
#include <format>

namespace duwn::airplay {

ConnectionTelemetry& ConnectionTelemetry::Get() noexcept {
    static ConnectionTelemetry instance;
    return instance;
}

void ConnectionTelemetry::RecordPhase(ConnectionPhase phase, std::string_view detail) noexcept {
    int idx = static_cast<int>(phase);
    if (idx < 1 || idx >= static_cast<int>(ConnectionPhase::_COUNT)) return;

    int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase_timestamps_ns[idx] != 0) {
        // Already recorded for this session
        return;
    }

    if (m_session_start_ns == 0 || phase == ConnectionPhase::SidecarStarted) {
        m_session_start_ns = now_ns;
        m_last_phase_ns = now_ns;
    }

    m_phase_timestamps_ns[idx] = now_ns;
    m_current_phase = phase;

    double delta_prev_ms = (now_ns - m_last_phase_ns) / 1'000'000.0;
    double delta_start_ms = (now_ns - m_session_start_ns) / 1'000'000.0;
    m_last_phase_ns = now_ns;

    if (detail.empty()) {
        DUWN_LOG_INFOF("ConnectionPhase", "[Phase {:2d}/14] {} (+{:.1f} ms, total: {:.1f} ms)",
            idx, ConnectionPhaseName(phase), delta_prev_ms, delta_start_ms);
    } else {
        DUWN_LOG_INFOF("ConnectionPhase", "[Phase {:2d}/14] {} (+{:.1f} ms, total: {:.1f} ms) — {}",
            idx, ConnectionPhaseName(phase), delta_prev_ms, delta_start_ms, detail);
    }
}

int64_t ConnectionTelemetry::GetPhaseTimestampNs(ConnectionPhase phase) const noexcept {
    int idx = static_cast<int>(phase);
    if (idx < 1 || idx >= static_cast<int>(ConnectionPhase::_COUNT)) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_phase_timestamps_ns[idx];
}

bool ConnectionTelemetry::IsFullyConnected() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_phase_timestamps_ns[static_cast<int>(ConnectionPhase::FirstPresentedFrame)] != 0;
}

void ConnectionTelemetry::Reset() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_phase_timestamps_ns.fill(0);
    m_session_start_ns = 0;
    m_last_phase_ns = 0;
    m_current_phase = ConnectionPhase::SidecarStarted;
}

} // namespace duwn::airplay
