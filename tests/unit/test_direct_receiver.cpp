#include "direct/DirectPacket.h"
#include "direct/FreshestFrameSlot.h"
#include "direct/DirectFrameAssembler.h"
#include "direct/DirectSession.h"
#include "direct/DirectReceiver.h"
#include "direct/DirectUdpTransport.h"
#include "MockDirectSender.h"
#include <algorithm>
#include <thread>
#include <chrono>

using namespace duwn::direct;
using namespace duwn::direct::test;

// 1. DirectPacketHeader Serialization & Deserialization
DUWN_TEST(DirectReceiver_PacketHeaderSerializationRoundtrip) {
    DirectPacketHeader orig;
    orig.protocol_major = 1;
    orig.protocol_minor = 2;
    orig.session_id = 0x12345678;
    orig.stream_id = static_cast<uint16_t>(DirectStreamId::Video);
    orig.flags = DirectPacketFlags::KeyFrame | DirectPacketFlags::EndOfFrame;
    orig.frame_id = 999;
    orig.packet_index = 3;
    orig.packet_count = 5;
    orig.source_timestamp_ns = 9876543210123ULL;
    orig.payload_type = static_cast<uint8_t>(DirectPayloadType::VideoSlice);

    uint8_t buffer[DirectPacketHeader::kHeaderSize];
    orig.Serialize(buffer);

    DirectPacketHeader parsed;
    DUWN_ASSERT(DirectPacketHeader::Deserialize(buffer, sizeof(buffer), parsed));

    DUWN_ASSERT(parsed.protocol_major == 1);
    DUWN_ASSERT(parsed.protocol_minor == 2);
    DUWN_ASSERT(parsed.session_id == 0x12345678);
    DUWN_ASSERT(parsed.stream_id == static_cast<uint16_t>(DirectStreamId::Video));
    DUWN_ASSERT(parsed.flags == (DirectPacketFlags::KeyFrame | DirectPacketFlags::EndOfFrame));
    DUWN_ASSERT(parsed.frame_id == 999);
    DUWN_ASSERT(parsed.packet_index == 3);
    DUWN_ASSERT(parsed.packet_count == 5);
    DUWN_ASSERT(parsed.source_timestamp_ns == 9876543210123ULL);
    DUWN_ASSERT(parsed.payload_type == static_cast<uint8_t>(DirectPayloadType::VideoSlice));
}

DUWN_TEST(DirectReceiver_PacketHeaderRejectsMalformedAndIncompatible) {
    DirectPacketHeader h;
    uint8_t buf[DirectPacketHeader::kHeaderSize];
    h.Serialize(buf);

    DirectPacketHeader parsed;
    // Buffer too small
    DUWN_ASSERT(!DirectPacketHeader::Deserialize(buf, DirectPacketHeader::kHeaderSize - 1, parsed));
    // Nullptr
    DUWN_ASSERT(!DirectPacketHeader::Deserialize(nullptr, DirectPacketHeader::kHeaderSize, parsed));

    // Incompatible major version
    h.protocol_major = 2;
    h.Serialize(buf);
    DUWN_ASSERT(!DirectPacketHeader::Deserialize(buf, sizeof(buf), parsed));

    // Zero packet count
    h.protocol_major = 1;
    h.packet_count = 0;
    h.Serialize(buf);
    DUWN_ASSERT(!DirectPacketHeader::Deserialize(buf, sizeof(buf), parsed));

    // packet_index >= packet_count
    h.packet_count = 2;
    h.packet_index = 2;
    h.Serialize(buf);
    DUWN_ASSERT(!DirectPacketHeader::Deserialize(buf, sizeof(buf), parsed));
}

