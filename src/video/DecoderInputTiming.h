#pragma once

#include <array>
#include <cstdint>

namespace duwn::video {

struct DecoderInputTiming {
    int64_t sample_time{0}; // MF 100-ns units, used only as a correlation key.
    int64_t rtp_arrival_qpc{0};
    int64_t au_received_qpc{0};
    int64_t process_input_qpc{0};
};

// Decode-thread owned; delayed/reordered output must match its accepted input.
class DecoderTimingTracker {
public:
    void Push(DecoderInputTiming timing) noexcept {
        m_pending[m_next] = timing;
        m_next = (m_next + 1) % m_pending.size();
    }

    DecoderInputTiming Take(int64_t sample_time) noexcept {
        DecoderInputTiming result{};
        size_t matches = 0;
        for (auto& timing : m_pending) {
            if (timing.process_input_qpc > 0 && timing.sample_time == sample_time) {
                result = timing;
                timing = {};
                ++matches;
            }
        }
        // Missing, evicted or ambiguous timestamps are unavailable, never guessed.
        return matches == 1 ? result : DecoderInputTiming{};
    }

    void Reset() noexcept { m_pending = {}; m_next = 0; }

private:
    std::array<DecoderInputTiming, 64> m_pending{};
    size_t m_next{0};
};

} // namespace duwn::video
