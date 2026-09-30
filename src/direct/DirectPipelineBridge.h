#pragma once
// DirectPipelineBridge.h — Unified Bridge connecting Duwn Direct Mode to Windows VideoDecoder.
// Ingests assembled DirectFrame objects, converts them into RFC 6184 / Annex-B EncodedAccessUnit,
// and routes them directly into VideoDecoder::FeedAccessUnit without duplicating decoder or render infrastructure.
// Integrates 10-point latency telemetry (S0..S4, R0..R4).

#include "direct/DirectReceiver.h"
#include "direct/FreshestFrameSlot.h"
#include "direct/DirectLatencyTracker.h"
#include "video/VideoDecoder.h"
#include "video/EncodedAccessUnit.h"
#include <cstdint>
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace duwn::direct {

class DirectPipelineBridge {
public:
    explicit DirectPipelineBridge(video::VideoDecoder& decoder,
                                  DirectLatencyTracker* latency_tracker = nullptr);
    ~DirectPipelineBridge();

    // Stream negotiation configuration
    void SetStreamFormat(uint32_t width, uint32_t height, uint32_t fps,
                         video::VideoCodecType codec = video::VideoCodecType::H264) noexcept;
    void SetCodec(video::VideoCodecType codec) noexcept { m_codec = codec; }
    video::VideoCodecType GetCodec() const noexcept { return m_codec; }
    uint32_t GetWidth() const noexcept { return m_width; }
    uint32_t GetHeight() const noexcept { return m_height; }
    uint32_t GetFps() const noexcept { return m_fps; }

    // Direct Access Unit Ingest
    // Accepts an assembled DirectFrame, transforms to EncodedAccessUnit,
    // records R1 and R2, and feeds VideoDecoder::FeedAccessUnit.
    bool FeedDirectFrame(DirectFrame frame, uint64_t r0_packet_arrival_ns = 0);

    // Pulls freshest frame from slot and feeds decoder (latest frame policy)
    bool ProcessFreshestFrame(FreshestFrameSlot& slot, uint64_t r0_packet_arrival_ns = 0);

    // Receiver attachment
    void AttachReceiver(DirectReceiver& receiver);
    void DetachReceiver();

    // Latency Checkpoint Recording
    void SetLatencyTracker(DirectLatencyTracker* tracker) noexcept { m_latency_tracker = tracker; }
    DirectLatencyTracker* GetLatencyTracker() const noexcept { return m_latency_tracker; }

    // Checkpoint helpers for stages outside the bridge
    void RecordSenderTimestamps(uint64_t frame_id, uint64_t s0_pts, uint64_t s1_cb,
                                uint64_t s2_enc_in, uint64_t s3_enc_out, uint64_t s4_send);
    void RecordReceiverTimestamps(uint64_t frame_id, uint64_t r0_arrival,
                                  uint64_t r1_au_complete, uint64_t r2_dec_in);
    void RecordDecoderOutput(uint64_t frame_id, uint64_t r3_ts_ns = 0);
    void RecordPresent(uint64_t frame_id, uint64_t r4_ts_ns = 0);

    // Statistics
    uint64_t GetFramesSubmitted() const noexcept { return m_frames_submitted.load(std::memory_order_relaxed); }
    uint64_t GetFramesDropped() const noexcept { return m_frames_dropped.load(std::memory_order_relaxed); }

private:
    video::VideoDecoder& m_decoder;
    DirectLatencyTracker* m_latency_tracker{nullptr};
    DirectReceiver* m_attached_receiver{nullptr};

    uint32_t m_width{1920};
    uint32_t m_height{1080};
    uint32_t m_fps{60};
    video::VideoCodecType m_codec{video::VideoCodecType::H264};

    std::atomic<uint64_t> m_frames_submitted{0};
    std::atomic<uint64_t> m_frames_dropped{0};

    mutable std::mutex m_checkpoints_mutex;
    std::unordered_map<uint64_t, FrameLatencyCheckpoints> m_pending_checkpoints;
};

} // namespace duwn::direct
