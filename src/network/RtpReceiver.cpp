#include "RtpReceiver.h"
#include "RtpPacket.h"
#include "common/clock/MonotonicClock.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <avrt.h>
#include <array>
#include <cstring>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "avrt.lib")

namespace duwn::network {

namespace {
constexpr int kPreferredRecvBufSize = 8 * 1024 * 1024; // 8 MiB socket recv buffer for high-bitrate 2K/Original
constexpr int kFallbackRecvBufSize  = 4 * 1024 * 1024; // 4 MiB fallback
constexpr int kMaxPacket            = 65507;           // max UDP payload
} // namespace

bool RtpPacket::Parse(std::span<const uint8_t> data,
                      int64_t arrival_ns,
                      RtpPacket& out) noexcept {
    // Minimum RTP header: 12 bytes
    if (data.size() < 12) return false;

    const uint8_t b0 = data[0];
    out.version      = (b0 >> 6) & 0x03;
    if (out.version != 2) return false;

    out.padding      = (b0 >> 5) & 0x01;
    out.extension    = (b0 >> 4) & 0x01;
    const uint8_t cc = b0 & 0x0F; // CSRC count

    out.marker       = (data[1] >> 7) & 0x01;
    out.payload_type = data[1] & 0x7F;
    out.sequence     = static_cast<uint16_t>((data[2] << 8) | data[3]);
    out.timestamp    = static_cast<uint32_t>(
                           (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]);
    out.ssrc         = static_cast<uint32_t>(
                           (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11]);

    size_t offset = 12 + static_cast<size_t>(cc) * 4;
    if (offset > data.size()) return false;

    // Extension header
    if (out.extension) {
        if (offset + 4 > data.size()) return false;
        uint16_t ext_len = static_cast<uint16_t>(
                               (data[offset + 2] << 8) | data[offset + 3]);
        offset += 4 + static_cast<size_t>(ext_len) * 4;
    }

    if (offset > data.size()) return false;

    // Padding
    size_t payload_end = data.size();
    if (out.padding && payload_end > offset) {
        uint8_t pad = data[payload_end - 1];
        if (pad == 0 || pad > (payload_end - offset)) return false;
        payload_end -= pad;
    }

    out.payload     = data.subspan(offset, payload_end - offset);
    out.arrival_ns  = arrival_ns;
    return true;
}

// ---- RtpReceiver ----

RtpReceiver::RtpReceiver(RtpCallback callback, ReceiverPriorityPolicy priority) noexcept
    : m_callback(std::move(callback)), m_priority_policy(priority) {
    // Ensure Winsock is initialised (safe to call multiple times).
    WSADATA wsa{};
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
}

RtpReceiver::~RtpReceiver() {
    Stop();
}

uint16_t RtpReceiver::Start() noexcept {
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        DUWN_LOG_ERROR("RtpReceiver", "socket() failed");
        return 0;
    }

    // Set recv buffer to 4 MiB (fallback to 2 MiB)
    int recvbuf = kPreferredRecvBufSize;
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&recvbuf), sizeof(recvbuf)) == SOCKET_ERROR) {
        recvbuf = kFallbackRecvBufSize;
        ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&recvbuf), sizeof(recvbuf));
    }
    int actual_buf = 0;
    int optlen = sizeof(actual_buf);
    if (::getsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<char*>(&actual_buf), &optlen) == 0) {
        DUWN_LOG_INFOF("RtpReceiver", "SO_RCVBUF configured: requested={} bytes, actual={} bytes",
                       recvbuf, actual_buf);
    }

    // Disable SIO_UDP_CONNRESET to ignore spurious ICMP port unreachable errors on UDP socket
    DWORD bytes_returned = 0;
    BOOL new_behavior = FALSE;
    ::WSAIoctl(sock, SIO_UDP_CONNRESET, &new_behavior, sizeof(new_behavior),
               nullptr, 0, &bytes_returned, nullptr, nullptr);

    // Bind to loopback, port 0 (OS picks a free port)
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port        = 0;

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        DUWN_LOG_ERROR("RtpReceiver", "bind() failed");
        ::closesocket(sock);
        return 0;
    }

    // Query the actual port
    int addrlen = sizeof(addr);
    ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    m_port   = ::ntohs(addr.sin_port);
    m_socket = static_cast<uintptr_t>(sock);
    m_running.store(true, std::memory_order_release);

    m_thread = std::jthread([this](std::stop_token) { RecvLoop(); });

    DUWN_LOG_INFOF("RtpReceiver", "Listening on 127.0.0.1:{}", m_port);
    return m_port;
}

