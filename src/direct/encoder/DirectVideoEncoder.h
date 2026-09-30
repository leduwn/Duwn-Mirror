#pragma once
// DirectVideoEncoder.h — Core interfaces and types for Duwn Direct Video Encoders.
// Enforces 0 or 1 latest-frame input slot, strictly no B-frames, and comprehensive telemetry.

#include "direct/DirectProtocol.h"
#include "direct/DirectSessionModel.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <optional>
#include <algorithm>

namespace duwn::direct {

// Encoder Latency Policy Mode
enum class DirectEncoderLatencyMode : uint8_t {
    LowestLatency = 0, // Minimal buffer delay, intra-refresh if supported, tighter rate control
    Balanced      = 1, // Standard low-latency real-time 60fps streaming
    HighQuality   = 2  // Maximizes sharpness and visual clarity within real-time bounds
};

// Hardware Video Encoder Configuration
struct DirectEncoderConfig {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{60};
    uint32_t bitrate_target_bps{12'000'000};  // 12 Mbps default for 1080p60
    uint32_t bitrate_ceiling_bps{18'000'000}; // 18 Mbps max burst ceiling
    float keyframe_interval_seconds{2.0f};     // IDR interval
    DirectEncoderLatencyMode latency_mode{DirectEncoderLatencyMode::LowestLatency};

    bool real_time{true};                       // Must be true
    bool allow_frame_reordering{false};         // Strictly false: NO B-frames
    bool enable_hardware_acceleration{true};    // Require hardware encoder
    uint32_t max_frame_delay_count{0};          // 0 = emit immediately without buffering
};

// Encoded Video Frame Representation
struct EncodedVideoFrame {
    uint64_t frame_id{0};
    bool is_keyframe{false};
    std::vector<uint8_t> payload; // Annex-B or AVCC format NAL units
    uint64_t source_timestamp_ns{0};
    uint64_t encode_start_timestamp_ns{0};
    uint64_t encode_end_timestamp_ns{0};
    uint32_t width{0};
    uint32_t height{0};
    float estimated_qp{0.0f};

    uint64_t EncodeDurationNs() const noexcept {
        return (encode_end_timestamp_ns > encode_start_timestamp_ns)
            ? (encode_end_timestamp_ns - encode_start_timestamp_ns)
            : 0;
    }
};

// Encoder Latency and Throughput Telemetry
struct EncoderMetrics {
    uint64_t total_input_frames{0};
    uint64_t encoded_frames_count{0};
    uint64_t superseded_input_frames{0}; // Frames replaced because encoder was busy
    uint64_t keyframe_count{0};
    uint64_t total_encoded_bytes{0};
    uint32_t current_bitrate_bps{0};

    // Encode Latency in milliseconds
    double last_encode_latency_ms{0.0};
    double p50_encode_latency_ms{0.0};
    double p95_encode_latency_ms{0.0};
    double p99_encode_latency_ms{0.0};
    double max_encode_latency_ms{0.0};
};

// Raw frame input for encoder (holds raw buffer pointer / handle)
struct RawVideoFrameInput {
    void* buffer_handle{nullptr}; // CVPixelBufferRef or synthetic buffer handle
    uint32_t width{0};
    uint32_t height{0};
    uint64_t source_timestamp_ns{0};
    uint64_t frame_sequence{0};
    bool force_keyframe{false};
};

// 1-Frame Latest-Frame Input Slot for Video Encoder
// CRITICAL INVARIANT:
// - Never build a multi-frame latency queue.
// - At most 0 or 1 pending frame awaiting compression.
// - If encoder falls behind, the pending frame is superseded immediately.
class DirectEncoderInputSlot {
public:
    DirectEncoderInputSlot() = default;
    ~DirectEncoderInputSlot() = default;

    DirectEncoderInputSlot(const DirectEncoderInputSlot&) = delete;
    DirectEncoderInputSlot& operator=(const DirectEncoderInputSlot&) = delete;

    void Put(RawVideoFrameInput&& frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_total_input_frames++;
        if (m_slot.has_value()) {
            m_superseded_frames++;
        }
        m_slot = std::move(frame);
    }

    bool Take(RawVideoFrameInput& out_frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_slot.has_value()) {
            return false;
        }
        out_frame = std::move(*m_slot);
        m_slot.reset();
        return true;
    }

    bool HasFrame() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slot.has_value();
    }

    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slot.reset();
    }

    uint64_t GetTotalInputFrames() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_total_input_frames;
    }

    uint64_t GetSupersededCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_superseded_frames;
    }

    void ResetMetrics() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_total_input_frames = 0;
        m_superseded_frames = 0;
    }

private:
    mutable std::mutex m_mutex;
    std::optional<RawVideoFrameInput> m_slot;
    uint64_t m_total_input_frames{0};
    uint64_t m_superseded_frames{0};
};

// Abstract Hardware Video Encoder Interface
class DirectVideoEncoder {
public:
    virtual ~DirectVideoEncoder() = default;

    // Initializes session with given configuration
    virtual bool Initialize(const DirectEncoderConfig& config) = 0;

    // Reconfigures bitrate / framerate dynamically without recreating session
    virtual bool Reconfigure(uint32_t bitrate_target_bps, uint32_t fps) = 0;

    // Requests an immediate IDR keyframe on next encode
    virtual void RequestKeyframe() = 0;

    // Submits a frame to the 1-frame input slot (non-blocking)
    virtual void SubmitFrame(RawVideoFrameInput&& frame) = 0;

    // Direct synchronous or asynchronous encode call
    virtual bool EncodeImmediate(const RawVideoFrameInput& frame) = 0;

    // Flushes pending frames
    virtual void Flush() = 0;

    // Closes and releases encoder session
    virtual void Close() = 0;

    // Sets callback for emitted encoded frames
    virtual void SetOutputCallback(std::function<void(EncodedVideoFrame&&)> callback) = 0;

    // Accessors
    virtual bool IsHardwareAccelerated() const noexcept = 0;
    virtual DirectEncoderConfig GetConfig() const = 0;
    virtual EncoderMetrics GetMetrics() const = 0;
};

} // namespace duwn::direct
