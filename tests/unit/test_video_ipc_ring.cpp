// test_video_ipc_ring.cpp — unit tests for VideoIpcProducer and VideoIpcConsumer
#include "common/ipc/VideoIpcRing.h"
#include <vector>
#include <string>

using namespace duwn::ipc;

DUWN_TEST(ipc_ring_create_and_connect) {
    const std::wstring shm_name = L"Local\\DuwnTest_IpcRing_Create";
    const std::wstring evt_name = L"Local\\DuwnTest_IpcEvt_Create";

    VideoIpcProducer producer;
    bool prod_ok = producer.Initialize(shm_name, evt_name, 4, 1024);
    DUWN_ASSERT(prod_ok);
    DUWN_ASSERT(producer.ControlBlock() != nullptr);
    DUWN_ASSERT(producer.ControlBlock()->magic == kIpcVideoMagic);
    DUWN_ASSERT(producer.ControlBlock()->slot_count == 4);

    VideoIpcConsumer consumer;
    bool cons_ok = consumer.Open(shm_name, evt_name);
    DUWN_ASSERT(cons_ok);
    DUWN_ASSERT(consumer.ControlBlock() != nullptr);
    DUWN_ASSERT(consumer.ControlBlock()->slot_count == 4);

    consumer.Close();
    producer.Close();
}

DUWN_TEST(ipc_ring_single_frame_write_read) {
    const std::wstring shm_name = L"Local\\DuwnTest_IpcRing_Single";
    const std::wstring evt_name = L"Local\\DuwnTest_IpcEvt_Single";

    VideoIpcProducer producer;
    DUWN_ASSERT(producer.Initialize(shm_name, evt_name, 4, 4096));

    VideoIpcConsumer consumer;
    DUWN_ASSERT(consumer.Open(shm_name, evt_name));

    IpcAccessUnitHeader hdr{};
    hdr.sequence_number = 101;
    hdr.pts_ns = 1'000'000'000LL;
    hdr.flags = IpcVideoFlags::Keyframe | IpcVideoFlags::HasSps;
    hdr.width = 1920;
    hdr.height = 1080;

    const uint8_t test_payload[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f};
    bool written = producer.WriteAccessUnit(hdr, test_payload, sizeof(test_payload));
    DUWN_ASSERT(written);
    DUWN_ASSERT(producer.ControlBlock()->total_frames_written == 1);

    bool frame_received = false;
    consumer.Start([&](const IpcAccessUnitHeader& h, const uint8_t* p, size_t s) {
        DUWN_ASSERT(h.sequence_number == 101);
        DUWN_ASSERT(h.pts_ns == 1'000'000'000LL);
        DUWN_ASSERT(h.width == 1920);
        DUWN_ASSERT(h.height == 1080);
        DUWN_ASSERT(s == sizeof(test_payload));
        DUWN_ASSERT(std::memcmp(p, test_payload, sizeof(test_payload)) == 0);
        frame_received = true;
    });

    size_t drained = consumer.DrainAvailable();
    DUWN_ASSERT(drained == 1);
    DUWN_ASSERT(frame_received);
    DUWN_ASSERT(consumer.ControlBlock()->total_frames_consumed == 1);

    consumer.Stop();
    producer.Close();
}

DUWN_TEST(ipc_ring_overwrite_drops_oldest_retains_newest) {
    const std::wstring shm_name = L"Local\\DuwnTest_IpcRing_Overwrite";
    const std::wstring evt_name = L"Local\\DuwnTest_IpcEvt_Overwrite";

    // Ring with 3 slots
    VideoIpcProducer producer;
    DUWN_ASSERT(producer.Initialize(shm_name, evt_name, 3, 1024));

    VideoIpcConsumer consumer;
    DUWN_ASSERT(consumer.Open(shm_name, evt_name));

    // Producer pushes 6 frames into capacity-3 ring without consumer reading
    for (uint64_t i = 1; i <= 6; ++i) {
        IpcAccessUnitHeader hdr{};
        hdr.sequence_number = i;
        hdr.pts_ns = static_cast<int64_t>(i * 16'666'667LL);
        uint8_t dummy = static_cast<uint8_t>(i);
        DUWN_ASSERT(producer.WriteAccessUnit(hdr, &dummy, 1));
    }

    // Producer must have overwritten older frames
    DUWN_ASSERT(producer.ControlBlock()->total_frames_written == 6);
    DUWN_ASSERT(producer.ControlBlock()->total_frames_dropped_producer > 0);

    // Drain consumer: must only receive remaining newest frames (slots 4, 5, 6 or 5, 6)
    std::vector<uint64_t> received_seqs;
    consumer.Start([&](const IpcAccessUnitHeader& h, const uint8_t*, size_t) {
        received_seqs.push_back(h.sequence_number);
    });

    size_t drained = consumer.DrainAvailable();
    DUWN_ASSERT(drained > 0);
    DUWN_ASSERT(!received_seqs.empty());
    // The latest frame (seq 6) MUST be present
    DUWN_ASSERT(received_seqs.back() == 6);
    // Oldest frames (seq 1, 2) MUST have been dropped
    DUWN_ASSERT(received_seqs.front() > 2);

    consumer.Stop();
    producer.Close();
}

DUWN_TEST(ipc_ring_reject_oversized_payload) {
    const std::wstring shm_name = L"Local\\DuwnTest_IpcRing_Oversized";
    const std::wstring evt_name = L"Local\\DuwnTest_IpcEvt_Oversized";

    // Slot size: Header (64 bytes) + 256 bytes payload = 320 bytes total
    VideoIpcProducer producer;
    DUWN_ASSERT(producer.Initialize(shm_name, evt_name, 2, sizeof(IpcAccessUnitHeader) + 256));

    IpcAccessUnitHeader hdr{};
    std::vector<uint8_t> large_payload(512, 0xFF); // 512 bytes > 256 allowed

    // Write should be rejected cleanly without memory corruption or crash
    bool written = producer.WriteAccessUnit(hdr, large_payload.data(), large_payload.size());
    DUWN_ASSERT(!written);
    DUWN_ASSERT(producer.ControlBlock()->total_frames_written == 0);

    producer.Close();
}
