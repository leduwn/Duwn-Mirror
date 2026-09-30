#pragma once
// DirectUdpTransport.h — Concrete UDP transport implementation of IDirectTransport.
// Suitable for localhost tests and LAN datagram streaming.

#include "IDirectTransport.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <string>

namespace duwn::direct {

class DirectUdpTransport : public IDirectTransport {
public:
    explicit DirectUdpTransport(std::string bind_ip = "127.0.0.1", uint16_t bind_port = 0);
    ~DirectUdpTransport() override;

    // IDirectTransport implementation
    bool Start() override;
    void Stop() override;
    bool IsRunning() const noexcept override { return m_running.load(std::memory_order_relaxed); }
    void SetPacketCallback(std::function<void(const uint8_t* data, size_t size)> callback) override;
    bool SendPacket(const uint8_t* data, size_t size) override;
    uint16_t GetBoundPort() const noexcept override { return m_bound_port; }

    // Sets destination for outgoing SendPacket calls
    bool SetDestination(std::string dest_ip, uint16_t dest_port);

private:
    void ReaderLoop();

    std::string m_bind_ip;
    uint16_t m_requested_port{0};
    uint16_t m_bound_port{0};

    SOCKET m_socket{INVALID_SOCKET};
    sockaddr_in m_dest_addr{};
    bool m_dest_set{false};

    std::atomic<bool> m_running{false};
    std::thread m_reader_thread;
    std::function<void(const uint8_t* data, size_t size)> m_callback;
};

} // namespace duwn::direct
