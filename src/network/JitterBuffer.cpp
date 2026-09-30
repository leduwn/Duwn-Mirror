#include "JitterBuffer.h"
#include "common/metrics/Metrics.h"
#include <algorithm>
#include <cstring>

namespace duwn::network {

JitterBuffer::JitterBuffer(int64_t target_hold_ns) noexcept
    : m_target_hold_ns(target_hold_ns) {}

bool JitterBuffer::Push(const RtpPacket& pkt) noexcept {
    if (!m_initialised) {
        m_next_seq    = pkt.sequence;
        m_initialised = true;
        m_has_popped  = false;
    }

    // Drop duplicates: check if sequence is already in buffer
    for (const auto& slot : m_slots) {
        if (slot.occupied && slot.sequence == pkt.sequence) {
            return false;
        }
    }

    // Check if packet is old (behind m_next_seq)
    if (SeqLess(pkt.sequence, m_next_seq)) {
        if (!m_has_popped) {
            // Out-of-order startup: packets arrived before first-seen sequence.
            // If within buffer capacity, update expected start sequence.
            uint16_t diff = static_cast<uint16_t>(m_next_seq - pkt.sequence);
            if (diff < static_cast<uint16_t>(kJitterBufferCapacity)) {
                m_next_seq = pkt.sequence;
            } else {
                GlobalMetrics().network_reordered_packets.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        } else {
            GlobalMetrics().network_reordered_packets.fetch_add(1, std::memory_order_relaxed);
            return false; // Arrived too late; already popped past this sequence
        }
    }

    // Find an empty slot
    for (auto& slot : m_slots) {
        if (!slot.occupied) {
            slot.occupied   = true;
            slot.sequence   = pkt.sequence;
            slot.timestamp  = pkt.timestamp;
            slot.arrival_ns = pkt.arrival_ns;
            slot.payload.assign(pkt.payload.begin(), pkt.payload.end());
            ++m_size;
            return true;
        }
    }

    // Buffer full — drop the oldest slot to make room if incoming packet is newer
    Slot* oldest = nullptr;
    for (auto& slot : m_slots) {
        if (slot.occupied) {
            if (!oldest || SeqLess(slot.sequence, oldest->sequence)) {
                oldest = &slot;
            }
        }
    }
    if (oldest && SeqLess(oldest->sequence, pkt.sequence)) {
        if (m_next_seq == oldest->sequence) {
            ++m_next_seq;
        }
        oldest->sequence   = pkt.sequence;
        oldest->timestamp  = pkt.timestamp;
        oldest->arrival_ns = pkt.arrival_ns;
        oldest->payload.assign(pkt.payload.begin(), pkt.payload.end());
        return false; // returned false to signal a drop occurred
    }
    return false;
}

std::optional<JitterBuffer::Entry> JitterBuffer::Pop(int64_t now_ns) noexcept {
    if (m_size == 0) return std::nullopt;

    // Look for the slot matching m_next_seq
    for (auto& slot : m_slots) {
        if (slot.occupied && slot.sequence == m_next_seq) {
            if (m_target_hold_ns > 0 && (now_ns - slot.arrival_ns < m_target_hold_ns)) {
                return std::nullopt; // Hold target not reached yet
            }
            Entry e;
            e.sequence   = slot.sequence;
            e.timestamp  = slot.timestamp;
            e.arrival_ns = slot.arrival_ns;
            e.payload    = std::move(slot.payload);
            slot.occupied = false;
            --m_size;
            ++m_next_seq; // advance (wraps at 65535 → 0 naturally)
            m_has_popped = true;
            return e;
        }
    }

    // m_next_seq not found — check if we've held long enough to skip ahead
    // Find the earliest-sequence occupied slot
    Slot* earliest = nullptr;
    for (auto& slot : m_slots) {
        if (slot.occupied) {
            if (!earliest || SeqLess(slot.sequence, earliest->sequence)) {
                earliest = &slot;
            }
        }
    }
    if (!earliest) return std::nullopt;

    // If the earliest packet has been waiting longer than target_hold, release it
    // and advance next_seq past any gap.
    if (m_target_hold_ns <= 0 || (now_ns - earliest->arrival_ns >= m_target_hold_ns)) {
        // Count lost packets in the gap
        uint16_t gap = static_cast<uint16_t>(earliest->sequence - m_next_seq);
        GlobalMetrics().network_lost_packets.fetch_add(gap, std::memory_order_relaxed);

        m_next_seq = earliest->sequence; // skip the gap

        Entry e;
        e.sequence   = earliest->sequence;
        e.timestamp  = earliest->timestamp;
        e.arrival_ns = earliest->arrival_ns;
        e.payload    = std::move(earliest->payload);
        earliest->occupied = false;
        --m_size;
        ++m_next_seq;
        m_has_popped = true;
        return e;
    }

    return std::nullopt;
}

void JitterBuffer::Flush() noexcept {
    for (auto& slot : m_slots) {
        slot.occupied = false;
        slot.payload.clear();
    }
    m_size        = 0;
    m_initialised = false;
    m_has_popped  = false;
    m_next_seq    = 0;
}

} // namespace duwn::network