// 2. FreshestFrameSlot Bounded Invariant & Superseding
DUWN_TEST(DirectReceiver_FreshestFrameSlotStrictBounding) {
    FreshestFrameSlot slot;
    DUWN_ASSERT(!slot.HasFrame());
    DUWN_ASSERT(slot.GetSupersededCount() == 0ULL);

    DirectFrame f1;
    f1.frame_id = 1;
    f1.payload = {0x01, 0x02};

    DirectFrame f2;
    f2.frame_id = 2;
    f2.payload = {0x03, 0x04};

    // Put f1
    slot.Put(std::move(f1));
    DUWN_ASSERT(slot.HasFrame());
    DUWN_ASSERT(slot.GetSupersededCount() == 0ULL);

    // Put f2 without taking f1 -> f1 is superseded
    slot.Put(std::move(f2));
    DUWN_ASSERT(slot.HasFrame());
    DUWN_ASSERT(slot.GetSupersededCount() == 1ULL);

    // Take slot: must return f2 (freshest)
    auto opt = slot.Take();
    DUWN_ASSERT(opt.has_value());
    DUWN_ASSERT(opt->frame_id == 2);
    DUWN_ASSERT(opt->payload[0] == 0x03);

    // Slot is now empty
    DUWN_ASSERT(!slot.HasFrame());
    DUWN_ASSERT(!slot.Take().has_value());
}

// 3. DirectFrameAssembler: Ordered Assembly & Byte Integrity
DUWN_TEST(DirectReceiver_AssemblerOrderedPacketDelivery) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);
    MockDirectSender sender(100);

    const uint32_t frame_id = 1;
    const size_t payload_size = 4096;
    const uint32_t packet_count = 4;

    auto packets = sender.CreateFramePackets(frame_id, payload_size, packet_count, DirectPacketFlags::KeyFrame);
    DUWN_ASSERT(packets.size() == packet_count);

    for (const auto& pkt : packets) {
        DUWN_ASSERT(assembler.ProcessPacket(pkt.data(), pkt.size()));
    }

    auto metrics = assembler.GetMetrics();
    DUWN_ASSERT(metrics.total_packets == 4ULL);
    DUWN_ASSERT(metrics.completed_frames == 1ULL);
    DUWN_ASSERT(metrics.incomplete_frames == 0ULL);
    DUWN_ASSERT(metrics.malformed_packets == 0ULL);

    DUWN_ASSERT(slot.HasFrame());
    auto frame = slot.Take();
    DUWN_ASSERT(frame.has_value());
    DUWN_ASSERT(frame->frame_id == frame_id);
    DUWN_ASSERT(frame->is_keyframe);
    DUWN_ASSERT(frame->payload.size() == payload_size);

    // Verify exact byte integrity
    auto expected_payload = MockDirectSender::GeneratePayload(frame_id, payload_size);
    DUWN_ASSERT(frame->payload == expected_payload);
}

// 4. DirectFrameAssembler: Reordered Packets Delivery
DUWN_TEST(DirectReceiver_AssemblerReorderedPacketDelivery) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);
    MockDirectSender sender(100);

    const uint32_t frame_id = 2;
    const size_t payload_size = 3000;
    const uint32_t packet_count = 5;

    auto packets = sender.CreateFramePackets(frame_id, payload_size, packet_count);
    // Reverse packet delivery: packet 4, 3, 2, 1, 0
    std::reverse(packets.begin(), packets.end());

    for (const auto& pkt : packets) {
        DUWN_ASSERT(assembler.ProcessPacket(pkt.data(), pkt.size()));
    }

    auto metrics = assembler.GetMetrics();
    DUWN_ASSERT(metrics.completed_frames == 1ULL);
    DUWN_ASSERT(metrics.malformed_packets == 0ULL);

    auto frame = slot.Take();
    DUWN_ASSERT(frame.has_value());
    DUWN_ASSERT(frame->frame_id == frame_id);
    DUWN_ASSERT(frame->payload.size() == payload_size);

    auto expected_payload = MockDirectSender::GeneratePayload(frame_id, payload_size);
    DUWN_ASSERT(frame->payload == expected_payload);
}

