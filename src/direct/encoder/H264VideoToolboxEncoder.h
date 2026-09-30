#pragma once
// H264VideoToolboxEncoder.h — Hardware-accelerated Apple VideoToolbox H.264 low-latency encoder.
// Configured strictly for real-time: NO frame reordering, NO B-frames, zero frame lookahead delay.

#include "DirectVideoEncoder.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>

namespace duwn::direct {

class H264VideoToolboxEncoder : public DirectVideoEncoder {
public:
    H264VideoToolboxEncoder();
    ~H264VideoToolboxEncoder() override;

    // DirectVideoEncoder implementation
    bool Initialize(const DirectEncoderConfig& config) override;
    bool Reconfigure(uint32_t bitrate_target_bps, uint32_t fps) override;
    void RequestKeyframe() override;
    void SubmitFrame(RawVideoFrameInput&& frame) override;
    bool EncodeImmediate(const RawVideoFrameInput& frame) override;
    void Flush() override;
    void Close() override;
    void SetOutputCallback(std::function<void(EncodedVideoFrame&&)> callback) override;

    bool IsHardwareAccelerated() const noexcept override { return m_is_hardware; }
    DirectEncoderConfig GetConfig() const override;
    EncoderMetrics GetMetrics() const override;

    // Static capability query: checks if hardware H.264 encoder is available on current system
    static bool IsHardwareEncoderAvailable() noexcept;

    // Internal output callback bridge
    void OnCompressionOutput(int status,
                             unsigned int info_flags,
                             void* sample_buffer_ref,
                             uint64_t source_pts_ns,
                             uint64_t encode_start_ns,
                             uint64_t frame_seq);

private:
    void WorkerLoop();
    void UpdateLatencyMetrics(double latency_ms);

    DirectEncoderConfig m_config;
    std::atomic<bool> m_is_hardware{false};
    std::atomic<bool> m_force_keyframe{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};

    DirectEncoderInputSlot m_input_slot;
    std::thread m_worker_thread;
    std::condition_variable m_cv;
    std::mutex m_worker_mutex;

    mutable std::mutex m_state_mutex;
    EncoderMetrics m_metrics;
    std::vector<double> m_latency_history;
    std::function<void(EncodedVideoFrame&&)> m_output_callback;

    void* m_session{nullptr}; // VTCompressionSessionRef
};

} // namespace duwn::direct
