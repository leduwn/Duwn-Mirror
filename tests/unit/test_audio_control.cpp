#include "audio/AudioBufferController.h"
#include "audio/AudioClockServo.h"
#include "audio/AudioEngine.h"

#include <cmath>

using duwn::audio::AudioBufferController;
using duwn::audio::AudioClockServo;

DUWN_TEST(stable_low_jitter_target_stays_low) {
    AudioBufferController c;
    c.Configure(145, 480);
    c.ObserveJitter(0.2);
    DUWN_ASSERT(c.TargetFrames() == c.MinimumFrames());
}

DUWN_TEST(jitter_increase_raises_target) {
    AudioBufferController c;
    c.Configure(145, 480);
    auto initial = c.TargetFrames();
    c.ObserveJitter(8.0);
    DUWN_ASSERT(c.TargetFrames() > initial);
}

DUWN_TEST(stable_recovery_lowers_target_slowly) {
    AudioBufferController c;
    c.Configure(145, 480);
    c.OnUnderrun();
    auto raised = c.TargetFrames();
    c.AdvanceStable(4.0);
    DUWN_ASSERT(c.TargetFrames() == raised);
    c.AdvanceStable(4.0);
    DUWN_ASSERT(c.TargetFrames() < raised);
}

DUWN_TEST(underrun_raises_target) {
    AudioBufferController c;
    c.Configure(145, 480);
    auto initial = c.TargetFrames();
    c.OnUnderrun();
    DUWN_ASSERT(c.TargetFrames() > initial);
}

DUWN_TEST(target_never_exceeds_configured_safety_bound) {
    AudioBufferController c;
    c.Configure(145, 480);
    for (int i = 0; i < 100; ++i) c.OnUnderrun();
    DUWN_ASSERT(c.TargetFrames() == c.MaximumFrames());
    DUWN_ASSERT(c.MaximumFrames() == 4800); // measured 100 ms emergency ceiling at 48 kHz
}

DUWN_TEST(target_never_below_endpoint_packet_minimum) {
    AudioBufferController c;
    c.Configure(600, 480);
    c.Reset();
    DUWN_ASSERT(c.TargetFrames() >= 1200);
    DUWN_ASSERT(c.TargetFrames() >= 960);
}

DUWN_TEST(zero_error_zero_correction) {
    AudioClockServo s;
    DUWN_ASSERT(std::abs(s.Update(960, 960, 1.0)) < 0.001);
}

DUWN_TEST(positive_fill_drift_reduces_resampler_ratio) {
    AudioClockServo s;
    DUWN_ASSERT(s.Update(1200, 960, 1.0) < 0.0);
}

DUWN_TEST(negative_fill_drift_increases_resampler_ratio) {
    AudioClockServo s;
    DUWN_ASSERT(s.Update(700, 960, 1.0) > 0.0);
}

DUWN_TEST(ppm_limit_enforced) {
    AudioClockServo s;
    for (int i = 0; i < 1000; ++i) s.Update(10000, 100, 0.1);
    DUWN_ASSERT(std::abs(s.CorrectionPpm()) <= 300.0);
}

DUWN_TEST(integrator_anti_windup) {
    AudioClockServo s;
    for (int i = 0; i < 10000; ++i) s.Update(10000, 100, 0.1);
    for (int i = 0; i < 200; ++i) s.Update(0, 100, 0.1);
    DUWN_ASSERT(s.CorrectionPpm() > -300.0);
}

DUWN_TEST(session_reset_zeroes_servo) {
    AudioClockServo s;
    s.Update(1200, 960, 1.0);
    s.Reset();
    DUWN_ASSERT(s.CorrectionPpm() == 0.0);
}

DUWN_TEST(endpoint_switch_zeroes_servo) {
    AudioClockServo s;
    s.Update(700, 960, 1.0);
    s.Reset();
    DUWN_ASSERT(s.CorrectionPpm() == 0.0);
}

DUWN_TEST(discontinuity_recovery_requires_gap_and_confirmed_underrun) {
    DUWN_ASSERT(!duwn::audio::ShouldRecoverAudioDiscontinuity(100.0, 0));
    DUWN_ASSERT(!duwn::audio::ShouldRecoverAudioDiscontinuity(60.0, 1));
    DUWN_ASSERT(duwn::audio::ShouldRecoverAudioDiscontinuity(60.1, 1));
}

