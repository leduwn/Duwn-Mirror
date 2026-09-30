#include "AudioConverter.h"

#include <algorithm>
#include <cmath>

namespace duwn::audio {

bool AudioConverter::IsPassthrough(uint32_t src_rate, uint32_t src_ch,
                                    uint32_t dst_rate, uint32_t dst_ch) noexcept {
    return src_rate == dst_rate && src_ch == dst_ch;
}

bool AudioConverter::Init(uint32_t src_rate, uint32_t src_ch,
                           uint32_t dst_rate, uint32_t dst_ch) noexcept {
    m_src_rate = src_rate;
    m_src_ch   = src_ch;
    m_dst_rate = dst_rate;
    m_dst_ch   = dst_ch;
    Reset();
    return true;
}

void AudioConverter::Reset() noexcept {
    m_input.clear();
    m_source_pos = 0.0;
    m_rate_correction_ppm = 0.0;
}

void AudioConverter::SetRateCorrectionPpm(double ppm) noexcept {
    if (!std::isfinite(ppm)) return;
    m_rate_correction_ppm = std::clamp(ppm, -300.0, 300.0);
}

void AudioConverter::Convert(const float* input, uint32_t input_frames,
                              std::vector<float>& output) noexcept {
    if (input_frames == 0 || !input) {
        output.clear();
        return;
    }

    if (IsPassthrough(m_src_rate, m_src_ch, m_dst_rate, m_dst_ch)) {
        output.assign(input, input + static_cast<size_t>(input_frames) * m_src_ch);
        return;
    }

    if (m_src_rate == 0 || m_dst_rate == 0 || m_src_ch != 2 || m_dst_ch != 2) {
        output.clear();
        return;
    }

    m_input.insert(m_input.end(), input, input + static_cast<size_t>(input_frames) * 2);
    output.clear();

    constexpr int kHalf = 16;
    constexpr double kPi = 3.14159265358979323846;
    const size_t available = m_input.size() / 2;
    const double cutoff = 0.94 * std::min(1.0,
        static_cast<double>(m_dst_rate) / static_cast<double>(m_src_rate));
    const double corrected_dst = static_cast<double>(m_dst_rate) *
                                 (1.0 + m_rate_correction_ppm / 1'000'000.0);
    const double step = static_cast<double>(m_src_rate) / corrected_dst;
    const size_t reserve_frames = static_cast<size_t>(input_frames / step) + 4;
    output.reserve(reserve_frames * 2);

    while (m_source_pos + kHalf < static_cast<double>(available)) {
        const int center = static_cast<int>(std::floor(m_source_pos));
        const double frac = m_source_pos - center;
        double sum_l = 0.0, sum_r = 0.0, weight_sum = 0.0;
        for (int tap = -kHalf; tap <= kHalf; ++tap) {
            const int sample_index = std::clamp(center + tap, 0,
                                                static_cast<int>(available) - 1);
            const double distance = static_cast<double>(tap) - frac;
            const double x = distance * cutoff;
            const double sinc = std::abs(x) < 1.0e-12
                ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double window = 0.5 + 0.5 * std::cos(
                kPi * distance / static_cast<double>(kHalf + 1));
            const double weight = cutoff * sinc * window;
            sum_l += m_input[static_cast<size_t>(sample_index) * 2] * weight;
            sum_r += m_input[static_cast<size_t>(sample_index) * 2 + 1] * weight;
            weight_sum += weight;
        }
        if (std::abs(weight_sum) > 1.0e-12) {
            sum_l /= weight_sum;
            sum_r /= weight_sum;
        }
        output.push_back(static_cast<float>(std::clamp(sum_l, -1.0, 1.0)));
        output.push_back(static_cast<float>(std::clamp(sum_r, -1.0, 1.0)));
        m_source_pos += step;
    }

    const size_t removable = m_source_pos > (kHalf + 1)
        ? static_cast<size_t>(m_source_pos) - kHalf : 0;
    if (removable > 0) {
        m_input.erase(m_input.begin(), m_input.begin() + removable * 2);
        m_source_pos -= removable;
    }
}

} // namespace duwn::audio
