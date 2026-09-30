#pragma once
// AudioEngine — receives RTP L16 audio from UxPlay sidecar (S16BE PCM stereo).
// Converts S16BE to internal float32 PCM.
// Resamples once to 48kHz stereo using AudioConverter only if needed.
// Pushes to AudioRingBuffer for WASAPI output.

#include "AudioRingBuffer.h"
#include "AudioClock.h"
#include "AudioConverter.h"
#include "AudioBufferController.h"
#include "AudioClockServo.h"
#include "airplay/StreamMetadata.h"
#include <vector>
#include <cstdint>
#include <functional>

namespace duwn::audio {

constexpr bool ShouldRecoverAudioDiscontinuity(double arrival_gap_ms,
                                                uint32_t pending_underruns) noexcept {
    return pending_underruns > 0 && arrival_gap_ms > 60.0;
}

class AudioEngine {
public:
    explicit AudioEngine(AudioRingBuffer& ring) noexcept;
    ~AudioEngine() = default;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Initialise based on stream parameters from AirPlay metadata.
    bool Init(const duwn::airplay::StreamMetadata& meta) noexcept;

    // Feed raw RTP audio payload (L16 S16BE PCM) + RTP timestamp.
    void Feed(const uint8_t* data, size_t size, uint32_t rtp_ts, int64_t arrival_ns) noexcept;

    void Flush() noexcept;
    void NotifyUnderrun() noexcept { m_pending_underruns.fetch_add(1, std::memory_order_relaxed); }

    bool InDiscontinuityRecovery() const noexcept { return m_in_discontinuity_recovery; }

    AudioClock& Clock() noexcept { return m_clock; }
    AudioBufferController& BufferController() noexcept { return m_buffer_controller; }
    const AudioBufferController& BufferController() const noexcept { return m_buffer_controller; }

private:
    AudioRingBuffer& m_ring;
    AudioClock       m_clock;
    AudioConverter   m_converter;
    AudioBufferController m_buffer_controller;
    AudioClockServo  m_servo;

    uint32_t         m_src_rate{44100};   // Default AirPlay RTP clock rate
    uint32_t         m_src_channels{2};   // Stereo
    bool             m_initialized{false};
    bool             m_controller_configured{false};
    std::atomic<uint32_t> m_pending_underruns{0};

    // Silence and Resume tracking
    int64_t          m_last_rtp_arrival_ns{0};
    uint32_t         m_last_rtp_ts{0};
    bool             m_in_silence{false};
    bool             m_needs_resume_crossfade{false};
    bool             m_in_discontinuity_recovery{false};
    uint32_t         m_consecutive_stable_packets{0};
    int64_t          m_recovery_start_ns{0};

    std::vector<float> m_src_pcm;         // Converted float32 PCM from S16BE
    std::vector<float> m_resampled_pcm;   // Output of resampler (if src_rate != 48000)
};

} // namespace duwn::audio