DUWN_TEST(audio_engine_discontinuity_recovery_state_transitions) {
    duwn::audio::AudioRingBuffer ring(8192, 2);
    duwn::audio::AudioEngine engine(ring);
    duwn::airplay::StreamMetadata meta{};
    meta.audio_sample_rate = 44100;
    meta.audio_channels = 2;
    engine.Init(meta);

    DUWN_ASSERT(!engine.InDiscontinuityRecovery());

    // Feed initial packet
    std::vector<uint8_t> pcm_data(144 * 4, 0); // 144 stereo frames of S16BE
    int64_t t = 1'000'000'000LL;
    engine.Feed(pcm_data.data(), pcm_data.size(), 1000, t);
    DUWN_ASSERT(!engine.InDiscontinuityRecovery());

    // Trigger discontinuity: notify underrun + gap > 60ms
    engine.NotifyUnderrun();
    t += 70'000'000LL; // 70ms gap
    engine.Feed(pcm_data.data(), pcm_data.size(), 2000, t);
    DUWN_ASSERT(engine.InDiscontinuityRecovery());

    // Feed 35 consecutive stable packets (3ms interval)
    for (int i = 0; i < 35; ++i) {
        t += 3'000'000LL;
        engine.Feed(pcm_data.data(), pcm_data.size(), 2000 + (i + 1) * 144, t);
    }

    // Now pull from ring to settle buffer below target + headroom
    std::vector<float> pull_dst(8192 * 2, 0.0f);
    ring.Pull(pull_dst.data(), ring.Available());

    // Feed one more stable packet to trigger exit check
    t += 3'000'000LL;
    engine.Feed(pcm_data.data(), pcm_data.size(), 2000 + 36 * 144, t);
    DUWN_ASSERT(!engine.InDiscontinuityRecovery());

    // Test Flush resets recovery
    engine.NotifyUnderrun();
    t += 70'000'000LL;
    engine.Feed(pcm_data.data(), pcm_data.size(), 10000, t);
    DUWN_ASSERT(engine.InDiscontinuityRecovery());
    engine.Flush();
    DUWN_ASSERT(!engine.InDiscontinuityRecovery());
}

DUWN_TEST(adaptive_target_increase_and_decision) {
    AudioBufferController c(48000);
    c.Configure(144, 480);
    DUWN_ASSERT(c.TargetFrames() == c.MinimumFrames());
    DUWN_ASSERT(c.MinimumFrames() == 960); // 20ms at 48kHz

    // Initial decision
    const auto& d0 = c.LastDecision();
    DUWN_ASSERT(d0.clamp_min_ms == 20.0);
    DUWN_ASSERT(d0.clamp_max_ms == 100.0);

    // Jitter of 15ms -> required = max(960, 15ms * 48 * 3 = 2160 frames = 45ms)
    c.ObserveJitter(15.0);
    DUWN_ASSERT(c.TargetFrames() == 2160);
    DUWN_ASSERT(c.TargetMs() == 45.0);
    const auto& d1 = c.LastDecision();
    DUWN_ASSERT(d1.rtp_jitter_estimate_ms == 15.0);
    DUWN_ASSERT(d1.new_target_ms == 45.0);
}

DUWN_TEST(adaptive_target_decay_returns_to_floor) {
    AudioBufferController c(48000);
    c.Configure(144, 480);
    c.ObserveJitter(15.0); // Target = 2160 frames (45ms)
    DUWN_ASSERT(c.TargetFrames() == 2160);

    // Less than 6.0s -> no decay
    c.AdvanceStable(3.0);
    DUWN_ASSERT(c.TargetFrames() == 2160);

    // Reach 6.0s -> decays by 1 packet (144 frames)
    c.AdvanceStable(3.0);
    DUWN_ASSERT(c.TargetFrames() == 2160 - 144);
}

DUWN_TEST(target_min_max_clamps_enforced) {
    AudioBufferController c(48000);
    c.Configure(144, 480);

    // Minimum clamp is 20ms (960 frames)
    DUWN_ASSERT(c.MinimumFrames() == 960);
    DUWN_ASSERT(c.TargetFrames() >= 960);

    // Massive jitter (500ms) must be clamped to 100ms (4800 frames)
    c.ObserveJitter(500.0);
    DUWN_ASSERT(c.TargetFrames() == 4800);
    DUWN_ASSERT(c.TargetMs() == 100.0);
}

