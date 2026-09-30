#pragma once
// DirectFrameAssembler.h — Bounded frame reassembler enforcing Latest Frame Policy.
// At most 1 assembling frame + 1 newest ready frame in FreshestFrameSlot.

#include "DirectPacket.h"
#include "FreshestFrameSlot.h"
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <functional>

namespace duwn::direct {

struct AssemblerMetrics {
    uint64_t total_packets{0};
    uint64_t completed_frames{0};
    uint64_t superseded_frames{0};
    uint64_t incomplete_frames{0};
    uint64_t malformed_packets{0};
    uint64_t late_packets{0};
    uint64_t duplicate_packets{0};
};

class DirectFrameAssembler {
public:
    explicit DirectFrameAssembler(FreshestFrameSlot& output_slot);
    ~DirectFrameAssembler() = default;

    // Non-copyable
    DirectFrameAssembler(const DirectFrameAssembler&) = delete;
    DirectFrameAssembler& operator=(const DirectFrameAssembler&) = delete;

    // Processes a raw packet buffer received from transport.
    // Thread-safe. Reassembles packets into frames, drops obsolete/malformed fragments.
    bool ProcessPacket(const uint8_t* data, size_t size);

    // Resets assembler state on session disconnect or reconfiguration.
    void Reset() noexcept;

    // Snapshot of assembler metrics
    AssemblerMetrics GetMetrics() const noexcept;

    // Optional callback when a frame completes
    void SetFrameCompleteCallback(std::function<void(const DirectFrame&)> callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frame_complete_callback = std::move(callback);
    }

private:
    struct AssemblingFrame {
        bool is_active{false};
        uint32_t frame_id{0};
        uint16_t stream_id{0};
        uint8_t  payload_type{0};
        uint16_t flags{0};
        uint64_t source_timestamp_ns{0};
        uint32_t packet_count{0};
        uint32_t packets_received{0};
        size_t   total_payload_bytes{0};

        std::vector<std::vector<uint8_t>> fragments;
        std::vector<bool> fragment_received;

        void Init(const DirectPacketHeader& h) {
            is_active = true;
            frame_id = h.frame_id;
            stream_id = h.stream_id;
            payload_type = h.payload_type;
            flags = h.flags;
            source_timestamp_ns = h.source_timestamp_ns;
            packet_count = h.packet_count;
            packets_received = 0;
            total_payload_bytes = 0;
            fragments.assign(packet_count, {});
            fragment_received.assign(packet_count, false);
        }

        void Clear() noexcept {
            is_active = false;
            frame_id = 0;
            packet_count = 0;
            packets_received = 0;
            total_payload_bytes = 0;
            fragments.clear();
            fragment_received.clear();
        }
    };

    mutable std::mutex m_mutex;
    FreshestFrameSlot& m_output_slot;
    AssemblingFrame m_assembling;
    AssemblerMetrics m_metrics;
    uint32_t m_highest_completed_frame_id{0};
    bool m_has_completed_frame{false};
    std::function<void(const DirectFrame&)> m_frame_complete_callback;
};

} // namespace duwn::direct
