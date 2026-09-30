// Tests for AudioConverter

#include "audio/AudioConverter.h"
#include <vector>
#include <cmath>

DUWN_TEST(audio_converter_passthrough_48k) {
    duwn::audio::AudioConverter conv;
    conv.Init(48000, 2, 48000, 2);
    DUWN_ASSERT(conv.IsPassthrough(48000, 2, 48000, 2));

    std::vector<float> input = {0.1f, -0.2f, 0.5f, -0.5f};
    std::vector<float> output;
    conv.Convert(input.data(), 2, output);

    DUWN_ASSERT(output.size() == 4);
    DUWN_ASSERT(output[0] == 0.1f);
    DUWN_ASSERT(output[1] == -0.2f);
    DUWN_ASSERT(output[2] == 0.5f);
    DUWN_ASSERT(output[3] == -0.5f);
}

DUWN_TEST(audio_converter_resample_44k_to_48k) {
    duwn::audio::AudioConverter conv;
    conv.Init(44100, 2, 48000, 2);
    DUWN_ASSERT(!conv.IsPassthrough(44100, 2, 48000, 2));

    // 441 input frames (10ms of audio at 44.1kHz)
    constexpr uint32_t kInFrames = 441;
    std::vector<float> input(kInFrames * 2);
    for (uint32_t i = 0; i < kInFrames; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / 44100.0f));
        input[i * 2 + 0] = val;
        input[i * 2 + 1] = val;
    }

    std::vector<float> output;
    conv.Convert(input.data(), kInFrames, output);

    // The streaming sinc filter retains a fixed 16-source-frame lookahead.
    uint32_t out_frames = static_cast<uint32_t>(output.size() / 2);
    DUWN_ASSERT(out_frames >= 460 && out_frames <= 465);

    // Check no NaN or out-of-range samples
    for (float s : output) {
        DUWN_ASSERT(!std::isnan(s));
        DUWN_ASSERT(s >= -1.05f && s <= 1.05f);
    }
}

DUWN_TEST(audio_converter_streaming_continuity) {
    duwn::audio::AudioConverter conv;
    conv.Init(44100, 2, 48000, 2);
    constexpr uint32_t frames = 441;
    std::vector<float> a(frames * 2), b(frames * 2);
    for (uint32_t i = 0; i < frames * 2; ++i) {
        const uint32_t frame = i / 2;
        a[i] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 440.0 * frame / 44100.0));
        b[i] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 440.0 * (frame + frames) / 44100.0));
    }
    std::vector<float> out_a, out_b;
    conv.Convert(a.data(), frames, out_a);
    conv.Convert(b.data(), frames, out_b);
    DUWN_ASSERT(!out_a.empty() && !out_b.empty());
    DUWN_ASSERT(std::abs(out_b[0] - out_a[out_a.size() - 2]) < 0.15f);
    const uint32_t total_frames = static_cast<uint32_t>((out_a.size() + out_b.size()) / 2);
    DUWN_ASSERT(total_frames >= 940 && total_frames <= 945);
}
