#pragma once
// DirectReceiver.h — Duwn Direct mode receiver coordinator for Windows.
// Connects transport, session, bounded frame assembly, and freshest frame output.

#include "IDirectTransport.h"
#include "DirectSession.h"
#include "DirectSessionModel.h"
#include "FreshestFrameSlot.h"
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

namespace duwn::direct {

class DirectReceiver {
public:
    explicit DirectReceiver(ReceiverCapabilities capabilities = {});
    ~DirectReceiver();

    // Configuration & Transport attachment
    void SetCapabilities(ReceiverCapabilities capabilities);
    const ReceiverCapabilities& GetCapabilities() const noexcept { return m_capabilities; }

    void SetTransport(std::unique_ptr<IDirectTransport> transport);
    IDirectTransport* GetTransport() const noexcept { return m_transport.get(); }

    // Start / Stop listening
    bool Start();
    void Stop();
    bool IsRunning() const noexcept { return m_running.load(std::memory_order_relaxed); }

    // Session Management
    void StartNewSession(uint32_t session_id);
    void ResetSession(uint32_t new_session_id = 0);
    DirectSession* GetCurrentSession() const noexcept { return m_session.get(); }

    // Frame Output access
    FreshestFrameSlot& GetFrameSlot() noexcept { return m_frame_slot; }
    bool TakeFreshestFrame(DirectFrame& out_frame) { return m_frame_slot.Take(out_frame); }
    void SetOnFrameReady(std::function<void()> callback);
    void SetFrameCallback(std::function<void(DirectFrame)> callback);

    // Diagnostics / Metrics
    AssemblerMetrics GetMetrics() const noexcept;

    // Optional direct packet ingestion (bypassing transport if needed, e.g. for deterministic unit tests)
    bool IngestPacket(const uint8_t* data, size_t size);

private:
    void HandleIncomingPacket(const uint8_t* data, size_t size);
    void SetupSessionCallbacks();

    ReceiverCapabilities m_capabilities;
    FreshestFrameSlot m_frame_slot;
    std::unique_ptr<IDirectTransport> m_transport;
    std::unique_ptr<DirectSession> m_session;
    std::atomic<bool> m_running{false};
    std::function<void()> m_on_frame_ready;
    std::function<void(DirectFrame)> m_frame_callback;
    mutable std::recursive_mutex m_mutex;
};

} // namespace duwn::direct
