#include "AudioRingBuffer.h"
#include <cassert>
#include <cstring>

namespace duwn::audio {

static uint32_t NextPow2(uint32_t v) noexcept {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

AudioRingBuffer::AudioRingBuffer(uint32_t capacity_frames, uint32_t channels) noexcept
    : m_capacity(NextPow2(capacity_frames))
    , m_channels(channels)
    , m_mask(NextPow2(capacity_frames) - 1) {
    m_buf.resize(static_cast<size_t>(m_capacity) * channels, 0.0f);
}

uint32_t AudioRingBuffer::Available() const noexcept {
    uint32_t w = m_write_idx.load(std::memory_order_acquire);
    uint32_t r = m_read_idx.load(std::memory_order_acquire);
    return w - r; // wraps safely with unsigned arithmetic
}

uint32_t AudioRingBuffer::FreeSpace() const noexcept {
    return m_capacity - Available();
}

uint32_t AudioRingBuffer::Push(const float* frames, uint32_t count) noexcept {
    uint32_t free  = FreeSpace();
    uint32_t write = std::min(count, free);
    if (write == 0) return 0;

    uint32_t w = m_write_idx.load(std::memory_order_relaxed) & m_mask;

    for (uint32_t i = 0; i < write; ++i) {
        uint32_t idx = ((w + i) & m_mask) * m_channels;
        for (uint32_t c = 0; c < m_channels; ++c)
            m_buf[idx + c] = frames[i * m_channels + c];
    }

    m_write_idx.fetch_add(write, std::memory_order_release);
    return write;
}

uint32_t AudioRingBuffer::Pull(float* dst, uint32_t count) noexcept {
    uint32_t avail = Available();
    uint32_t read  = std::min(count, avail);

    // Underrun: fill with silence
    if (read < count) {
        std::fill_n(dst + read * m_channels,
                    static_cast<size_t>((count - read) * m_channels), 0.0f);
    }

    if (read == 0) return 0;

    uint32_t r = m_read_idx.load(std::memory_order_relaxed) & m_mask;
    for (uint32_t i = 0; i < read; ++i) {
        uint32_t idx = ((r + i) & m_mask) * m_channels;
        for (uint32_t c = 0; c < m_channels; ++c)
            dst[i * m_channels + c] = m_buf[idx + c];
    }

    m_read_idx.fetch_add(read, std::memory_order_release);
    return read;
}

void AudioRingBuffer::Flush() noexcept {
    uint32_t w = m_write_idx.load(std::memory_order_acquire);
    m_read_idx.store(w, std::memory_order_release);
}

} // namespace duwn::audio
