#include "DirectFrameAssembler.h"
#include <algorithm>

namespace duwn::direct {

DirectFrameAssembler::DirectFrameAssembler(FreshestFrameSlot& output_slot)
    : m_output_slot(output_slot) {
}

bool DirectFrameAssembler::ProcessPacket(const uint8_t* data, size_t size) {
    if (!data || size < DirectPacketHeader::kHeaderSize) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.malformed_packets++;
        return false;
    }

    DirectPacketHeader header;
    if (!DirectPacketHeader::Deserialize(data, size, header)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.malformed_packets++;
        return false;
    }

    const uint8_t* payload = data + DirectPacketHeader::kHeaderSize;
    size_t payload_len = size - DirectPacketHeader::kHeaderSize;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.total_packets++;

    // Check if packet belongs to an already-completed frame or obsolete frame
    if (m_has_completed_frame && header.frame_id <= m_highest_completed_frame_id) {
        m_metrics.late_packets++;
        return false;
    }

    if (m_assembling.is_active) {
        if (header.frame_id < m_assembling.frame_id) {
            m_metrics.late_packets++;
            return false;
        }

        // A newer frame started arriving while the current frame was still assembling
        if (header.frame_id > m_assembling.frame_id) {
            if (m_assembling.packets_received < m_assembling.packet_count) {
                m_metrics.incomplete_frames++;
                m_metrics.superseded_frames++;
            }
            m_assembling.Init(header);
        }
    } else {
        m_assembling.Init(header);
    }

    // Sanity check: packet_index must be strictly within packet_count
    if (header.packet_index >= m_assembling.packet_count) {
        m_metrics.malformed_packets++;
        return false;
    }

    // Duplicate fragment check
    if (m_assembling.fragment_received[header.packet_index]) {
        m_metrics.duplicate_packets++;
        return false;
    }

    // Store fragment
    m_assembling.fragments[header.packet_index].assign(payload, payload + payload_len);
    m_assembling.fragment_received[header.packet_index] = true;
    m_assembling.packets_received++;
    m_assembling.total_payload_bytes += payload_len;

    // Check if frame is complete
    if (m_assembling.packets_received == m_assembling.packet_count) {
        DirectFrame frame;
        frame.frame_id = m_assembling.frame_id;
        frame.stream_id = m_assembling.stream_id;
        frame.payload_type = m_assembling.payload_type;
        frame.flags = m_assembling.flags;
        frame.source_timestamp_ns = m_assembling.source_timestamp_ns;
        frame.is_keyframe = (m_assembling.flags & DirectPacketFlags::KeyFrame) != 0;

        frame.payload.reserve(m_assembling.total_payload_bytes);
        for (const auto& frag : m_assembling.fragments) {
            frame.payload.insert(frame.payload.end(), frag.begin(), frag.end());
        }

        m_metrics.completed_frames++;
        if (m_assembling.frame_id > m_highest_completed_frame_id || !m_has_completed_frame) {
            m_highest_completed_frame_id = m_assembling.frame_id;
            m_has_completed_frame = true;
        }

        // Push into bounded FreshestFrameSlot (guarantees at most 1 pending frame)
        if (m_frame_complete_callback) {
            DirectFrame copy = frame;
            m_output_slot.Put(std::move(frame));
            m_frame_complete_callback(copy);
        } else {
            m_output_slot.Put(std::move(frame));
        }

        // Done assembling this frame
        m_assembling.Clear();
    }

    return true;
}

void DirectFrameAssembler::Reset() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assembling.Clear();
    m_highest_completed_frame_id = 0;
    m_has_completed_frame = false;
}

AssemblerMetrics DirectFrameAssembler::GetMetrics() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metrics;
}

} // namespace duwn::direct
