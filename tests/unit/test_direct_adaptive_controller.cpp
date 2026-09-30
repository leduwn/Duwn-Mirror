#include "direct/quality/DirectQualityController.h"
#include <vector>
#include <cstdio>
#include <string>

using namespace duwn::direct::quality;

// 1. Clean Network Scenario:
// Starts at Balanced point, ramps up gradually via additive increase to High Quality point.
// Maintains 60 FPS, 0 resolution changes, zero queue latency.
DUWN_TEST(DirectAdaptive_CleanNetwork_GradualAdditiveRampUp) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier1080p;
    cfg.initial_fps = 60;
    cfg.step_up_hold_cycles = 5; // Fast test cadence

    DirectQualityController controller(cfg);

    DUWN_ASSERT(controller.GetCurrentWidth() == 1920);
    DUWN_ASSERT(controller.GetCurrentHeight() == 1080);
    DUWN_ASSERT(controller.GetCurrentFps() == 60);
    DUWN_ASSERT(controller.GetCurrentBitrate() == kTier1080p.balanced_bitrate_bps); // 12 Mbps

    NetworkTelemetryMetrics clean_metrics;
    clean_metrics.actual_send_bitrate_bps = 12'000'000;
    clean_metrics.available_throughput_estimate_bps = 30'000'000; // Abundant bandwidth
    clean_metrics.rtt_ms = 4.2;
    clean_metrics.jitter_ms = 0.8;
    clean_metrics.packet_loss_fraction = 0.0;
    clean_metrics.decoder_queue_depth = 0;
    clean_metrics.frame_supersede_count = 0;
    clean_metrics.receiver_incomplete_frames = 0;

    // Run clean cycles: verify bitrate ramps up without resolution flapping
    for (int i = 0; i < 25; ++i) {
        auto decision = controller.Evaluate(clean_metrics);
        DUWN_ASSERT(!decision.resolution_changed);
        DUWN_ASSERT(!decision.fps_changed);
        DUWN_ASSERT(decision.fps == 60);
        clean_metrics.actual_send_bitrate_bps = decision.target_bitrate_bps;
    }

    // Must reach high-quality ceiling (20 Mbps)
    DUWN_ASSERT(controller.GetCurrentBitrate() == kTier1080p.high_bitrate_bps);
    DUWN_ASSERT(controller.GetResolutionChangeCount() == 0);
    DUWN_ASSERT(controller.GetFpsChangeCount() == 0);
}

// 2. Mild Congestion Scenario:
// Bandwidth dips, but stays above current resolution quality floor (6 Mbps for 1080p).
// Controller reduces bitrate while keeping resolution and 60 FPS intact.
DUWN_TEST(DirectAdaptive_MildCongestion_ReducesBitrateWithoutDownscaling) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier1080p;
    DirectQualityController controller(cfg);

    // Initial state at balanced 12 Mbps
    DUWN_ASSERT(controller.GetCurrentBitrate() == 12'000'000);

    // Mild congestion: available throughput drops to 9 Mbps (well above 6 Mbps floor)
    NetworkTelemetryMetrics congested_metrics;
    congested_metrics.actual_send_bitrate_bps = 12'000'000;
    congested_metrics.available_throughput_estimate_bps = 9'000'000;
    congested_metrics.rtt_ms = 28.0; // Mild RTT elevation
    congested_metrics.jitter_ms = 9.5;
    congested_metrics.packet_loss_fraction = 0.025; // 2.5% loss

    auto decision = controller.Evaluate(congested_metrics);

    // Policy: Bitrate reduced, but resolution and FPS strictly preserved!
    DUWN_ASSERT(decision.bitrate_changed);
    DUWN_ASSERT(decision.target_bitrate_bps < 12'000'000);
    DUWN_ASSERT(decision.target_bitrate_bps >= kTier1080p.floor_bitrate_bps); // >= 6 Mbps
    DUWN_ASSERT(!decision.resolution_changed);
    DUWN_ASSERT(decision.width == 1920 && decision.height == 1080);
    DUWN_ASSERT(!decision.fps_changed);
    DUWN_ASSERT(decision.fps == 60);
}

