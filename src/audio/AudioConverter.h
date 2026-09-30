#pragma once
// AudioConverter — sample rate converter for audio streaming.
// Supports passthrough when src == dst (48kHz stereo).
// When source differs (e.g. 44.1kHz from AirPlay RTP L16):
// Uses a continuous windowed-sinc resampler with bounded group delay.

#include <cstdint>
#include <vector>

namespace duwn::audio {

class AudioConverter {
public:
    static bool IsPassthrough(uint32_t src_rate, uint32_t src_ch,
                               uint32_t dst_rate, uint32_t dst_ch) noexcept;

    bool Init(uint32_t src_rate, uint32_t src_ch,
              uint32_t dst_rate, uint32_t dst_ch) noexcept;

    // Convert interleaved float32 frames.
    // Returns resampled frames in output.
    void Convert(const float* input, uint32_t input_frames,
                 std::vector<float>& output) noexcept;

    void SetRateCorrectionPpm(double ppm) noexcept;
    void Reset() noexcept;
    uint32_t GroupDelayFrames() const noexcept { return 16; }

private:
    uint32_t m_src_rate{0}, m_src_ch{0};
    uint32_t m_dst_rate{0}, m_dst_ch{0};
    std::vector<float> m_input;
    double m_source_pos{0.0};
    double m_rate_correction_ppm{0.0};
};

} // namespace duwn::audio