// 5. DirectFrameAssembler: Missing Packet & Superseded by Newer Frame
DUWN_TEST(DirectReceiver_AssemblerMissingPacketSuperseded) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);
    MockDirectSender sender(100);

    // Frame 1 with 3 packets - send only packet 0 and 1 (packet 2 missing)
    auto f1_packets = sender.CreateFramePackets(1, 1500, 3);
    DUWN_ASSERT(assembler.ProcessPacket(f1_packets[0].data(), f1_packets[0].size()));
    DUWN_ASSERT(assembler.ProcessPacket(f1_packets[1].data(), f1_packets[1].size()));

    DUWN_ASSERT(!slot.HasFrame()); // Incomplete

    // Frame 2 arrives complete with 2 packets
    auto f2_packets = sender.CreateFramePackets(2, 1000, 2);
    DUWN_ASSERT(assembler.ProcessPacket(f2_packets[0].data(), f2_packets[0].size()));
    DUWN_ASSERT(assembler.ProcessPacket(f2_packets[1].data(), f2_packets[1].size()));

    auto metrics = assembler.GetMetrics();
    DUWN_ASSERT(metrics.completed_frames == 1ULL);
    DUWN_ASSERT(metrics.incomplete_frames == 1ULL);
    DUWN_ASSERT(metrics.superseded_frames == 1ULL);

    DUWN_ASSERT(slot.HasFrame());
    auto frame = slot.Take();
    DUWN_ASSERT(frame.has_value());
    DUWN_ASSERT(frame->frame_id == 2);
    DUWN_ASSERT(frame->payload.size() == 1000);
}

// 6. DirectFrameAssembler: Duplicate Packet Ignored Safely
DUWN_TEST(DirectReceiver_AssemblerDuplicatePacketIgnored) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);
    MockDirectSender sender(100);

    auto packets = sender.CreateFramePackets(5, 1000, 2);
    DUWN_ASSERT(assembler.ProcessPacket(packets[0].data(), packets[0].size()));
    // Duplicate packet 0
    DUWN_ASSERT(!assembler.ProcessPacket(packets[0].data(), packets[0].size()));
    // Send packet 1
    DUWN_ASSERT(assembler.ProcessPacket(packets[1].data(), packets[1].size()));

    auto metrics = assembler.GetMetrics();
    DUWN_ASSERT(metrics.completed_frames == 1ULL);
    DUWN_ASSERT(metrics.duplicate_packets == 1ULL);
    DUWN_ASSERT(slot.HasFrame());
}

// 7. DirectFrameAssembler: Late Packets Dropped
DUWN_TEST(DirectReceiver_AssemblerLatePacketsDropped) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);
    MockDirectSender sender(100);

    auto f1_packets = sender.CreateFramePackets(1, 1000, 2);
    auto f2_packets = sender.CreateFramePackets(2, 1000, 2);

    // Complete frame 2 first
    DUWN_ASSERT(assembler.ProcessPacket(f2_packets[0].data(), f2_packets[0].size()));
    DUWN_ASSERT(assembler.ProcessPacket(f2_packets[1].data(), f2_packets[1].size()));
    DUWN_ASSERT(assembler.GetMetrics().completed_frames == 1ULL);

    // Now late packet from frame 1 arrives
    DUWN_ASSERT(!assembler.ProcessPacket(f1_packets[0].data(), f1_packets[0].size()));
    DUWN_ASSERT(assembler.GetMetrics().late_packets == 1ULL);
}

// 8. DirectFrameAssembler: Malformed Packets Handled Safely
DUWN_TEST(DirectReceiver_AssemblerMalformedPacketsSafeRejection) {
    FreshestFrameSlot slot;
    DirectFrameAssembler assembler(slot);

    // Truncated packet
    auto trunc = MockDirectSender::CreateTruncatedPacket(15);
    DUWN_ASSERT(!assembler.ProcessPacket(trunc.data(), trunc.size()));

    // Corrupt header
    auto corrupt = MockDirectSender::CreateCorruptHeaderPacket(1, 0, 2);
    DUWN_ASSERT(!assembler.ProcessPacket(corrupt.data(), corrupt.size()));

    // Out of bounds index
    auto out_of_bounds = MockDirectSender::CreateOutOfBoundsIndexPacket(1, 2);
    DUWN_ASSERT(!assembler.ProcessPacket(out_of_bounds.data(), out_of_bounds.size()));

    auto metrics = assembler.GetMetrics();
    DUWN_ASSERT(metrics.malformed_packets == 3ULL);
    DUWN_ASSERT(!slot.HasFrame());
}

