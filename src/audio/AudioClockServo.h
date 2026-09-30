#pragma once

#include <cstdint>

namespace duwn::audio {

class AudioClockServo {
public:
    void Reset() noexcept;
    double Update(uint32_t fill_frames, uint32_t target_frames,
                  double elapsed_seconds) noexcept;
    double CorrectionPpm() const noexcept { return m_correction_ppm; }

private:
    double m_integral{0.0};
    double m_correction_ppm{0.0};
};

} // namespace duwn::audio
