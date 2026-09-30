#include "AudioClockServo.h"

#include <algorithm>
#include <cmath>

namespace duwn::audio {

namespace {
constexpr double kNormalLimitPpm = 300.0;
constexpr double kProportionalPpm = 180.0;
constexpr double kIntegralPpmPerSecond = 35.0;
constexpr double kSmoothingSeconds = 0.75;
}

void AudioClockServo::Reset() noexcept {
    m_integral = 0.0;
    m_correction_ppm = 0.0;
}

double AudioClockServo::Update(uint32_t fill_frames, uint32_t target_frames,
                               double elapsed_seconds) noexcept {
    if (target_frames == 0 || !std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0)
        return m_correction_ppm;

    const double error = (static_cast<double>(fill_frames) - target_frames) / target_frames;
    m_integral = std::clamp(m_integral + error * elapsed_seconds, -4.0, 4.0);
    // Positive fill error means the producer is running fast. Produce fewer output
    // frames per input frame, hence a negative resampling-ratio correction.
    const double requested = std::clamp(
        -(kProportionalPpm * error + kIntegralPpmPerSecond * m_integral),
        -kNormalLimitPpm, kNormalLimitPpm);
    const double alpha = 1.0 - std::exp(-elapsed_seconds / kSmoothingSeconds);
    m_correction_ppm += (requested - m_correction_ppm) * alpha;
    m_correction_ppm = std::clamp(m_correction_ppm, -kNormalLimitPpm, kNormalLimitPpm);
    return m_correction_ppm;
}

} // namespace duwn::audio
