// Tests for JitterBuffer

#include "network/JitterBuffer.h"
#include "network/RtpPacket.h"

// Helper: create a minimal RtpPacket with given sequence and timestamp
static duwn::network::RtpPacket MakePkt(uint16_t seq, uint32_t ts,
                                        const uint8_t* payload, size_t size,
                                        int64_t arrival_ns = 0) {
    duwn::network::RtpPacket p;
    p.version      = 2;
    p.sequence     = seq;
    p.timestamp    = ts;
    p.arrival_ns   = arrival_ns;
    p.payload      = std::span<const uint8_t>{payload, size};
    return p;
}

DUWN_TEST(jitter_buffer_in_order_0_1_2) {
    duwn::network::JitterBuffer buf{33'000'000LL}; // 33ms hold
    uint8_t d[] = {1, 2, 3};

    DUWN_ASSERT(buf.Push(MakePkt(0, 0,    d, 3, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(1, 3000, d, 3, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(2, 6000, d, 3, 0)));
    DUWN_ASSERT(buf.Size() == 3);

    // Pop before hold window expires — must return nullopt
    auto e_early = buf.Pop(10'000'000LL);
    DUWN_ASSERT(!e_early.has_value());

    // After 33ms hold time passes, pop in exact order: 0, 1, 2
    auto e0 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e0.has_value());
    DUWN_ASSERT(e0->sequence == 0);

    auto e1 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e1.has_value());
    DUWN_ASSERT(e1->sequence == 1);

    auto e2 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e2.has_value());
    DUWN_ASSERT(e2->sequence == 2);

    auto e3 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(!e3.has_value());
    DUWN_ASSERT(buf.Size() == 0);
}

DUWN_TEST(jitter_buffer_reorder_0_2_1) {
    duwn::network::JitterBuffer buf{33'000'000LL};
    uint8_t d[] = {0};

    DUWN_ASSERT(buf.Push(MakePkt(0, 0,    d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(2, 6000, d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(1, 3000, d, 1, 0)));
    DUWN_ASSERT(buf.Size() == 3);

    // After hold, must pop 0, 1, 2 in order
    auto e0 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e0.has_value());
    DUWN_ASSERT(e0->sequence == 0);

    auto e1 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e1.has_value());
    DUWN_ASSERT(e1->sequence == 1);

    auto e2 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e2.has_value());
    DUWN_ASSERT(e2->sequence == 2);

    auto e3 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(!e3.has_value());
}

DUWN_TEST(jitter_buffer_reorder_1_before_0) {
    duwn::network::JitterBuffer buf{33'000'000LL};
    uint8_t d[] = {0};

    // Seq 1 arrives before seq 0 at stream startup
    DUWN_ASSERT(buf.Push(MakePkt(1, 3000, d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(0, 0,    d, 1, 0)));
    DUWN_ASSERT(buf.Size() == 2);

    // After hold, seq 0 must come first, then seq 1
    auto e0 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e0.has_value());
    DUWN_ASSERT(e0->sequence == 0);

    auto e1 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e1.has_value());
    DUWN_ASSERT(e1->sequence == 1);

    auto e2 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(!e2.has_value());
}

DUWN_TEST(jitter_buffer_sequence_wrap_boundary) {
    // Sequence wraps from 65534, 65535, 0, 1
    duwn::network::JitterBuffer buf{0LL}; // 0 hold for immediate release
    uint8_t d[] = {0};

    DUWN_ASSERT(buf.Push(MakePkt(65534, 0,    d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(65535, 1000, d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(0,     2000, d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(1,     3000, d, 1, 0)));
    DUWN_ASSERT(buf.Size() == 4);

    auto e0 = buf.Pop(0);
    DUWN_ASSERT(e0.has_value());
    DUWN_ASSERT(e0->sequence == 65534);

    auto e1 = buf.Pop(0);
    DUWN_ASSERT(e1.has_value());
    DUWN_ASSERT(e1->sequence == 65535);

    auto e2 = buf.Pop(0);
    DUWN_ASSERT(e2.has_value());
    DUWN_ASSERT(e2->sequence == 0);

    auto e3 = buf.Pop(0);
    DUWN_ASSERT(e3.has_value());
    DUWN_ASSERT(e3->sequence == 1);

    auto e4 = buf.Pop(0);
    DUWN_ASSERT(!e4.has_value());
}

DUWN_TEST(jitter_buffer_duplicate) {
    duwn::network::JitterBuffer buf{33'000'000LL};
    uint8_t d[] = {42};

    DUWN_ASSERT(buf.Push(MakePkt(5, 100, d, 1, 0)));
    DUWN_ASSERT(buf.Size() == 1);

    // Duplicate packet 5 must be rejected
    bool accepted = buf.Push(MakePkt(5, 100, d, 1, 1000));
    DUWN_ASSERT(!accepted);
    DUWN_ASSERT(buf.Size() == 1);

    auto e = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e.has_value());
    DUWN_ASSERT(e->sequence == 5);

    auto e2 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(!e2.has_value());
}

DUWN_TEST(jitter_buffer_old_packet) {
    duwn::network::JitterBuffer buf{33'000'000LL};
    uint8_t d[] = {0};

    DUWN_ASSERT(buf.Push(MakePkt(10, 100, d, 1, 0)));
    DUWN_ASSERT(buf.Push(MakePkt(11, 200, d, 1, 0)));

    auto e10 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e10.has_value());
    DUWN_ASSERT(e10->sequence == 10);

    // Old packets behind next_seq (11) must be rejected
    DUWN_ASSERT(!buf.Push(MakePkt(9,  50,  d, 1, 0)));
    DUWN_ASSERT(!buf.Push(MakePkt(10, 100, d, 1, 0)));

    auto e11 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(e11.has_value());
    DUWN_ASSERT(e11->sequence == 11);

    auto e12 = buf.Pop(34'000'000LL);
    DUWN_ASSERT(!e12.has_value());
}

DUWN_TEST(jitter_buffer_flush) {
    duwn::network::JitterBuffer buf{0LL};
    uint8_t d[] = {0};

    DUWN_ASSERT(buf.Push(MakePkt(10, 0, d, 1)));
    DUWN_ASSERT(buf.Push(MakePkt(11, 0, d, 1)));
    DUWN_ASSERT(buf.Size() == 2);

    buf.Flush();
    DUWN_ASSERT(buf.Size() == 0);

    auto e = buf.Pop(0);
    DUWN_ASSERT(!e.has_value());

    // After flush, new packets starting at 0 are accepted cleanly
    DUWN_ASSERT(buf.Push(MakePkt(0, 0, d, 1)));
    auto e0 = buf.Pop(0);
    DUWN_ASSERT(e0.has_value());
    DUWN_ASSERT(e0->sequence == 0);
}