void RtpReceiver::Stop() noexcept {
    m_running.store(false, std::memory_order_release);
    SOCKET sock = static_cast<SOCKET>(m_socket);
    if (sock != INVALID_SOCKET) {
        ::closesocket(sock); // unblocks recvfrom
        m_socket = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (m_thread.joinable()) m_thread.join();
}

void RtpReceiver::RecvLoop() noexcept {
    using clock = duwn::clock::MonotonicClock;

    DWORD task_idx = 0;
    const wchar_t* task_name = (m_priority_policy == ReceiverPriorityPolicy::ProAudioHighest)
                               ? L"Pro Audio" : L"Playback";
    int fallback_priority = (m_priority_policy == ReceiverPriorityPolicy::ProAudioHighest)
                            ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_ABOVE_NORMAL;

    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(task_name, &task_idx);
    if (!mmcss) {
        ::SetThreadPriority(::GetCurrentThread(), fallback_priority);
    }

    std::array<uint8_t, kMaxPacket> buf{};
    SOCKET sock = static_cast<SOCKET>(m_socket);

    while (m_running.load(std::memory_order_acquire)) {
        int n = ::recv(sock, reinterpret_cast<char*>(buf.data()),
                       static_cast<int>(buf.size()), 0);
        if (n <= 0) {
            if (!m_running.load(std::memory_order_acquire)) {
                DUWN_LOG_INFOF("RtpReceiver", "RecvLoop stopped normally on port {}", m_port);
                break;
            }
            int err = ::WSAGetLastError();
            if (err == WSAECONNRESET) {
                static std::atomic<uint32_t> s_connreset_limit{0};
                if (s_connreset_limit.fetch_add(1, std::memory_order_relaxed) < 5) {
                    DUWN_LOG_WARNF("RtpReceiver",
                        "recv returned WSAECONNRESET (10054) on port {}; ignoring spurious ICMP Port Unreachable",
                        m_port);
                }
                continue;
            }
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEMSGSIZE) {
                m_stats.raw_udp.fetch_add(1, std::memory_order_relaxed);
                GlobalMetrics().network_raw_udp_packets.fetch_add(1, std::memory_order_relaxed);
                m_stats.malformed.fetch_add(1, std::memory_order_relaxed);
                GlobalMetrics().network_malformed_packets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            DUWN_LOG_ERRORF("RtpReceiver",
                "RecvLoop fatal error on port {}: n={}, WSAGetLastError={}; thread exiting",
                m_port, n, err);
            break;
        }

        m_stats.raw_udp.fetch_add(1, std::memory_order_relaxed);
        GlobalMetrics().network_raw_udp_packets.fetch_add(1, std::memory_order_relaxed);

        int64_t now_ns = clock::Now().time_since_epoch().count();

        RtpPacket pkt;
        if (!RtpPacket::Parse(std::span{buf.data(), static_cast<size_t>(n)},
                               now_ns, pkt)) {
            m_stats.malformed.fetch_add(1, std::memory_order_relaxed);
            GlobalMetrics().network_malformed_packets.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        m_stats.received.fetch_add(1, std::memory_order_relaxed);
        GlobalMetrics().network_received_packets.fetch_add(1, std::memory_order_relaxed);

        m_callback(pkt);
    }

    if (mmcss) {
        ::AvRevertMmThreadCharacteristics(mmcss);
    }
}

} // namespace duwn::network
