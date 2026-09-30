#include "DirectUdpTransport.h"
#include <vector>

namespace duwn::direct {

DirectUdpTransport::DirectUdpTransport(std::string bind_ip, uint16_t bind_port)
    : m_bind_ip(std::move(bind_ip)), m_requested_port(bind_port) {
}

DirectUdpTransport::~DirectUdpTransport() {
    Stop();
}

bool DirectUdpTransport::Start() {
    if (m_running.load(std::memory_order_relaxed)) return true;

    m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    // Set receive timeout so loop checks stop condition periodically
    DWORD timeout_ms = 50;
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    // Expand receive buffer
    int rcvbuf = 2 * 1024 * 1024; // 2 MB
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = ::htons(m_requested_port);
    ::inet_pton(AF_INET, m_bind_ip.c_str(), &bind_addr.sin_addr);

    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == SOCKET_ERROR) {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // Retrieve bound port if ephemeral
    sockaddr_in actual_addr{};
    int addr_len = sizeof(actual_addr);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&actual_addr), &addr_len) == 0) {
        m_bound_port = ::ntohs(actual_addr.sin_port);
    }

    m_running.store(true, std::memory_order_release);
    m_reader_thread = std::thread(&DirectUdpTransport::ReaderLoop, this);
    return true;
}

void DirectUdpTransport::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_socket != INVALID_SOCKET) {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_reader_thread.joinable()) {
        m_reader_thread.join();
    }
}

void DirectUdpTransport::SetPacketCallback(std::function<void(const uint8_t* data, size_t size)> callback) {
    m_callback = std::move(callback);
}

bool DirectUdpTransport::SetDestination(std::string dest_ip, uint16_t dest_port) {
    m_dest_addr = {};
    m_dest_addr.sin_family = AF_INET;
    m_dest_addr.sin_port = ::htons(dest_port);
    if (::inet_pton(AF_INET, dest_ip.c_str(), &m_dest_addr.sin_addr) <= 0) {
        return false;
    }
    m_dest_set = true;
    return true;
}

bool DirectUdpTransport::SendPacket(const uint8_t* data, size_t size) {
    if (m_socket == INVALID_SOCKET || !m_dest_set || !data || size == 0) return false;
    int sent = ::sendto(m_socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                        reinterpret_cast<const sockaddr*>(&m_dest_addr), sizeof(m_dest_addr));
    return sent == static_cast<int>(size);
}

void DirectUdpTransport::ReaderLoop() {
    std::vector<uint8_t> buffer(65536);

    while (m_running.load(std::memory_order_relaxed)) {
        sockaddr_in src_addr{};
        int src_len = sizeof(src_addr);
        int bytes = ::recvfrom(m_socket, reinterpret_cast<char*>(buffer.data()),
                               static_cast<int>(buffer.size()), 0,
                               reinterpret_cast<sockaddr*>(&src_addr), &src_len);

        if (bytes > 0 && m_callback) {
            m_callback(buffer.data(), static_cast<size_t>(bytes));
        }
    }
}

} // namespace duwn::direct
