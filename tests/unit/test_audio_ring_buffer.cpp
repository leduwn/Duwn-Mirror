// Tests for AudioRingBuffer — SPSC correctness and stereo channel integrity

#include "audio/AudioRingBuffer.h"
#include <vector>

DUWN_TEST(audio_ring_push_pull_roundtrip) {
    duwn::audio::AudioRingBuffer ring{1024, 2};
    float input[] = {0.1f, 0.2f,  0.3f, 0.4f};  // 2 frames, 2ch interleaved
    ring.Push(input, 2);
    float output[4]{};
    uint32_t pulled = ring.Pull(output, 2);
    DUWN_ASSERT(pulled == 2);
    DUWN_ASSERT(std::abs(output[0] - 0.1f) < 1e-6f); // L ch frame 0
    DUWN_ASSERT(std::abs(output[1] - 0.2f) < 1e-6f); // R ch frame 0
    DUWN_ASSERT(std::abs(output[2] - 0.3f) < 1e-6f); // L ch frame 1
    DUWN_ASSERT(std::abs(output[3] - 0.4f) < 1e-6f); // R ch frame 1
}

DUWN_TEST(audio_ring_stereo_left_only) {
    // Left-channel tone: L=1.0, R=0.0
    duwn::audio::AudioRingBuffer ring{512, 2};
    float frames[200];
    for (int i = 0; i < 100; ++i) {
        frames[i * 2 + 0] = 1.0f; // L
        frames[i * 2 + 1] = 0.0f; // R
    }
    ring.Push(frames, 100);
    float out[200]{};
    ring.Pull(out, 100);
    for (int i = 0; i < 100; ++i) {
        DUWN_ASSERT(std::abs(out[i * 2 + 0] - 1.0f) < 1e-6f); // L must be 1
        DUWN_ASSERT(std::abs(out[i * 2 + 1] - 0.0f) < 1e-6f); // R must be 0
    }
}

DUWN_TEST(audio_ring_stereo_right_only) {
    duwn::audio::AudioRingBuffer ring{512, 2};
    float frames[200];
    for (int i = 0; i < 100; ++i) {
        frames[i * 2 + 0] = 0.0f; // L
        frames[i * 2 + 1] = 1.0f; // R
    }
    ring.Push(frames, 100);
    float out[200]{};
    ring.Pull(out, 100);
    for (int i = 0; i < 100; ++i) {
        DUWN_ASSERT(std::abs(out[i * 2 + 0] - 0.0f) < 1e-6f); // L must be 0
        DUWN_ASSERT(std::abs(out[i * 2 + 1] - 1.0f) < 1e-6f); // R must be 1
    }
}

DUWN_TEST(audio_ring_underrun_silence) {
    duwn::audio::AudioRingBuffer ring{512, 2};
    float out[10]{};
    // Ring is empty — should fill with silence
    ring.Pull(out, 5);
    for (auto v : out) DUWN_ASSERT(v == 0.0f);
}

DUWN_TEST(audio_ring_capacity) {
    duwn::audio::AudioRingBuffer ring{8, 2}; // capacity=8 (next pow2)
    DUWN_ASSERT(ring.Capacity() == 8);
    DUWN_ASSERT(ring.FreeSpace() == 8);
    float frames[16]{1.0f};
    ring.Push(frames, 8);
    DUWN_ASSERT(ring.Available() == 8);
    DUWN_ASSERT(ring.FreeSpace() == 0);
}

DUWN_TEST(audio_ring_partial_pull_zero_fill) {
    duwn::audio::AudioRingBuffer ring{512, 2};
    float input[] = {0.7f, 0.8f, 0.9f, 1.0f}; // 2 stereo frames
    ring.Push(input, 2);

    float output[10];
    std::fill(std::begin(output), std::end(output), -999.0f);

    // Request 5 frames: 2 frames exist, 3 frames must be zero-filled
    uint32_t pulled = ring.Pull(output, 5);
    DUWN_ASSERT(pulled == 2);

    // Frame 0
    DUWN_ASSERT(std::abs(output[0] - 0.7f) < 1e-6f);
    DUWN_ASSERT(std::abs(output[1] - 0.8f) < 1e-6f);
    // Frame 1
    DUWN_ASSERT(std::abs(output[2] - 0.9f) < 1e-6f);
    DUWN_ASSERT(std::abs(output[3] - 1.0f) < 1e-6f);

    // Remaining 3 frames (6 floats) must be exact zero-fill
    for (int i = 4; i < 10; ++i) {
        DUWN_ASSERT(output[i] == 0.0f);
    }
}

DUWN_TEST(audio_ring_pipeline_preservation_after_flush) {
    duwn::audio::AudioRingBuffer ring{256, 2};
    float frames[8]{0.5f};
    ring.Push(frames, 4);
    DUWN_ASSERT(ring.Available() == 4);

    // Flush should reset occupancy but preserve ring parameters
    ring.Flush();
    DUWN_ASSERT(ring.Available() == 0);
    DUWN_ASSERT(ring.Capacity() == 256);
    DUWN_ASSERT(ring.Channels() == 2);

    // Push new frames after flush works immediately
    float new_frames[] = {0.2f, 0.3f};
    ring.Push(new_frames, 1);
    DUWN_ASSERT(ring.Available() == 1);

    float out[2]{};
    uint32_t pulled = ring.Pull(out, 1);
    DUWN_ASSERT(pulled == 1);
    DUWN_ASSERT(std::abs(out[0] - 0.2f) < 1e-6f);
    DUWN_ASSERT(std::abs(out[1] - 0.3f) < 1e-6f);
}
