#pragma once
// AudioRingBuffer — lock-free bounded ring buffer for float32 PCM.
// Single producer, single consumer (SPSC).
// Frame = one sample per channel. Buffer holds N frames.
// Internal format: interleaved float32, 2 channels, 48 kHz.

#include <atomic>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace duwn::audio {

class AudioRingBuffer {
public:
    // capacity_frames: must be power of 2 for mask-based wrap.
    // 4800 frames = 100ms at 48kHz. Start with 8192 frames (~170ms).
    explicit AudioRingBuffer(uint32_t capacity_frames = 8192,
                              uint32_t channels        = 2) noexcept;

    // Push interleaved PCM frames.
    // Returns number of frames actually written (may be < count if near-full).
    uint32_t Push(const float* frames, uint32_t count) noexcept;

    // Pull interleaved PCM frames into dst.
    // Returns number of frames actually read (may be < count if near-empty → silence).
    uint32_t Pull(float* dst, uint32_t count) noexcept;

    // Frames available for reading.
    uint32_t Available() const noexcept;

    // Frames of free space for writing.
    uint32_t FreeSpace() const noexcept;

    // Clear all buffered data.
    void Flush() noexcept;

    // Discard oldest frames if available exceeds keep_frames (caps buffer queue).
    void DiscardOldest(uint32_t keep_frames) noexcept;

    uint32_t Capacity() const noexcept { return m_capacity; }
    uint32_t Channels() const noexcept { return m_channels; }

private:
    std::vector<float>   m_buf;   // interleaved samples: size = capacity * channels
    const uint32_t       m_capacity;
    const uint32_t       m_channels;
    const uint32_t       m_mask;  // capacity - 1 (for fast modulo, requires power-of-2)

    // read_idx and write_idx count FRAMES (not samples).
    std::atomic<uint32_t> m_read_idx{0};
    std::atomic<uint32_t> m_write_idx{0};
};

} // namespace duwn::audio
