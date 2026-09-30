#pragma once
// JitterBuffer — bounded reorder buffer for RTP packets.
// Holds up to kCapacity packets sorted by sequence number.
// Pop delivers the next expected sequence or null if not yet arrived.
//
// NOT thread-safe by itself — caller must serialise Push/Pop, or use from
// dedicated producer/consumer threads with an external SPSC queue.

#include "RtpPacket.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace duwn::network {

// Maximum number of packets held in flight.
// 64 is generous for 60fps video (covers ~1s at 60fps); audio uses less.
constexpr int kJitterBufferCapacity = 64;

class JitterBuffer {
public:
    // target_hold_ns: how long to hold a packet before releasing even if gaps remain.
    // For Smooth Live mode: ~33ms (2 frames at 60fps).
    explicit JitterBuffer(int64_t target_hold_ns) noexcept;

    // Push a received RTP packet. Copies payload into internal storage.
    // Returns false if the buffer is full (caller should drop oldest).
    bool Push(const RtpPacket& pkt) noexcept;

    // Pop the next in-sequence packet ready for consumption.
    // now_ns: current MonotonicClock nanoseconds.
    // Returns nullopt if not ready.
    struct Entry {
        uint16_t             sequence;
        uint32_t             timestamp;  // RTP timestamp
        int64_t              arrival_ns;
        std::vector<uint8_t> payload;
    };
    std::optional<Entry> Pop(int64_t now_ns) noexcept;

    // Packets in buffer
    int Size() const noexcept { return m_size; }

    // Flush all pending packets immediately (e.g. on session reset).
    void Flush() noexcept;

    // Update target hold time (streaming mode change).
    void SetTargetHoldNs(int64_t ns) noexcept { m_target_hold_ns = ns; }

private:
    struct Slot {
        bool             occupied{false};
        uint16_t         sequence{};
        uint32_t         timestamp{};
        int64_t          arrival_ns{};
        std::vector<uint8_t> payload;
    };

    std::array<Slot, kJitterBufferCapacity> m_slots{};
    uint16_t  m_next_seq{0};
    bool      m_initialised{false};
    bool      m_has_popped{false};
    int       m_size{0};
    int64_t   m_target_hold_ns;

    // RTP sequence number comparison (handles 16-bit wrap per RFC 3550 §A.1)
    static bool SeqLess(uint16_t a, uint16_t b) noexcept {
        return static_cast<int16_t>(a - b) < 0;
    }
};

} // namespace duwn::network
