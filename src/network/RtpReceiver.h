#pragma once
// RtpReceiver — UDP socket receiver on 127.0.0.1:<port>.
// Runs its own recv thread. Delivers RtpPackets to a bounded SPSC queue.
// Binds ONLY to loopback — never exposed to LAN.

#include "RtpPacket.h"
#include "PacketStatistics.h"
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>

namespace duwn::network {

// Callback invoked on the receiver thread for each validated RTP packet.
// MUST NOT block. Packet data is valid only for the duration of the call;
// copy payload if you need it past the callback.
using RtpCallback = std::function<void(const RtpPacket&)>;

enum class ReceiverPriorityPolicy {
    ProAudioHighest = 0,     // MMCSS "Pro Audio" or THREAD_PRIORITY_HIGHEST
    PlaybackAboveNormal = 1, // MMCSS "Playback" or THREAD_PRIORITY_ABOVE_NORMAL
};

enum class RtpStreamKind {
    Generic,
    Video,
    Audio,
};

class RtpReceiver {
public:
    explicit RtpReceiver(RtpCallback callback,
                         ReceiverPriorityPolicy priority = ReceiverPriorityPolicy::PlaybackAboveNormal,
                         RtpStreamKind stream_kind = RtpStreamKind::Generic) noexcept;
    ~RtpReceiver();

    // Non-copyable, non-movable (owns a socket and thread)
    RtpReceiver(const RtpReceiver&) = delete;
    RtpReceiver& operator=(const RtpReceiver&) = delete;

    // Bind to 127.0.0.1 on a dynamic port. Returns the bound port, or 0 on error.
    uint16_t Start() noexcept;

    // Stop receiving and close the socket.
    void Stop() noexcept;

    uint16_t Port() const noexcept { return m_port; }
    bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

    const PacketStatistics& Stats() const noexcept { return m_stats; }

    void SetPriorityPolicy(ReceiverPriorityPolicy policy) noexcept { m_priority_policy = policy; }
    ReceiverPriorityPolicy GetPriorityPolicy() const noexcept { return m_priority_policy; }

private:
    void RecvLoop() noexcept;

    RtpCallback            m_callback;
    ReceiverPriorityPolicy m_priority_policy{ReceiverPriorityPolicy::PlaybackAboveNormal};
    RtpStreamKind          m_stream_kind{RtpStreamKind::Generic};
    uint16_t               m_port{0};
    uintptr_t              m_socket{static_cast<uintptr_t>(~0ull)}; // INVALID_SOCKET
    std::atomic_bool       m_running{false};
    std::jthread           m_thread;
    PacketStatistics       m_stats;
};

} // namespace duwn::network
