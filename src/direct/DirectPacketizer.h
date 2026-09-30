#pragma once
// DirectPacketizer.h — Sender-side packetizer for Duwn Direct Mode.
// Slices Annex-B H.264 bitstream into MTU-safe datagrams with packed 36-byte DirectPacketHeader.

#include "DirectPacket.h"
#include "direct/encoder/DirectVideoEncoder.h"
#include "IDirectTransport.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <chrono>

namespace duwn::direct {

class DirectPacketizer {
public:
    static constexpr size_t kDefaultMaxPayloadPerPacket = 1400; // Safe for standard 1500 MTU LAN

    explicit DirectPacketizer(uint32_t session_id,
                              size_t max_payload_size = kDefaultMaxPayloadPerPacket)
        : m_session_id(session_id), m_max_payload_size(max_payload_size) {}

    void SetSessionId(uint32_t session_id) noexcept { m_session_id = session_id; }
    uint32_t GetSessionId() const noexcept { return m_session_id; }

    // Packetizes an encoded video frame into fragmented datagram buffers
    std::vector<std::vector<uint8_t>> PacketizeFrame(const EncodedVideoFrame& frame,
                                                     uint64_t send_timestamp_ns = 0) const {
        std::vector<std::vector<uint8_t>> packets;
        const size_t total_size = frame.payload.size();
        if (total_size == 0) return packets;

        const uint32_t packet_count = static_cast<uint32_t>((total_size + m_max_payload_size - 1) / m_max_payload_size);
        packets.reserve(packet_count);

        if (send_timestamp_ns == 0) {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            send_timestamp_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        }

        uint16_t base_flags = DirectPacketFlags::None;
        if (frame.is_keyframe) {
            base_flags |= DirectPacketFlags::KeyFrame;
        }

        for (uint32_t idx = 0; idx < packet_count; ++idx) {
            size_t offset = idx * m_max_payload_size;
            size_t chunk_len = std::min(m_max_payload_size, total_size - offset);

            DirectPacketHeader header;
            header.protocol_major = 1;
            header.protocol_minor = 0;
            header.session_id = m_session_id;
            header.stream_id = static_cast<uint16_t>(DirectStreamId::Video);
            header.flags = base_flags;
            if (idx == packet_count - 1) {
                header.flags |= DirectPacketFlags::LastPacket;
            }
            header.frame_id = static_cast<uint32_t>(frame.frame_id);
            header.packet_index = idx;
            header.packet_count = packet_count;
            header.source_timestamp_ns = frame.source_timestamp_ns;
            header.payload_type = static_cast<uint8_t>(DirectPayloadType::VideoSlice);

            std::vector<uint8_t> packet_data(DirectPacketHeader::kHeaderSize + chunk_len);
            header.Serialize(packet_data.data(), packet_data.size());

            std::copy(frame.payload.begin() + offset,
                      frame.payload.begin() + offset + chunk_len,
                      packet_data.begin() + DirectPacketHeader::kHeaderSize);

            packets.push_back(std::move(packet_data));
        }

        return packets;
    }

    // Packetizes and immediately transmits across an IDirectTransport instance
    size_t TransmitFrame(const EncodedVideoFrame& frame, IDirectTransport& transport) const {
        auto packets = PacketizeFrame(frame);
        size_t sent_count = 0;
        for (const auto& pkt : packets) {
            if (transport.SendPacket(pkt.data(), pkt.size())) {
                sent_count++;
            }
        }
        return sent_count;
    }

private:
    uint32_t m_session_id{0};
    size_t m_max_payload_size{kDefaultMaxPayloadPerPacket};
};

} // namespace duwn::direct
