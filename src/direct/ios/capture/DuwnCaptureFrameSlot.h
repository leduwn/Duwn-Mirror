#pragma once
// DuwnCaptureFrameSlot.h — Thread-safe 1-frame latest-frame slot for iOS capture.
// Enforces CRITICAL INVARIANT:
// - Capture callback never blocks, never sends network, never encodes, never disk writes.
// - At most 1 pending captured frame.
// - Stale pending frame is replaced immediately when producer outruns consumer.
// - Tracks callback intervals, delivery delays, and frame replacement counts.

#include "DuwnCaptureTypes.h"
#include <mutex>
#include <optional>
#include <atomic>
#include <chrono>

namespace duwn::direct::ios {

struct CaptureSlotMetrics {
    uint64_t total_pushed_frames{0};
    uint64_t total_consumed_frames{0};
    uint64_t frame_replacement_count{0}; // Number of stale frames replaced before consumption
    uint64_t last_callback_interval_ns{0};
    uint64_t min_callback_interval_ns{UINT64_MAX};
    uint64_t max_callback_interval_ns{0};
    uint64_t last_delivery_delay_ns{0};
};

class DuwnCaptureFrameSlot {
public:
    DuwnCaptureFrameSlot() = default;
    ~DuwnCaptureFrameSlot() = default;

    // Non-copyable, non-movable
    DuwnCaptureFrameSlot(const DuwnCaptureFrameSlot&) = delete;
    DuwnCaptureFrameSlot& operator=(const DuwnCaptureFrameSlot&) = delete;

    // Puts a freshly captured frame into the 1-frame slot.
    // Must execute with near-zero latency (< 5 microseconds).
    // If a frame is already pending, it is replaced and counted as a replaced frame.
    void Put(CapturedVideoFrame&& frame) {
        uint64_t now_ns = GetMonotonicTimeNs();

        std::lock_guard<std::mutex> lock(m_mutex);

        // Interval measurement
        if (m_last_put_time_ns != 0 && now_ns > m_last_put_time_ns) {
            uint64_t interval = now_ns - m_last_put_time_ns;
            m_metrics.last_callback_interval_ns = interval;
            if (interval < m_metrics.min_callback_interval_ns) {
                m_metrics.min_callback_interval_ns = interval;
            }
            if (interval > m_metrics.max_callback_interval_ns) {
                m_metrics.max_callback_interval_ns = interval;
            }
        }
        m_last_put_time_ns = now_ns;

        // Delivery delay tracking (source PTS to callback entry)
        m_metrics.last_delivery_delay_ns = frame.timestamp.DeliveryDelayNs();

        // Check if an unconsumed frame is already in slot -> replace
        if (m_slot.has_value()) {
            m_metrics.frame_replacement_count++;
        }

        m_slot = std::move(frame);
        m_metrics.total_pushed_frames++;
    }

    // Takes the freshest frame from slot.
    // Returns true if a frame was available; false if empty.
    bool Take(CapturedVideoFrame& out_frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_slot.has_value()) {
            return false;
        }
        out_frame = std::move(*m_slot);
        m_slot.reset();
        m_metrics.total_consumed_frames++;
        return true;
    }

    // Checks if a frame is pending without extracting
    bool HasFrame() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slot.has_value();
    }

    // Drops any pending frame
    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slot.reset();
    }

    // Snapshot of metrics
    CaptureSlotMetrics GetMetrics() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_metrics;
    }

    void ResetMetrics() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics = {};
        m_last_put_time_ns = 0;
    }

private:
    static uint64_t GetMonotonicTimeNs() noexcept {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }

    mutable std::mutex m_mutex;
    std::optional<CapturedVideoFrame> m_slot;
    uint64_t m_last_put_time_ns{0};
    CaptureSlotMetrics m_metrics;
};

} // namespace duwn::direct::ios