// 9. DirectSession: Session Lifecycle and Isolation
DUWN_TEST(DirectReceiver_DirectSessionLifecycleAndReset) {
    FreshestFrameSlot slot;
    DirectSession session(500, slot);
    DUWN_ASSERT(session.GetSessionId() == 500);

    MockDirectSender sender500(500);
    MockDirectSender sender999(999);

    auto pkt_wrong_session = sender999.CreateFramePackets(1, 500, 1);
    DUWN_ASSERT(!session.OnPacketReceived(pkt_wrong_session[0].data(), pkt_wrong_session[0].size()));

    auto pkt_right_session = sender500.CreateFramePackets(1, 500, 1);
    DUWN_ASSERT(session.OnPacketReceived(pkt_right_session[0].data(), pkt_right_session[0].size()));
    DUWN_ASSERT(slot.HasFrame());

    // Reset session
    session.Reset(600);
    DUWN_ASSERT(session.GetSessionId() == 600);
    DUWN_ASSERT(!slot.HasFrame());
}

// 10. DirectReceiver: End-to-End Ingestion & Bounded Memory
DUWN_TEST(DirectReceiver_DirectReceiverEndToEndIngestion) {
    DirectReceiver receiver;
    MockDirectSender sender(42);

    receiver.StartNewSession(42);

    // Stream 20 frames rapidly
    for (uint32_t f = 1; f <= 20; ++f) {
        auto packets = sender.CreateFramePackets(f, 2048, 3);
        for (const auto& pkt : packets) {
            receiver.IngestPacket(pkt.data(), pkt.size());
        }
    }

    auto metrics = receiver.GetMetrics();
    DUWN_ASSERT(metrics.completed_frames == 20ULL);
    DUWN_ASSERT(metrics.malformed_packets == 0ULL);

    // The slot must hold AT MOST 1 frame (the freshest, frame 20)
    DUWN_ASSERT(receiver.GetFrameSlot().HasFrame());
    auto latest = receiver.GetFrameSlot().Take();
    DUWN_ASSERT(latest.has_value());
    DUWN_ASSERT(latest->frame_id == 20);

    // Exactly 19 frames were superseded in the slot because we didn't drain between puts
    DUWN_ASSERT(receiver.GetFrameSlot().GetSupersededCount() == 19ULL);
}

// 11. DirectUdpTransport: Localhost Datagram Loopback Integration
DUWN_TEST(DirectReceiver_DirectUdpTransportLoopbackDelivery) {
    // Spin up UDP transport on ephemeral localhost port
    auto receiver_transport = std::make_unique<DirectUdpTransport>("127.0.0.1", uint16_t{0});
    DUWN_ASSERT(receiver_transport->Start());
    uint16_t receiver_port = receiver_transport->GetBoundPort();
    DUWN_ASSERT(receiver_port > 0);

    std::atomic<bool> packet_arrived{false};
    std::vector<uint8_t> received_bytes;
    std::mutex rcv_mutex;

    receiver_transport->SetPacketCallback([&](const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(rcv_mutex);
        received_bytes.assign(data, data + size);
        packet_arrived.store(true, std::memory_order_release);
    });

    // Sender transport
    DirectUdpTransport sender_transport("127.0.0.1", uint16_t{0});
    DUWN_ASSERT(sender_transport.Start());
    DUWN_ASSERT(sender_transport.SetDestination("127.0.0.1", receiver_port));

    MockDirectSender sender(77);
    auto packets = sender.CreateFramePackets(1, 256, 1);
    DUWN_ASSERT(packets.size() == 1);

    DUWN_ASSERT(sender_transport.SendPacket(packets[0].data(), packets[0].size()));

    // Wait for loopback arrival (up to 500 ms)
    for (int i = 0; i < 50 && !packet_arrived.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    DUWN_ASSERT(packet_arrived.load(std::memory_order_acquire));

    {
        std::lock_guard<std::mutex> lock(rcv_mutex);
        DUWN_ASSERT(received_bytes == packets[0]);
    }

    sender_transport.Stop();
    receiver_transport->Stop();
}
