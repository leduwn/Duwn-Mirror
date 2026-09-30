#pragma once
// MockVideoEncoder.h — Mock / test encoder for validating DirectVideoEncoder invariants.
// Simulates hardware encoding with deterministic latency, verifying:
// - 0 or 1 latest-frame input slot
// - No B-frame reordering (strictly monotonic PTS)
// - Latency bounds (P50/P95/P99)
// - No queue growth under backpressure

#include "direct/encoder/DirectVideoEncoder.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>

namespace duwn::direct::test {

class MockVideoEncoder : public DirectVideoEncoder {
public:
    MockVideoEncoder() = default;
    ~MockVideoEncoder() override { Close(); }

    bool Initialize(const DirectEncoderConfig& config) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = config;
        m_initialized = true;
        return true;
    }

    bool Reconfigure(uint32_t bitrate_target_bps, uint32_t fps) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.bitrate_target_bps = bitrate_target_bps;
        m_config.fps = fps;
        return true;
    }

    void RequestKeyframe() override {
        m_force_keyframe.store(true, std::memory_order_release);
    }

    void SubmitFrame(RawVideoFrameInput&& frame) override {
        m_input_slot.Put(std::move(frame));
    }

    bool EncodeImmediate(const RawVideoFrameInput& frame) override {
        if (!m_initialized) return false;

        uint64_t start_ns = frame.source_timestamp_ns;
        // Simulated hardware encode delay: 3.5ms nominal, 5.0ms on keyframe
        bool is_keyframe = (frame.frame_sequence % static_cast<uint64_t>(m_config.fps * m_config.keyframe_interval_seconds) == 0)
                           || frame.force_keyframe
                           || m_force_keyframe.exchange(false, std::memory_order_acq_rel);

        uint64_t delay_ns = is_keyframe ? 5'000'000ULL : 3'500'000ULL;
        uint64_t end_ns = start_ns + delay_ns;

        EncodedVideoFrame enc;
        enc.frame_id = frame.frame_sequence;
        enc.is_keyframe = is_keyframe;
        enc.source_timestamp_ns = frame.source_timestamp_ns;
        enc.encode_start_timestamp_ns = start_ns;
        enc.encode_end_timestamp_ns = end_ns;
        enc.width = m_config.width;
        enc.height = m_config.height;

        // Synthetic NAL payload
        size_t payload_size = is_keyframe ? 64'000 : 25'000;
        enc.payload.assign(payload_size, static_cast<uint8_t>(is_keyframe ? 0xAA : 0xBB));

        double latency_ms = static_cast<double>(delay_ns) / 1'000'000.0;

        std::function<void(EncodedVideoFrame&&)> cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_metrics.encoded_frames_count++;
            m_metrics.total_encoded_bytes += payload_size;
            if (is_keyframe) m_metrics.keyframe_count++;
            UpdateLatency(latency_ms);
            cb = m_output_callback;
        }

        if (cb) {
            cb(std::move(enc));
        }

        return true;
    }

    // Pulls and encodes one frame from the input slot
    bool ProcessOneSlotFrame() {
        RawVideoFrameInput frame;
        if (m_input_slot.Take(frame)) {
            return EncodeImmediate(frame);
        }
        return false;
    }

    void Flush() override {
        RawVideoFrameInput frame;
        while (m_input_slot.Take(frame)) {
            EncodeImmediate(frame);
        }
    }

    void Close() override {
        m_initialized = false;
        m_input_slot.Clear();
    }

    void SetOutputCallback(std::function<void(EncodedVideoFrame&&)> callback) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output_callback = std::move(callback);
    }

    bool IsHardwareAccelerated() const noexcept override { return true; }
    DirectEncoderConfig GetConfig() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config;
    }

    EncoderMetrics GetMetrics() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        EncoderMetrics m = m_metrics;
        m.total_input_frames = m_input_slot.GetTotalInputFrames();
        m.superseded_input_frames = m_input_slot.GetSupersededCount();
        return m;
    }

    DirectEncoderInputSlot& GetInputSlot() noexcept { return m_input_slot; }

private:
    void UpdateLatency(double latency_ms) {
        m_metrics.last_encode_latency_ms = latency_ms;
        if (latency_ms > m_metrics.max_encode_latency_ms) {
            m_metrics.max_encode_latency_ms = latency_ms;
        }
        m_latencies.push_back(latency_ms);
        if (m_latencies.size() > 600) {
            m_latencies.erase(m_latencies.begin());
        }

        auto sorted = m_latencies;
        std::sort(sorted.begin(), sorted.end());
        size_t count = sorted.size();
        if (count > 0) {
            m_metrics.p50_encode_latency_ms = sorted[count * 50 / 100];
            m_metrics.p95_encode_latency_ms = sorted[count * 95 / 100];
            m_metrics.p99_encode_latency_ms = sorted[count * 99 / 100];
        }
    }

    mutable std::mutex m_mutex;
    bool m_initialized{false};
    DirectEncoderConfig m_config;
    std::atomic<bool> m_force_keyframe{false};
    DirectEncoderInputSlot m_input_slot;
    EncoderMetrics m_metrics;
    std::vector<double> m_latencies;
    std::function<void(EncodedVideoFrame&&)> m_output_callback;
};

} // namespace duwn::direct::test
