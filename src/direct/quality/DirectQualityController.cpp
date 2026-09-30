#include "DirectQualityController.h"

namespace duwn::direct::quality {

DirectQualityController::DirectQualityController(const ControllerConfig& config)
    : m_config(config),
      m_current_profile(config.native_source_resolution),
      m_current_fps(config.initial_fps),
      m_current_bitrate_bps(config.native_source_resolution.balanced_bitrate_bps) {
}

void DirectQualityController::Reset(const ControllerConfig& config) {
    m_config = config;
    m_current_profile = config.native_source_resolution;
    m_current_fps = config.initial_fps;
    m_current_bitrate_bps = config.native_source_resolution.balanced_bitrate_bps;
    m_consecutive_clean_cycles = 0;
    m_resolution_cooldown_counter = 0;
    m_resolution_changes = 0;
    m_fps_changes = 0;
    m_last_supersede_count = 0;
    m_last_incomplete_frames = 0;
}

ResolutionTierProfile DirectQualityController::GetNextLowerResolution(
    const ResolutionTierProfile& current) const noexcept {
    if (current.width > kTier1080p.width) {
        return kTier1080p;
    }
    if (current.width > kTier720p.width) {
        return kTier720p;
    }
    return current; // Already at lowest supported resolution (720p)
}

ResolutionTierProfile DirectQualityController::GetNextHigherResolution(
    const ResolutionTierProfile& current) const noexcept {
    if (current.width < kTier1080p.width && m_config.native_source_resolution.width >= kTier1080p.width) {
        return kTier1080p;
    }
    if (current.width < kTier1440p.width && m_config.native_source_resolution.width >= kTier1440p.width) {
        return kTier1440p;
    }
    return current; // Already at native resolution
}

bool DirectQualityController::IsCongested(const NetworkTelemetryMetrics& metrics) const noexcept {
    // 1. Packet loss above tolerance (e.g. > 2%)
    if (metrics.packet_loss_fraction > 0.02) return true;

    // 2. High latency or jitter spike
    if (metrics.rtt_ms > 25.0) return true;
    if (metrics.jitter_ms > 8.0) return true;

    // 3. Buffer growth / queue latency (violating 0/1 buffering invariant)
    if (metrics.decoder_queue_depth > 1) return true;
    if (metrics.frame_supersede_count > m_last_supersede_count) return true;
    if (metrics.receiver_incomplete_frames > m_last_incomplete_frames) return true;

    // 4. Throughput estimate dropped below current send bitrate
    if (metrics.available_throughput_estimate_bps > 0 &&
        metrics.available_throughput_estimate_bps < m_current_bitrate_bps) {
        return true;
    }

    return false;
}

bool DirectQualityController::IsClean(const NetworkTelemetryMetrics& metrics) const noexcept {
    if (metrics.packet_loss_fraction > 0.005) return false;
    if (metrics.rtt_ms > 15.0) return false;
    if (metrics.jitter_ms > 4.0) return false;
    if (metrics.decoder_queue_depth > 1) return false;
    if (metrics.frame_supersede_count > m_last_supersede_count) return false;
    if (metrics.receiver_incomplete_frames > m_last_incomplete_frames) return false;

    // If throughput is known, require at least 15% headroom above current bitrate
    if (metrics.available_throughput_estimate_bps > 0 &&
        metrics.available_throughput_estimate_bps < static_cast<uint32_t>(m_current_bitrate_bps * 1.15)) {
        return false;
    }

    return true;
}

AdaptationDecision DirectQualityController::Evaluate(const NetworkTelemetryMetrics& metrics) {
    AdaptationDecision decision;
    decision.width = m_current_profile.width;
    decision.height = m_current_profile.height;
    decision.fps = m_current_fps;
    decision.target_bitrate_bps = m_current_bitrate_bps;

    if (m_resolution_cooldown_counter > 0) {
        m_resolution_cooldown_counter--;
    }

    bool congested = IsCongested(metrics);
    bool clean = IsClean(metrics);

    if (congested) {
        m_consecutive_clean_cycles = 0;

        // Calculate desired reduced bitrate
        uint32_t desired_bitrate = static_cast<uint32_t>(m_current_bitrate_bps * 0.80);
        if (metrics.available_throughput_estimate_bps > 0) {
            uint32_t safe_estimate = static_cast<uint32_t>(metrics.available_throughput_estimate_bps * 0.85);
            desired_bitrate = safe_estimate;
        }

        // Check if desired bitrate stays at or above current resolution's quality floor
        if (desired_bitrate >= m_current_profile.floor_bitrate_bps) {
            // Priority 3: Maintain resolution as long as bitrate remains above floor
            if (desired_bitrate != m_current_bitrate_bps) {
                m_current_bitrate_bps = desired_bitrate;
                decision.target_bitrate_bps = m_current_bitrate_bps;
                decision.bitrate_changed = true;
                decision.reason = "Congestion: reduced bitrate above quality floor";
            }
        } else {
            // Required bitrate would fall below Phase 6A quality floor!
            // Policy: "reduce resolution one step rather than creating extremely blurry video"
            ResolutionTierProfile lower_profile = GetNextLowerResolution(m_current_profile);

            if (!(lower_profile == m_current_profile)) {
                // Downscale resolution one step
                if (m_resolution_cooldown_counter == 0 || metrics.packet_loss_fraction > 0.08) {
                    m_current_profile = lower_profile;
                    m_resolution_changes++;
                    m_resolution_cooldown_counter = m_config.resolution_cooldown_cycles;

                    // Set bitrate to healthy point in new lower resolution
                    m_current_bitrate_bps = std::clamp(
                        desired_bitrate,
                        lower_profile.floor_bitrate_bps,
                        lower_profile.high_bitrate_bps
                    );

                    decision.width = m_current_profile.width;
                    decision.height = m_current_profile.height;
                    decision.target_bitrate_bps = m_current_bitrate_bps;
                    decision.resolution_changed = true;
                    decision.bitrate_changed = true;
                    decision.keyframe_requested = true;
                    decision.reason = "Bandwidth below quality floor: downscaled resolution to prevent blur";
                } else {
                    // In cooldown: clamp to floor
                    m_current_bitrate_bps = m_current_profile.floor_bitrate_bps;
                    decision.target_bitrate_bps = m_current_bitrate_bps;
                    decision.bitrate_changed = true;
                    decision.reason = "Bandwidth constrained: clamped to quality floor during cooldown";
                }
            } else {
                // Already at lowest resolution (720p) and bandwidth is still below 720p floor!
                // Priority 2: Maintain smooth FPS, but if both bitrate and resolution adaptation
                // cannot maintain stream, reduce FPS to 30 as last resort
                if (m_current_fps > m_config.min_fps &&
                    (desired_bitrate < kTier720p.floor_bitrate_bps || metrics.packet_loss_fraction > 0.15)) {
                    m_current_fps = m_config.min_fps;
                    m_fps_changes++;
                    // At 30 FPS, double byte budget per frame allows lower bitrate
                    m_current_bitrate_bps = std::max(2'000'000U, desired_bitrate);
                    decision.fps = m_current_fps;
                    decision.target_bitrate_bps = m_current_bitrate_bps;
                    decision.fps_changed = true;
                    decision.bitrate_changed = true;
                    decision.keyframe_requested = true;
                    decision.reason = "Severe congestion at lowest resolution: reduced FPS to 30 to maintain stream";
                } else {
                    m_current_bitrate_bps = std::max(2'000'000U, desired_bitrate);
                    decision.target_bitrate_bps = m_current_bitrate_bps;
                    decision.bitrate_changed = true;
                    decision.reason = "Lowest resolution clamped to absolute minimum bandwidth";
                }
            }
        }
    } else if (clean) {
        m_consecutive_clean_cycles++;

        // 1. First recover FPS if it was degraded
        if (m_current_fps < m_config.initial_fps &&
            m_consecutive_clean_cycles >= m_config.step_up_hold_cycles) {
            m_current_fps = m_config.initial_fps;
            m_fps_changes++;
            decision.fps = m_current_fps;
            decision.fps_changed = true;
            decision.keyframe_requested = true;
            decision.reason = "Network recovered: restored 60 FPS";
        }
        // 2. Check if resolution can be stepped up
        else if (!(m_current_profile == m_config.native_source_resolution) &&
                 m_current_bitrate_bps >= static_cast<uint32_t>(m_current_profile.high_bitrate_bps * 0.90) &&
                 m_consecutive_clean_cycles >= m_config.resolution_step_up_hold_cycles &&
                 m_resolution_cooldown_counter == 0) {
            ResolutionTierProfile higher_profile = GetNextHigherResolution(m_current_profile);
            bool headroom_confirmed = (metrics.available_throughput_estimate_bps == 0) ||
                (metrics.available_throughput_estimate_bps >= static_cast<uint32_t>(higher_profile.balanced_bitrate_bps * 1.15));

            if (headroom_confirmed) {
                m_current_profile = higher_profile;
                m_resolution_changes++;
                m_resolution_cooldown_counter = m_config.resolution_cooldown_cycles;
                m_current_bitrate_bps = higher_profile.balanced_bitrate_bps;
                m_consecutive_clean_cycles = 0; // Reset after step-up

                decision.width = m_current_profile.width;
                decision.height = m_current_profile.height;
                decision.target_bitrate_bps = m_current_bitrate_bps;
                decision.resolution_changed = true;
                decision.bitrate_changed = true;
                decision.keyframe_requested = true;
                decision.reason = "Sustained headroom: stepped up resolution";
            }
        }
        // 3. Gradual additive bitrate increase
        else if (m_consecutive_clean_cycles >= m_config.step_up_hold_cycles) {
            uint32_t step = 500'000; // +500 Kbps additive increase
            uint32_t new_bitrate = m_current_bitrate_bps + step;

            // Cap at current profile high bitrate
            new_bitrate = std::min(new_bitrate, m_current_profile.high_bitrate_bps);

            // Do not exceed estimated available throughput
            if (metrics.available_throughput_estimate_bps > 0) {
                uint32_t safe_cap = static_cast<uint32_t>(metrics.available_throughput_estimate_bps * 0.90);
                new_bitrate = std::min(new_bitrate, safe_cap);
            }

            if (new_bitrate > m_current_bitrate_bps) {
                m_current_bitrate_bps = new_bitrate;
                decision.target_bitrate_bps = m_current_bitrate_bps;
                decision.bitrate_changed = true;
                decision.reason = "Clean network: additive bitrate probe increase";
            }
        }
    } else {
        // Network state neutral / steady
    }

    // Update historical metric counters
    m_last_supersede_count = metrics.frame_supersede_count;
    m_last_incomplete_frames = metrics.receiver_incomplete_frames;

    return decision;
}

} // namespace duwn::direct::quality
