#pragma once
// FreshestFrameSlot.h — Zero/Single-item thread-safe slot implementing DirectBufferingInvariant.
// Guarantees at most 1 pending freshest frame; older unconsumed frame is overwritten.

#include "DirectPacket.h"
#include <vector>
#include <optional>
#include <mutex>
#include <atomic>
#include <utility>

namespace duwn::direct {

// Assembled video or media frame payload
struct DirectFrame {
    uint32_t frame_id{0};
    uint16_t stream_id{0};
    uint8_t  payload_type{0};
    uint16_t flags{0};
    uint64_t source_timestamp_ns{0};
    std::vector<uint8_t> payload;
    bool is_keyframe{false};

    bool IsEmpty() const noexcept { return payload.empty(); }
    void Clear() noexcept {
        frame_id = 0;
        stream_id = 0;
        payload_type = 0;
        flags = 0;
        source_timestamp_ns = 0;
        payload.clear();
        is_keyframe = false;
    }
};

class FreshestFrameSlot {
public:
    FreshestFrameSlot() = default;
    ~FreshestFrameSlot() = default;

    // Non-copyable, non-movable
    FreshestFrameSlot(const FreshestFrameSlot&) = delete;
    FreshestFrameSlot& operator=(const FreshestFrameSlot&) = delete;

    // Puts a newly assembled frame into the slot.
    // If the slot already holds an unconsumed frame, it is immediately superseded.
    void Put(DirectFrame&& frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_slot.has_value()) {
            m_superseded_count.fetch_add(1, std::memory_order_relaxed);
        }
        m_slot = std::move(frame);
        m_pushed_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Takes the freshest frame out of the slot, leaving it empty.
    // Returns true if a frame was available; false if empty.
    bool Take(DirectFrame& out_frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_slot.has_value()) {
            return false;
        }
        out_frame = std::move(*m_slot);
        m_slot.reset();
        m_consumed_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::optional<DirectFrame> Take() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_slot.has_value()) {
            return std::nullopt;
        }
        DirectFrame frame = std::move(*m_slot);
        m_slot.reset();
        m_consumed_count.fetch_add(1, std::memory_order_relaxed);
        return frame;
    }

    // Checks if the slot currently contains a frame.
    bool HasFrame() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slot.has_value();
    }

    // Clears the slot without counting as consumed.
    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slot.reset();
    }

    uint64_t SupersededCount() const noexcept { return m_superseded_count.load(std::memory_order_relaxed); }
    uint64_t GetSupersededCount() const noexcept { return SupersededCount(); }
    uint64_t PushedCount() const noexcept { return m_pushed_count.load(std::memory_order_relaxed); }
    uint64_t ConsumedCount() const noexcept { return m_consumed_count.load(std::memory_order_relaxed); }

    void ResetMetrics() noexcept {
        m_superseded_count.store(0, std::memory_order_relaxed);
        m_pushed_count.store(0, std::memory_order_relaxed);
        m_consumed_count.store(0, std::memory_order_relaxed);
    }

private:
    mutable std::mutex m_mutex;
    std::optional<DirectFrame> m_slot;
    std::atomic<uint64_t> m_superseded_count{0};
    std::atomic<uint64_t> m_pushed_count{0};
    std::atomic<uint64_t> m_consumed_count{0};
};

} // namespace duwn::direct
