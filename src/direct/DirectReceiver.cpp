#include "DirectReceiver.h"

namespace duwn::direct {

DirectReceiver::DirectReceiver(ReceiverCapabilities capabilities)
    : m_capabilities(std::move(capabilities)) {
    m_session = std::make_unique<DirectSession>(0, m_frame_slot);
}

DirectReceiver::~DirectReceiver() {
    Stop();
}

void DirectReceiver::SetCapabilities(ReceiverCapabilities capabilities) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_capabilities = std::move(capabilities);
}

void DirectReceiver::SetTransport(std::unique_ptr<IDirectTransport> transport) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_transport && m_transport->IsRunning()) {
        m_transport->Stop();
    }
    m_transport = std::move(transport);
    if (m_transport) {
        m_transport->SetPacketCallback([this](const uint8_t* data, size_t size) {
            HandleIncomingPacket(data, size);
        });
    }
}

bool DirectReceiver::Start() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_running.load(std::memory_order_relaxed)) return true;

    if (m_transport) {
        if (!m_transport->Start()) {
            return false;
        }
    }
    m_running.store(true, std::memory_order_release);
    return true;
}

void DirectReceiver::Stop() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_transport) {
        m_transport->Stop();
    }
}

void DirectReceiver::SetOnFrameReady(std::function<void()> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_on_frame_ready = std::move(callback);
    SetupSessionCallbacks();
}

void DirectReceiver::SetFrameCallback(std::function<void(DirectFrame)> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_frame_callback = std::move(callback);
    SetupSessionCallbacks();
}

void DirectReceiver::SetupSessionCallbacks() {
    if (!m_session) return;
    if (m_on_frame_ready || m_frame_callback) {
        m_session->GetAssembler().SetFrameCompleteCallback([this](const DirectFrame& frame) {
            std::function<void()> on_ready;
            std::function<void(DirectFrame)> frame_cb;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                on_ready = m_on_frame_ready;
                frame_cb = m_frame_callback;
            }
            if (on_ready) {
                on_ready();
            }
            if (frame_cb) {
                DirectFrame copy;
                copy.frame_id = frame.frame_id;
                copy.stream_id = frame.stream_id;
                copy.payload_type = frame.payload_type;
                copy.flags = frame.flags;
                copy.source_timestamp_ns = frame.source_timestamp_ns;
                copy.payload = frame.payload;
                copy.is_keyframe = frame.is_keyframe;
                frame_cb(std::move(copy));
            }
        });
    } else {
        m_session->GetAssembler().SetFrameCompleteCallback(nullptr);
    }
}

void DirectReceiver::StartNewSession(uint32_t session_id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_session = std::make_unique<DirectSession>(session_id, m_frame_slot);
    m_session->SetState(SessionState::Active);
    SetupSessionCallbacks();
}

void DirectReceiver::ResetSession(uint32_t new_session_id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_session) {
        m_session->Reset(new_session_id);
    } else {
        m_session = std::make_unique<DirectSession>(new_session_id, m_frame_slot);
    }
    SetupSessionCallbacks();
}

AssemblerMetrics DirectReceiver::GetMetrics() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_session) {
        return m_session->GetAssemblerMetrics();
    }
    return {};
}

bool DirectReceiver::IngestPacket(const uint8_t* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session) {
        m_session = std::make_unique<DirectSession>(0, m_frame_slot);
        SetupSessionCallbacks();
    }
    return m_session->OnPacketReceived(data, size);
}

void DirectReceiver::HandleIncomingPacket(const uint8_t* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_running.load(std::memory_order_relaxed)) return;
    if (m_session) {
        m_session->OnPacketReceived(data, size);
    }
}

} // namespace duwn::direct