// 3. Temporary Packet Loss Spike Scenario:
// Packet loss spikes for 2 cycles, then clears.
// Controller backs off fast, but does NOT instantly spike bitrate back up (anti-oscillation).
DUWN_TEST(DirectAdaptive_TemporaryPacketLoss_FastBackoffSlowProbe) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier1080p;
    cfg.step_up_hold_cycles = 6;
    DirectQualityController controller(cfg);

    // Cycle 1: 5% packet loss burst
    NetworkTelemetryMetrics loss_metrics;
    loss_metrics.actual_send_bitrate_bps = 12'000'000;
    loss_metrics.available_throughput_estimate_bps = 12'000'000;
    loss_metrics.packet_loss_fraction = 0.05; // 5% loss

    auto dec1 = controller.Evaluate(loss_metrics);
    DUWN_ASSERT(dec1.bitrate_changed);
    uint32_t backed_off_bitrate = dec1.target_bitrate_bps;
    DUWN_ASSERT(backed_off_bitrate < 12'000'000);

    // Cycle 2: loss clears immediately
    NetworkTelemetryMetrics clean_metrics;
    clean_metrics.actual_send_bitrate_bps = backed_off_bitrate;
    clean_metrics.available_throughput_estimate_bps = 25'000'000;
    clean_metrics.packet_loss_fraction = 0.0;
    clean_metrics.rtt_ms = 5.0;
    clean_metrics.jitter_ms = 1.0;

    // Immediately after loss clears (cycle 1 clean), bitrate must NOT jump back up
    auto dec2 = controller.Evaluate(clean_metrics);
    DUWN_ASSERT(!dec2.bitrate_changed);
    DUWN_ASSERT(dec2.target_bitrate_bps == backed_off_bitrate);

    // Run clean cycles up to hold threshold: bitrate remains held
    for (int i = 0; i < 4; ++i) {
        auto dec = controller.Evaluate(clean_metrics);
        DUWN_ASSERT(!dec.bitrate_changed);
    }

    // Only after sustained clean hold cycles does probe increase start
    auto dec_probe = controller.Evaluate(clean_metrics);
    DUWN_ASSERT(dec_probe.bitrate_changed);
    DUWN_ASSERT(dec_probe.target_bitrate_bps > backed_off_bitrate);
}

// 4. Bandwidth Reduction Below Quality Floor Scenario:
// Source is 1440p (Quality floor 12 Mbps).
// Bandwidth collapses to 8 Mbps.
// Policy: Instead of dropping bitrate to 8 Mbps at 1440p (which causes severe blur),
// controller steps down resolution to 1080p, preserving text sharpness!
DUWN_TEST(DirectAdaptive_BandwidthBelowFloor_StepsDownResolutionToPreserveSharpness) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier1440p; // 2560x1440 source
    cfg.resolution_cooldown_cycles = 5;
    DirectQualityController controller(cfg);

    DUWN_ASSERT(controller.GetCurrentWidth() == 2560);
    DUWN_ASSERT(controller.GetCurrentHeight() == 1440);

    // Bandwidth collapses to 8 Mbps (violates 1440p floor of 12 Mbps!)
    NetworkTelemetryMetrics starved_metrics;
    starved_metrics.actual_send_bitrate_bps = 16'000'000;
    starved_metrics.available_throughput_estimate_bps = 8'000'000;
    starved_metrics.packet_loss_fraction = 0.04;
    starved_metrics.rtt_ms = 35.0;

    auto decision = controller.Evaluate(starved_metrics);

    // Invariant: Controller downscaled to 1080p, where 8 Mbps is well above floor (6 Mbps)
    DUWN_ASSERT(decision.resolution_changed);
    DUWN_ASSERT(decision.width == 1920 && decision.height == 1080);
    DUWN_ASSERT(decision.keyframe_requested);
    DUWN_ASSERT(decision.target_bitrate_bps >= kTier1080p.floor_bitrate_bps); // >= 6 Mbps
    DUWN_ASSERT(decision.fps == 60); // 60 FPS maintained!

    // Second drop: Bandwidth collapses further to 4 Mbps (violates 1080p floor of 6 Mbps!)
    // Wait out cooldown while maintaining 1080p within its 6-12 Mbps envelope
    NetworkTelemetryMetrics stable_1080p;
    stable_1080p.available_throughput_estimate_bps = 8'500'000;
    stable_1080p.rtt_ms = 8.0;
    stable_1080p.packet_loss_fraction = 0.0;
    for (int i = 0; i < 5; ++i) {
        controller.Evaluate(stable_1080p);
    }

    NetworkTelemetryMetrics second_drop;
    second_drop.available_throughput_estimate_bps = 4'000'000;
    second_drop.packet_loss_fraction = 0.04;
    auto decision2 = controller.Evaluate(second_drop);

    // Invariant: Controller downscaled to 720p, preserving sharpness at 4 Mbps!
    DUWN_ASSERT(decision2.resolution_changed);
    DUWN_ASSERT(decision2.width == 1280 && decision2.height == 720);
    DUWN_ASSERT(decision2.target_bitrate_bps >= kTier720p.floor_bitrate_bps); // >= 3 Mbps
    DUWN_ASSERT(decision2.fps == 60); // 60 FPS still maintained!
}

