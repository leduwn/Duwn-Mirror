#include "AudioEngine.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include <cstring>
#include <format>
#include <algorithm>

namespace duwn::audio {

AudioEngine::AudioEngine(AudioRingBuffer& ring) noexcept
    : m_ring(ring) {
    m_src_pcm.reserve(4096);
    m_resampled_pcm.reserve(4096);
}

bool AudioEngine::Init(const duwn::airplay::StreamMetadata& meta) noexcept {
    m_src_rate     = meta.audio_sample_rate > 0 ? meta.audio_sample_rate : 44100;
    m_src_channels = meta.audio_channels > 0 ? meta.audio_channels : 2;

    m_converter.Init(m_src_rate, m_src_channels, 48000, 2);
    m_buffer_controller.Reset();
    m_buffer_controller.SetSessionClass(AudioSessionClass::Startup);
    m_servo.Reset();
    duwn::GlobalMetrics().audio_arrival_gap_ms.store(0.0, std::memory_order_relaxed);
    duwn::GlobalMetrics().audio_arrival_gap_max_ms.store(0.0, std::memory_order_relaxed);
    m_controller_configured = false;
    m_initialized = true;

    DUWN_LOG_INFOF("AudioEngine",
        "Audio connected: Codec=L16 (S16BE PCM), RTP Clock={}Hz, Channels={}, Output Rate=48000Hz",
        m_src_rate, m_src_channels);

    return true;
}

void AudioEngine::Feed(const uint8_t* data, size_t size,
                        uint32_t rtp_ts, int64_t arrival_ns) noexcept {
    if (size < 4 || !data) return;

    int64_t now_ns = arrival_ns > 0 ? arrival_ns : duwn::clock::MonotonicClock::Now().time_since_epoch().count();
    duwn::GlobalMetrics().last_audio_rtp_arrival_ns.store(now_ns, std::memory_order_relaxed);

    double arrival_gap_ms = 0.0;
    if (m_last_rtp_arrival_ns > 0) {
        int64_t gap_ns = now_ns - m_last_rtp_arrival_ns;
        double gap_ms = static_cast<double>(gap_ns) / 1'000'000.0;
        arrival_gap_ms = gap_ms;
        auto& gap_metrics = duwn::GlobalMetrics();
        gap_metrics.audio_arrival_gap_ms.store(gap_ms, std::memory_order_relaxed);
        double peak_gap = gap_metrics.audio_arrival_gap_max_ms.load(std::memory_order_relaxed);
        while (gap_ms > peak_gap &&
               !gap_metrics.audio_arrival_gap_max_ms.compare_exchange_weak(
                   peak_gap, gap_ms, std::memory_order_relaxed)) {}

        if (gap_ms > 150.0) {
            // Audio resumed after silence
            duwn::GlobalMetrics().audio_resume_events.fetch_add(1, std::memory_order_relaxed);
            duwn::GlobalMetrics().audio_silence_duration_ms.store(gap_ms, std::memory_order_relaxed);

            int64_t submit_ns = duwn::clock::MonotonicClock::Now().time_since_epoch().count();
            double submit_delay_ms = static_cast<double>(submit_ns - now_ns) / 1'000'000.0;
            duwn::GlobalMetrics().resume_first_packet_to_submit_ms.store(submit_delay_ms, std::memory_order_relaxed);

            DUWN_LOG_INFOF("AudioEngine",
                "AUDIO RESUME: first RTP arrived after {:.1f} ms silence (timestamp={}, submit_delay={:.2f}ms)",
                gap_ms, rtp_ts, submit_delay_ms);

            m_needs_resume_crossfade = true;
            m_in_silence = false;
        }

        const uint32_t pending_underruns =
            m_pending_underruns.load(std::memory_order_relaxed);
        const bool long_resume = gap_ms > 250.0;
        const bool confirmed_short_discontinuity =
            ShouldRecoverAudioDiscontinuity(gap_ms, pending_underruns);
        if (long_resume || confirmed_short_discontinuity) {
            // A short gap is only treated as a discontinuity after WASAPI confirms
            // that it drained the ring. This avoids flushing valid buffered audio.
            // Rotation stress showed stale resume bursts can recur for tens of
            // seconds. The guard only rejects packets that would exceed the
            // adaptive emergency ceiling; normal in-order audio is unaffected.
            m_ring.Flush();
            m_converter.Reset();
            m_buffer_controller.Reset();
            m_servo.Reset();
            m_controller_configured = false;
            m_pending_underruns.store(0, std::memory_order_relaxed);
            m_in_discontinuity_recovery = true;
            m_buffer_controller.SetSessionClass(AudioSessionClass::DiscontinuityRecovery);
            m_consecutive_stable_packets = 0;
            m_recovery_start_ns = now_ns;
            gap_metrics.audio_arrival_gap_ms.store(0.0, std::memory_order_relaxed);
            gap_metrics.audio_arrival_gap_max_ms.store(0.0, std::memory_order_relaxed);
            duwn::GlobalMetrics().audio_discontinuity_recoveries.fetch_add(
                1, std::memory_order_relaxed);
            m_needs_resume_crossfade = true;
            DUWN_LOG_WARNF("AudioEngine",
                "AudioDiscontinuityRecovery: {:.1f}ms RTP gap, pending_underruns={}; entered state-based recovery",
                gap_ms, pending_underruns);
        }
    }
    m_last_rtp_arrival_ns = now_ns;
    m_last_rtp_ts = rtp_ts;

    if (!m_initialized) {
        m_src_rate     = 44100;
        m_src_channels = 2;
        m_converter.Init(m_src_rate, m_src_channels, 48000, 2);
        m_initialized = true;
        DUWN_LOG_INFOF("AudioEngine",
            "Audio stream auto-detected: Codec=L16 (S16BE PCM), RTP Clock={}Hz, Channels={}, Output Rate=48000Hz",
            m_src_rate, m_src_channels);
    }

    m_clock.Update(rtp_ts, m_src_rate);

    // Each stereo frame is 4 bytes: 2 bytes Left (S16BE) + 2 bytes Right (S16BE)
    size_t frame_bytes = m_src_channels * sizeof(int16_t);
    size_t in_frames = size / frame_bytes;
    if (in_frames == 0) return;
    auto& metrics = duwn::GlobalMetrics();
    auto now = [] { return duwn::clock::MonotonicClock::Now().time_since_epoch().count(); };
    const int64_t a1 = now();
    metrics.audio_packet_duration_ms.store(1000.0 * in_frames / m_src_rate, std::memory_order_relaxed);
    metrics.audio_a0_a1_ms.store(std::max(0.0, (a1 - now_ns) / 1'000'000.0), std::memory_order_relaxed);

    m_src_pcm.resize(in_frames * m_src_channels);

    for (size_t i = 0; i < in_frames; ++i) {
        size_t byte_idx = i * frame_bytes;
        // S16BE (network big-endian): byte 0 is MSB, byte 1 is LSB
        int16_t left_s16  = static_cast<int16_t>((data[byte_idx + 0] << 8) | data[byte_idx + 1]);
        int16_t right_s16 = static_cast<int16_t>((data[byte_idx + 2] << 8) | data[byte_idx + 3]);

        m_src_pcm[i * 2 + 0] = static_cast<float>(left_s16) / 32768.0f;
        m_src_pcm[i * 2 + 1] = static_cast<float>(right_s16) / 32768.0f;
    }
    const int64_t a2 = now();
    metrics.audio_a1_a2_ms.store((a2 - a1) / 1'000'000.0, std::memory_order_relaxed);

    const uint32_t estimated_output_frames = static_cast<uint32_t>(std::ceil(
        static_cast<double>(in_frames) * 48000.0 / m_src_rate));
    if (!m_controller_configured) {
        uint32_t period_frames = static_cast<uint32_t>(std::lround(
            metrics.audio_engine_period_ms.load(std::memory_order_relaxed) * 48.0));
        m_buffer_controller.Configure(estimated_output_frames,
                                      period_frames ? period_frames : 480);
        m_controller_configured = true;
    }
    const double packet_ms = 1000.0 * in_frames / m_src_rate;

    if (m_in_discontinuity_recovery) {
        // Enforce emergency ring ceiling only after confirmed discontinuity
        if (m_ring.Available() + estimated_output_frames > m_buffer_controller.MaximumFrames()) {
            metrics.audio_backlog_recovery_drops.fetch_add(
                estimated_output_frames, std::memory_order_relaxed);
            return;
        }

        // Stability tracking: packet arrived on time
        if (arrival_gap_ms > 0.0 && arrival_gap_ms <= packet_ms * 2.0) {
            ++m_consecutive_stable_packets;
        } else if (arrival_gap_ms > packet_ms * 3.0) {
            m_consecutive_stable_packets = 0;
        }

        // Recovery exit condition:
        // Stream has delivered >= 30 consecutive stable packets (~90-100ms stable stream)
        // AND ring buffer has settled back down near target,
        // OR timeout after 15 seconds to prevent permanent lock if stream has ongoing jitter.
        const bool stream_stable = m_consecutive_stable_packets >= 30;
        const bool ring_settled = m_ring.Available() <= (m_buffer_controller.TargetFrames() + m_buffer_controller.MinimumFrames() / 2);
        const bool recovery_timeout = (now_ns - m_recovery_start_ns) > 15'000'000'000LL;

        if ((stream_stable && ring_settled) || recovery_timeout) {
            m_in_discontinuity_recovery = false;
            m_buffer_controller.SetSessionClass(AudioSessionClass::NormalSteady);
            DUWN_LOG_INFOF("AudioEngine",
                "AudioDiscontinuityRecovery: exited recovery state (stable_packets={}, ring_ms={:.2f}, timeout={})",
                m_consecutive_stable_packets,
                1000.0 * m_ring.Available() / 48000.0,
                recovery_timeout);
        }
    }

    // Only observe continuous stream jitter (< 50ms). Delivery gaps and stalls
    // are handled by OnUnderrun() and discontinuity recovery, not jitter inflation.
    if (arrival_gap_ms > 0.0 && arrival_gap_ms < 50.0)
        m_buffer_controller.ObserveJitter(std::abs(arrival_gap_ms - packet_ms));
    const uint32_t pending_underruns = m_pending_underruns.exchange(0, std::memory_order_relaxed);
    if (pending_underruns > 0) m_buffer_controller.OnUnderrun();
    else m_buffer_controller.AdvanceStable(packet_ms / 1000.0);
    const double correction_ppm = m_servo.Update(
        m_ring.Available(), m_buffer_controller.TargetFrames(), packet_ms / 1000.0);
    m_converter.SetRateCorrectionPpm(correction_ppm);
    metrics.audio_target_buffer_ms.store(m_buffer_controller.TargetMs(), std::memory_order_relaxed);
    metrics.audio_servo_correction_ppm.store(correction_ppm, std::memory_order_relaxed);
    metrics.audio_resampler_group_delay_ms.store(
        1000.0 * m_converter.GroupDelayFrames() / m_src_rate, std::memory_order_relaxed);

    if (m_converter.IsPassthrough(m_src_rate, m_src_channels, 48000, 2)) {
        if (m_needs_resume_crossfade && in_frames > 0) {
            size_t fade_frames = std::min(static_cast<size_t>(144), in_frames);
            for (size_t i = 0; i < fade_frames; ++i) {
                float ramp = static_cast<float>(i) / static_cast<float>(fade_frames);
                m_src_pcm[i * 2 + 0] *= ramp;
                m_src_pcm[i * 2 + 1] *= ramp;
            }
            m_needs_resume_crossfade = false;
        }

        const int64_t a3 = now();
        uint32_t written = m_ring.Push(m_src_pcm.data(), static_cast<uint32_t>(in_frames));
        metrics.audio_ring_overrun_frames.fetch_add(in_frames - written, std::memory_order_relaxed);
        metrics.audio_a2_a3_ms.store((a3 - a2) / 1'000'000.0, std::memory_order_relaxed);
        metrics.audio_a3_a4_ms.store((now() - a3) / 1'000'000.0, std::memory_order_relaxed);
        duwn::GlobalMetrics().audio_samples_rendered.fetch_add(
            static_cast<uint64_t>(in_frames), std::memory_order_relaxed);
    } else {
        m_converter.Convert(m_src_pcm.data(), static_cast<uint32_t>(in_frames), m_resampled_pcm);
        const int64_t a3 = now();
        metrics.audio_a2_a3_ms.store((a3 - a2) / 1'000'000.0, std::memory_order_relaxed);
        uint32_t out_frames = static_cast<uint32_t>(m_resampled_pcm.size() / 2);
        if (out_frames > 0) {
            if (m_needs_resume_crossfade) {
                size_t fade_frames = std::min(static_cast<size_t>(144), static_cast<size_t>(out_frames));
                for (size_t i = 0; i < fade_frames; ++i) {
                    float ramp = static_cast<float>(i) / static_cast<float>(fade_frames);
                    m_resampled_pcm[i * 2 + 0] *= ramp;
                    m_resampled_pcm[i * 2 + 1] *= ramp;
                }
                m_needs_resume_crossfade = false;
            }

            uint32_t written = m_ring.Push(m_resampled_pcm.data(), out_frames);
            metrics.audio_ring_overrun_frames.fetch_add(out_frames - written, std::memory_order_relaxed);
            metrics.audio_a3_a4_ms.store((now() - a3) / 1'000'000.0, std::memory_order_relaxed);
            duwn::GlobalMetrics().audio_samples_rendered.fetch_add(
                static_cast<uint64_t>(out_frames), std::memory_order_relaxed);
        }
    }
}

void AudioEngine::Flush() noexcept {
    m_ring.Flush();
    m_converter.Reset();
    m_buffer_controller.Reset();
    m_buffer_controller.SetSessionClass(AudioSessionClass::Startup);
    m_servo.Reset();
    m_controller_configured = false;
    m_pending_underruns.store(0, std::memory_order_relaxed);
    m_in_discontinuity_recovery = false;
    m_consecutive_stable_packets = 0;
    m_recovery_start_ns = 0;
}

} // namespace duwn::audio
