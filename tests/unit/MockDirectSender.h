#pragma once
// MockDirectSender.h — Test sender for generating synthetic Direct Mode packets and frames.
// Simulates ordered, reordered, missing, duplicate, superseding, and malformed packet sequences.

#include "direct/DirectPacket.h"
#include "direct/IDirectTransport.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <random>

namespace duwn::direct::test {

class MockDirectSender {
public:
    explicit MockDirectSender(uint32_t session_id = 42) : m_session_id(session_id) {}

    void SetSessionId(uint32_t session_id) noexcept { m_session_id = session_id; }
    uint32_t GetSessionId() const noexcept { return m_session_id; }

    // Generates a deterministic synthetic payload of specified size
    static std::vector<uint8_t> GeneratePayload(uint32_t frame_id, size_t size) {
        std::vector<uint8_t> payload(size);
        for (size_t i = 0; i < size; ++i) {
            payload[i] = static_cast<uint8_t>((frame_id * 17 + i * 31 + 7) & 0xFF);
        }
        return payload;
    }

    // Creates packet sequence for a single frame
    std::vector<std::vector<uint8_t>> CreateFramePackets(uint32_t frame_id,
                                                         size_t total_payload_size,
                                                         uint32_t packet_count,
                                                         uint16_t flags = DirectPacketFlags::None,
                                                         DirectPayloadType payload_type = DirectPayloadType::VideoSlice) {
        std::vector<std::vector<uint8_t>> packets;
        if (packet_count == 0) return packets;

        std::vector<uint8_t> full_payload = GeneratePayload(frame_id, total_payload_size);
        size_t slice_size = (total_payload_size + packet_count - 1) / packet_count;

        packets.reserve(packet_count);
        for (uint32_t idx = 0; idx < packet_count; ++idx) {
            size_t offset = idx * slice_size;
            size_t current_slice_len = 0;
            if (offset < total_payload_size) {
                current_slice_len = std::min(slice_size, total_payload_size - offset);
            }

            DirectPacketHeader header;
            header.protocol_major = 1;
            header.protocol_minor = 0;
            header.session_id = m_session_id;
            header.stream_id = static_cast<uint16_t>(DirectStreamId::Video);
            header.flags = flags;
            header.frame_id = frame_id;
            header.packet_index = idx;
            header.packet_count = packet_count;
            header.source_timestamp_ns = 1000000000ULL + frame_id * 16666666ULL;
            header.payload_type = static_cast<uint8_t>(payload_type);

            std::vector<uint8_t> packet_data(DirectPacketHeader::kHeaderSize + current_slice_len);
            header.Serialize(packet_data.data());

            if (current_slice_len > 0) {
                std::copy(full_payload.begin() + offset,
                          full_payload.begin() + offset + current_slice_len,
                          packet_data.begin() + DirectPacketHeader::kHeaderSize);
            }

            packets.push_back(std::move(packet_data));
        }

        return packets;
    }

    // Utility: creates a malformed packet with bad version or truncated bytes
    static std::vector<uint8_t> CreateTruncatedPacket(size_t truncate_to_bytes = 10) {
        std::vector<uint8_t> packet(truncate_to_bytes, 0xAA);
        return packet;
    }

    static std::vector<uint8_t> CreateCorruptHeaderPacket(uint32_t frame_id, uint32_t packet_index, uint32_t packet_count) {
        DirectPacketHeader header;
        header.protocol_major = 99; // Incompatible major version
        header.session_id = 42;
        header.frame_id = frame_id;
        header.packet_index = packet_index;
        header.packet_count = packet_count;

        std::vector<uint8_t> packet(DirectPacketHeader::kHeaderSize + 64, 0xBB);
        header.Serialize(packet.data());
        return packet;
    }

    static std::vector<uint8_t> CreateOutOfBoundsIndexPacket(uint32_t frame_id, uint32_t packet_count) {
        DirectPacketHeader header;
        header.protocol_major = 1;
        header.session_id = 42;
        header.frame_id = frame_id;
        header.packet_index = packet_count + 5; // index >= count!
        header.packet_count = packet_count;

        std::vector<uint8_t> packet(DirectPacketHeader::kHeaderSize + 32, 0xCC);
        header.Serialize(packet.data());
        return packet;
    }

private:
    uint32_t m_session_id{42};
};

} // namespace duwn::direct::test