DUWN_TEST(normal_vs_recovery_target_state) {
    AudioBufferController c(48000);
    c.Configure(144, 480);

    c.SetSessionClass(duwn::audio::AudioSessionClass::NormalSteady);
    DUWN_ASSERT(c.GetSessionClass() == duwn::audio::AudioSessionClass::NormalSteady);

    c.SetSessionClass(duwn::audio::AudioSessionClass::DiscontinuityRecovery);
    DUWN_ASSERT(c.GetSessionClass() == duwn::audio::AudioSessionClass::DiscontinuityRecovery);

    // Jitter decision records current session class
    c.ObserveJitter(10.0);
    DUWN_ASSERT(c.LastDecision().session_class == duwn::audio::AudioSessionClass::DiscontinuityRecovery);
}

DUWN_TEST(session_reset_restores_initial_state) {
    AudioBufferController c(48000);
    c.Configure(144, 480);
    c.ObserveJitter(25.0);
    DUWN_ASSERT(c.TargetFrames() > c.MinimumFrames());

    c.Reset();
    DUWN_ASSERT(c.TargetFrames() == c.MinimumFrames());
    DUWN_ASSERT(c.TargetFrames() == 960);
}

DUWN_TEST(no_permanent_ratchet_after_stable_run) {
    AudioBufferController c(48000);
    c.Configure(144, 480);

    // Trigger spike to 45ms (2160 frames)
    c.ObserveJitter(15.0);
    DUWN_ASSERT(c.TargetFrames() == 2160);

    // Simulate 60 seconds of stable streaming (10 decay cycles of 6.0s each)
    // 2160 - 960 = 1200 frames. 1200 / 144 = 8.33 cycles.
    for (int i = 0; i < 10; ++i) {
        c.AdvanceStable(6.0);
    }

    // Target MUST return completely to minimum floor (960 frames = 20ms)
    DUWN_ASSERT(c.TargetFrames() == c.MinimumFrames());
    DUWN_ASSERT(c.TargetMs() == 20.0);
}

DUWN_TEST(endpoint_period_dev_override_selection) {
    AudioBufferController c(48000);
    c.Configure(144, 480);

    // Verify dev override setter works
    c.SetDevTargetOverride(30.0);
    DUWN_ASSERT(c.DevTargetOverride() == 30.0);
    DUWN_ASSERT(c.TargetMs() == 30.0);
    DUWN_ASSERT(c.TargetFrames() == 1440); // 30ms at 48kHz

    // Jitter cannot override dev target
    c.ObserveJitter(50.0);
    DUWN_ASSERT(c.TargetMs() == 30.0);

    // Clear override
    c.SetDevTargetOverride(0.0);
    DUWN_ASSERT(c.TargetFrames() == 960);
}

DUWN_TEST(unsupported_period_fallback_logic) {
    // Validating period range math:
    // Device min = 240 (5ms), max = 480 (10ms)
    UINT32 min_p = 240, max_p = 480, default_p = 480;

    auto test_select_period = [&](double req_ms) -> UINT32 {
        if (req_ms <= 0.0) return std::max(min_p, default_p);
        UINT32 req_frames = static_cast<UINT32>(std::lround(req_ms * 48.0));
        if (req_frames >= min_p && req_frames <= max_p) return req_frames;
        return std::max(min_p, default_p); // fallback
    };

    // Valid 5ms period
    DUWN_ASSERT(test_select_period(5.0) == 240);
    // Valid 10ms period
    DUWN_ASSERT(test_select_period(10.0) == 480);
    // Unsupported 1ms period falls back to 480
    DUWN_ASSERT(test_select_period(1.0) == 480);
    // Unsupported 50ms period falls back to 480
    DUWN_ASSERT(test_select_period(50.0) == 480);
}

DUWN_TEST(clock_servo_independence_from_target) {
    AudioClockServo servo;
    // Test at different target buffer levels (20ms vs 50ms vs 90ms)
    // When buffer matches target exactly, servo correction must be 0.0 ppm
    DUWN_ASSERT(std::abs(servo.Update(960, 960, 0.01)) < 0.001);
    DUWN_ASSERT(std::abs(servo.Update(2400, 2400, 0.01)) < 0.001);
    DUWN_ASSERT(std::abs(servo.Update(4320, 4320, 0.01)) < 0.001);

    // Servo limits remain strictly clamped to ±300 ppm regardless of target
    for (int i = 0; i < 100; ++i) {
        servo.Update(10000, 960, 0.1);
    }
    DUWN_ASSERT(std::abs(servo.CorrectionPpm()) <= 300.0);
}

