#pragma once
// DirectSession.h — Represents an active Duwn Direct streaming session.
// Manages session state, negotiated plan, and frame assembly integration.

#include "DirectProtocol.h"
#include "DirectSessionModel.h"
#include "DirectPacket.h"
#include "DirectFrameAssembler.h"
#include "FreshestFrameSlot.h"
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

namespace duwn::direct {

enum class SessionState : uint8_t {
    Idle,
    Connecting,
    Active,
    Disconnected,
    Error
};

class DirectSession {
public:
    explicit DirectSession(uint32_t session_id, FreshestFrameSlot& frame_slot);
    ~DirectSession() = default;

    // Session ID
    uint32_t GetSessionId() const noexcept { return m_session_id; }

    // Session State
    SessionState GetState() const noexcept { return m_state.load(std::memory_order_relaxed); }
    void SetState(SessionState state) noexcept { m_state.store(state, std::memory_order_release); }

    // Session Plan
    void SetPlan(SessionPlan plan) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_plan = std::move(plan);
    }
    SessionPlan GetPlan() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_plan;
    }

    // Packet processing
    bool OnPacketReceived(const uint8_t* data, size_t size);

    // Reset session assembler and state (e.g. on sender reconnect / session rollover)
    void Reset(uint32_t new_session_id) noexcept;

    // Metrics
    AssemblerMetrics GetAssemblerMetrics() const noexcept {
        return m_assembler.GetMetrics();
    }

    // Direct access to assembler if needed
    DirectFrameAssembler& GetAssembler() noexcept { return m_assembler; }

private:
    uint32_t m_session_id;
    std::atomic<SessionState> m_state{SessionState::Idle};
    FreshestFrameSlot& m_frame_slot;
    DirectFrameAssembler m_assembler;
    mutable std::mutex m_mutex;
    SessionPlan m_plan;
};

} // namespace duwn::direct
