#include "DirectSession.h"

namespace duwn::direct {

DirectSession::DirectSession(uint32_t session_id, FreshestFrameSlot& frame_slot)
    : m_session_id(session_id), m_frame_slot(frame_slot), m_assembler(frame_slot) {
}

bool DirectSession::OnPacketReceived(const uint8_t* data, size_t size) {
    if (!data || size < DirectPacketHeader::kHeaderSize) {
        return m_assembler.ProcessPacket(data, size); // will record malformed
    }

    DirectPacketHeader header;
    if (!DirectPacketHeader::Deserialize(data, size, header)) {
        return m_assembler.ProcessPacket(data, size); // will record malformed
    }

    // Check if session ID matches or if session is unassigned
    if (m_session_id == 0) {
        m_session_id = header.session_id;
        m_state.store(SessionState::Active, std::memory_order_release);
    } else if (header.session_id != m_session_id) {
        // Packet belongs to a different session ID - ignore or signal mismatch
        return false;
    }

    return m_assembler.ProcessPacket(data, size);
}

void DirectSession::Reset(uint32_t new_session_id) noexcept {
    m_session_id = new_session_id;
    m_state.store(SessionState::Idle, std::memory_order_release);
    m_assembler.Reset();
    m_frame_slot.Clear();
}

} // namespace duwn::direct