// 5. Last Resort FPS Reduction Under Extreme Starvation:
// Bandwidth drops to 1.8 Mbps at 720p with 18% packet loss.
// Only after bitrate and resolution adaptation are exhausted does FPS drop to 30.
DUWN_TEST(DirectAdaptive_SevereStarvation_ReducesFpsAsLastResort) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier720p; // Already at lowest resolution
    DirectQualityController controller(cfg);

    NetworkTelemetryMetrics severe_metrics;
    severe_metrics.actual_send_bitrate_bps = 5'000'000;
    severe_metrics.available_throughput_estimate_bps = 1'800'000; // < 3.0 Mbps floor
    severe_metrics.packet_loss_fraction = 0.18;                  // 18% loss

    auto decision = controller.Evaluate(severe_metrics);

    // Priority 2: FPS drop to 30 strictly as last resort
    DUWN_ASSERT(decision.fps_changed);
    DUWN_ASSERT(decision.fps == 30);
    DUWN_ASSERT(decision.keyframe_requested);
}

// 6. Network Recovery & Anti-Oscillation Hysteresis:
// Network recovers to 30 Mbps.
// Quality recovers gradually: FPS restores -> bitrate ramps -> resolution steps up.
// Zero rapid oscillation between modes.
DUWN_TEST(DirectAdaptive_RecoveryAndAntiOscillation) {
    ControllerConfig cfg;
    cfg.native_source_resolution = kTier1080p;
    cfg.step_up_hold_cycles = 4;
    cfg.resolution_step_up_hold_cycles = 8;
    cfg.resolution_cooldown_cycles = 6;
    DirectQualityController controller(cfg);

    // Force downscale to 720p
    NetworkTelemetryMetrics bad_metrics;
    bad_metrics.available_throughput_estimate_bps = 4'000'000;
    bad_metrics.packet_loss_fraction = 0.05;
    auto d_down = controller.Evaluate(bad_metrics);
    DUWN_ASSERT(d_down.resolution_changed);
    DUWN_ASSERT(controller.GetCurrentWidth() == 1280);

    // Network fully recovers
    NetworkTelemetryMetrics clean_metrics;
    clean_metrics.available_throughput_estimate_bps = 30'000'000;
    clean_metrics.packet_loss_fraction = 0.0;
    clean_metrics.rtt_ms = 4.0;
    clean_metrics.jitter_ms = 0.5;

    // Run clean cycles: verify resolution does NOT flap immediately on cycle 1
    auto d1 = controller.Evaluate(clean_metrics);
    DUWN_ASSERT(!d1.resolution_changed);
    DUWN_ASSERT(controller.GetCurrentWidth() == 1280);

    // Step through cycles until resolution upgrade hold threshold is reached
    bool resolution_upgraded = false;
    for (int i = 0; i < 20; ++i) {
        auto d = controller.Evaluate(clean_metrics);
        if (d.resolution_changed && d.width == 1920) {
            resolution_upgraded = true;
            break;
        }
    }

    // Must successfully step back up to 1080p without oscillation
    DUWN_ASSERT(resolution_upgraded);
    DUWN_ASSERT(controller.GetCurrentWidth() == 1920);
    DUWN_ASSERT(controller.GetCurrentHeight() == 1080);
    DUWN_ASSERT(controller.GetCurrentFps() == 60);
}
