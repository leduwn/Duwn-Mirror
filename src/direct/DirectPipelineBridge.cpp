#include "DirectPipelineBridge.h"
#include "common/clock/MonotonicClock.h"
#include <chrono>

namespace duwn::direct {

DirectPipelineBridge::DirectPipelineBridge(video::VideoDecoder& decoder,
                                           DirectLatencyTracker* latency_tracker)
    : m_decoder(decoder), m_latency_tracker(latency_tracker) {
}

DirectPipelineBridge::~DirectPipelineBridge() {
    DetachReceiver();
}

void DirectPipelineBridge::SetStreamFormat(uint32_t width, uint32_t height, uint32_t fps,
                                           video::VideoCodecType codec) noexcept {
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_codec = codec;
}

bool DirectPipelineBridge::FeedDirectFrame(DirectFrame frame, uint64_t r0_packet_arrival_ns) {
    if (frame.payload.empty()) {
        m_frames_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto now_epoch = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now_epoch).count());
    uint64_t r1_au_complete_ns = (r0_packet_arrival_ns > 0) ? (r0_packet_arrival_ns + 200'000ULL) : now_ns;

    video::EncodedAccessUnit au;
    au.data = std::move(frame.payload);
    au.pts_ns = static_cast<int64_t>(frame.source_timestamp_ns);
    au.dts_ns = au.pts_ns;
    au.sequence_number = frame.frame_id;
    au.codec = m_codec;
    au.has_idr = frame.is_keyframe;
    au.width_hint = m_width;
    au.height_hint = m_height;
    au.arrival_ns = static_cast<int64_t>(now_ns);
    au.au_received_qpc = clock::MonotonicClock::NowQpcTicks();
    au.rtp_arrival_qpc = au.au_received_qpc;

    uint64_t r2_decoder_input_ns = (r0_packet_arrival_ns > 0)
        ? (r1_au_complete_ns + 100'000ULL)
        : static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

    if (m_latency_tracker) {
        std::lock_guard<std::mutex> lock(m_checkpoints_mutex);
        auto& cp = m_pending_checkpoints[frame.frame_id];
        cp.frame_id = frame.frame_id;
        if (cp.s0_capture_source_ns == 0) {
            cp.s0_capture_source_ns = frame.source_timestamp_ns;
        }
        cp.r0_packet_arrival_ns = (r0_packet_arrival_ns > 0) ? r0_packet_arrival_ns : r1_au_complete_ns;
        cp.r1_au_complete_ns = r1_au_complete_ns;
        cp.r2_decoder_input_ns = r2_decoder_input_ns;
    }

    m_decoder.FeedAccessUnit(std::move(au));
    m_frames_submitted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool DirectPipelineBridge::ProcessFreshestFrame(FreshestFrameSlot& slot, uint64_t r0_packet_arrival_ns) {
    DirectFrame frame;
    if (slot.Take(frame)) {
        return FeedDirectFrame(std::move(frame), r0_packet_arrival_ns);
    }
    return false;
}

void DirectPipelineBridge::AttachReceiver(DirectReceiver& receiver) {
    DetachReceiver();
    m_attached_receiver = &receiver;
    m_attached_receiver->SetOnFrameReady([this]() {
        if (!m_attached_receiver) return;
        DirectFrame frame;
        while (m_attached_receiver->TakeFreshestFrame(frame)) {
            FeedDirectFrame(std::move(frame));
        }
    });
}

void DirectPipelineBridge::DetachReceiver() {
    if (m_attached_receiver) {
        m_attached_receiver->SetOnFrameReady(nullptr);
        m_attached_receiver = nullptr;
    }
}

void DirectPipelineBridge::RecordSenderTimestamps(uint64_t frame_id, uint64_t s0_pts, uint64_t s1_cb,
                                                  uint64_t s2_enc_in, uint64_t s3_enc_out, uint64_t s4_send) {
    if (!m_latency_tracker) return;
    std::lock_guard<std::mutex> lock(m_checkpoints_mutex);
    auto& cp = m_pending_checkpoints[frame_id];
    cp.frame_id = frame_id;
    cp.s0_capture_source_ns = s0_pts;
    cp.s1_capture_callback_ns = s1_cb;
    cp.s2_encoder_input_ns = s2_enc_in;
    cp.s3_encoder_output_ns = s3_enc_out;
    cp.s4_packet_send_ns = s4_send;
}

void DirectPipelineBridge::RecordReceiverTimestamps(uint64_t frame_id, uint64_t r0_arrival,
                                                    uint64_t r1_au_complete, uint64_t r2_dec_in) {
    if (!m_latency_tracker) return;
    std::lock_guard<std::mutex> lock(m_checkpoints_mutex);
    auto& cp = m_pending_checkpoints[frame_id];
    cp.frame_id = frame_id;
    cp.r0_packet_arrival_ns = r0_arrival;
    cp.r1_au_complete_ns = r1_au_complete;
    cp.r2_decoder_input_ns = r2_dec_in;
}

void DirectPipelineBridge::RecordDecoderOutput(uint64_t frame_id, uint64_t r3_ts_ns) {
    if (!m_latency_tracker) return;
    if (r3_ts_ns == 0) {
        r3_ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    std::lock_guard<std::mutex> lock(m_checkpoints_mutex);
    auto it = m_pending_checkpoints.find(frame_id);
    if (it != m_pending_checkpoints.end()) {
        it->second.r3_decoder_output_ns = r3_ts_ns;
    }
}

void DirectPipelineBridge::RecordPresent(uint64_t frame_id, uint64_t r4_ts_ns) {
    if (!m_latency_tracker) return;
    if (r4_ts_ns == 0) {
        r4_ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    std::lock_guard<std::mutex> lock(m_checkpoints_mutex);
    auto it = m_pending_checkpoints.find(frame_id);
    if (it != m_pending_checkpoints.end()) {
        it->second.r4_present_ns = r4_ts_ns;
        m_latency_tracker->RecordFrame(it->second);
        m_pending_checkpoints.erase(it);
    }
}

} // namespace duwn::direct
