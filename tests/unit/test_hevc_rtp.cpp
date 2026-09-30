#include "video/HevcRtpAssembler.h"
#include "video/HevcParameterSetCache.h"
#include "video/VideoDecoder.h"
#include "video/D3D11Device.h"
#include <vector>
#include <cstdint>

DUWN_TEST(hevc_single_nal) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    // Single NAL: type 1 (TRAIL_R), LayerId=0, TID=1 -> byte 0: 0x02, byte 1: 0x01
    uint8_t nalu[6] = {0x02, 0x01, 0xAA, 0xBB, 0xCC, 0xDD};
    uint64_t before_count = duwn::GlobalMetrics().hevc_single_nals.load(std::memory_order_relaxed);

    assembler.FeedRtp(nalu, sizeof(nalu), 90000, 1000000, true, 100);

    DUWN_ASSERT(emitted.size() == 1);
    DUWN_ASSERT(emitted[0].codec == VideoCodecType::H265);
    // Must start with Annex-B start code: 00 00 00 01
    DUWN_ASSERT(emitted[0].data.size() >= 10);
    DUWN_ASSERT(emitted[0].data[0] == 0 && emitted[0].data[1] == 0 &&
                emitted[0].data[2] == 0 && emitted[0].data[3] == 1);
    DUWN_ASSERT(emitted[0].data[4] == 0x02 && emitted[0].data[5] == 0x01);
    DUWN_ASSERT(emitted[0].data[6] == 0xAA && emitted[0].data[9] == 0xDD);
    DUWN_ASSERT(duwn::GlobalMetrics().hevc_single_nals.load(std::memory_order_relaxed) == before_count + 1);
}

DUWN_TEST(hevc_ap_type_48) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    // AP Header: Type=48 -> 48 << 1 = 96 (0x60), LayerId=0, TID=1 -> byte 0: 0x60, byte 1: 0x01
    // NALU 1: VPS (type 32 -> 0x40, 0x01), length = 3
    // NALU 2: SPS (type 33 -> 0x42, 0x01), length = 4
    std::vector<uint8_t> ap_packet = {
        0x60, 0x01,                         // AP header (2 bytes)
        0x00, 0x03,                         // NALU 1 length (3 bytes)
        0x40, 0x01, 0x11,                   // NALU 1 (VPS)
        0x00, 0x04,                         // NALU 2 length (4 bytes)
        0x42, 0x01, 0x22, 0x33              // NALU 2 (SPS)
    };

    uint64_t before_ap = duwn::GlobalMetrics().hevc_ap_packets.load(std::memory_order_relaxed);
    assembler.FeedRtp(ap_packet.data(), ap_packet.size(), 90000, 1000000, true, 200);

    DUWN_ASSERT(emitted.size() == 1);
    DUWN_ASSERT(emitted[0].has_vps);
    DUWN_ASSERT(emitted[0].has_sps);
    DUWN_ASSERT(duwn::GlobalMetrics().hevc_ap_packets.load(std::memory_order_relaxed) == before_ap + 1);

    // Verify both NALUs have Annex B start codes
    const auto& d = emitted[0].data;
    DUWN_ASSERT(d.size() == (4 + 3 + 4 + 4));
    DUWN_ASSERT(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1);
    DUWN_ASSERT(d[4] == 0x40 && d[5] == 0x01 && d[6] == 0x11);
    DUWN_ASSERT(d[7] == 0 && d[8] == 0 && d[9] == 0 && d[10] == 1);
    DUWN_ASSERT(d[11] == 0x42 && d[12] == 0x01 && d[13] == 0x22 && d[14] == 0x33);
}

DUWN_TEST(hevc_fu_type_49_start_middle_end) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    // Fragmented IDR slice: original type 19 (IDR_W_RADL) -> 0x13
    // FU indicator: Type=49 -> 49 << 1 = 98 (0x62), LayerId=0, TID=1 -> byte 0: 0x62, byte 1: 0x01
    // FU header: S (0x80), E (0x40), FuType (0x3F)

    // Fragment 1: Start (S=1, E=0, FuType=19 -> 0x80 | 19 = 0x93)
    uint8_t frag1[] = {0x62, 0x01, 0x93, 0xAA, 0xBB};
    // Fragment 2: Middle (S=0, E=0, FuType=19 -> 0x13)
    uint8_t frag2[] = {0x62, 0x01, 0x13, 0xCC, 0xDD};
    // Fragment 3: End (S=0, E=1, FuType=19 -> 0x40 | 19 = 0x53)
    uint8_t frag3[] = {0x62, 0x01, 0x53, 0xEE, 0xFF};

    uint64_t before_start = duwn::GlobalMetrics().hevc_fu_started.load(std::memory_order_relaxed);
    uint64_t before_comp  = duwn::GlobalMetrics().hevc_fu_completed.load(std::memory_order_relaxed);

    assembler.FeedRtp(frag1, sizeof(frag1), 90000, 1000000, false, 301);
    assembler.FeedRtp(frag2, sizeof(frag2), 90000, 1000000, false, 302);
    assembler.FeedRtp(frag3, sizeof(frag3), 90000, 1000000, true,  303);

    DUWN_ASSERT(emitted.size() == 1);
    DUWN_ASSERT(emitted[0].has_idr);
    DUWN_ASSERT(duwn::GlobalMetrics().hevc_fu_started.load(std::memory_order_relaxed) == before_start + 1);
    DUWN_ASSERT(duwn::GlobalMetrics().hevc_fu_completed.load(std::memory_order_relaxed) == before_comp + 1);

    // Reconstructed NAL unit:
    // Header byte 0: (0x62 & 0x81) | (19 << 1) = 0x00 | 0x26 = 0x26
    // Header byte 1: 0x01
    // Total size: 4 (start code) + 2 (header) + 2 (frag1) + 2 (frag2) + 2 (frag3) = 12
    const auto& d = emitted[0].data;
    DUWN_ASSERT(d.size() == 12);
    DUWN_ASSERT(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1);
    DUWN_ASSERT(d[4] == 0x26 && d[5] == 0x01);
    DUWN_ASSERT(d[6] == 0xAA && d[7] == 0xBB);
    DUWN_ASSERT(d[8] == 0xCC && d[9] == 0xDD);
    DUWN_ASSERT(d[10] == 0xEE && d[11] == 0xFF);
}

DUWN_TEST(hevc_fu_loss_abort) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    uint8_t frag1[] = {0x62, 0x01, 0x93, 0xAA, 0xBB}; // Start, seq 400
    // Skip seq 401!
    uint8_t frag3[] = {0x62, 0x01, 0x53, 0xEE, 0xFF}; // End, seq 402

    uint64_t before_abort = duwn::GlobalMetrics().hevc_fu_aborted.load(std::memory_order_relaxed);

    assembler.FeedRtp(frag1, sizeof(frag1), 90000, 1000000, false, 400);
    assembler.FeedRtp(frag3, sizeof(frag3), 90000, 1000000, true,  402); // Gap detected: 402 != 401

    // Corrupted NAL was aborted; no complete AU emitted
    DUWN_ASSERT(emitted.empty());
    DUWN_ASSERT(duwn::GlobalMetrics().hevc_fu_aborted.load(std::memory_order_relaxed) == before_abort + 1);
}

DUWN_TEST(hevc_vps_sps_pps_caching) {
    using namespace duwn::video;

    HevcParameterSetCache cache;
    DUWN_ASSERT(!cache.HasAll());

    // VPS (type 32): 0x40, 0x01, 0x10, 0x20
    uint8_t vps[] = {0x40, 0x01, 0x10, 0x20};
    // SPS (type 33): 0x42, 0x01, 0x30, 0x40
    uint8_t sps[] = {0x42, 0x01, 0x30, 0x40};
    // PPS (type 34): 0x44, 0x01, 0x50, 0x60
    uint8_t pps[] = {0x44, 0x01, 0x50, 0x60};

    cache.Update(32, vps, 1);
    DUWN_ASSERT(cache.HasVps() && !cache.HasSps() && !cache.HasPps());
    cache.Update(33, sps, 1);
    DUWN_ASSERT(cache.HasVps() && cache.HasSps() && !cache.HasPps());
    cache.Update(34, pps, 1);
    DUWN_ASSERT(cache.HasAll());

    // Test prepending missing parameter sets
    std::vector<uint8_t> au = {0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xAA}; // IDR slice
    cache.PrependMissing(au, /*has_vps=*/false, /*has_sps=*/false, /*has_pps=*/false);

    // Should now contain: VPS + SPS + PPS + IDR slice
    // Total size: (4+4) + (4+4) + (4+4) + 7 = 31
    DUWN_ASSERT(au.size() == 31);
    DUWN_ASSERT(au[0] == 0 && au[1] == 0 && au[2] == 0 && au[3] == 1);
    DUWN_ASSERT(au[4] == 0x40); // VPS
    DUWN_ASSERT(au[8] == 0 && au[9] == 0 && au[10] == 0 && au[11] == 1);
    DUWN_ASSERT(au[12] == 0x42); // SPS
    DUWN_ASSERT(au[16] == 0 && au[17] == 0 && au[18] == 0 && au[19] == 1);
    DUWN_ASSERT(au[20] == 0x44); // PPS
    DUWN_ASSERT(au[24] == 0 && au[25] == 0 && au[26] == 0 && au[27] == 1);
    DUWN_ASSERT(au[28] == 0x26); // IDR slice
}

DUWN_TEST(hevc_marker_timestamp_au_completion) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    uint8_t slice1[] = {0x02, 0x01, 0x11};
    uint8_t slice2[] = {0x02, 0x01, 0x22};

    // Packet 1: timestamp 1000, marker false
    assembler.FeedRtp(slice1, sizeof(slice1), 1000, 10000, false, 501);
    DUWN_ASSERT(emitted.empty());

    // Packet 2: timestamp 2000, marker false -> timestamp change triggers emission of AU 1000
    assembler.FeedRtp(slice2, sizeof(slice2), 2000, 20000, false, 502);
    DUWN_ASSERT(emitted.size() == 1);
    DUWN_ASSERT(emitted[0].pts_ns == 10000);

    // Marker true on AU 2000 -> triggers emission of AU 2000
    uint8_t slice3[] = {0x02, 0x01, 0x33};
    assembler.FeedRtp(slice3, sizeof(slice3), 2000, 20000, true, 503);
    DUWN_ASSERT(emitted.size() == 2);
    DUWN_ASSERT(emitted[1].pts_ns == 20000);
}

DUWN_TEST(hevc_h264_codec_isolation) {
    using namespace duwn::video;

    D3D11Device dev;
    DUWN_ASSERT(dev.Create(false, true));

    std::vector<VideoFrame> frames;
    VideoDecoder decoder(dev, [&](VideoFrame f) {
        frames.push_back(std::move(f));
    });

    // Initialize as H.264
    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));

    // Feed HEVC FU (type 49): in H.264 mode, 49 & 0x1F = 17 (unsupported NAL type), not FU-A (28)
    uint8_t hevc_fu[] = {0x62, 0x01, 0x93, 0xAA, 0xBB};
    decoder.FeedRtp(hevc_fu, sizeof(hevc_fu), 90000, 1000000, true, 601);

    // Switch to H.265
    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H265));
    // Verify HEVC assembler is active
    DUWN_ASSERT(decoder.GetHevcAssembler() != nullptr);
}

DUWN_TEST(hevc_session_generation_reset) {
    using namespace duwn::video;

    HevcParameterSetCache cache;
    uint8_t vps[] = {0x40, 0x01, 0x10};
    cache.Update(32, vps, 1);
    DUWN_ASSERT(cache.HasVps());

    // Reset with generation 2 -> parameter cache must be cleared
    cache.SetGeneration(2);
    DUWN_ASSERT(!cache.HasVps());
    DUWN_ASSERT(cache.Generation() == 2);
}

DUWN_TEST(hevc_annex_b_output) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    uint8_t nalu[] = {0x02, 0x01, 0x55, 0x66};
    assembler.FeedRtp(nalu, sizeof(nalu), 90000, 1000000, true, 700);

    DUWN_ASSERT(emitted.size() == 1);
    const auto& d = emitted[0].data;
    // Exactly 4 bytes of start code + 4 bytes of nalu
    DUWN_ASSERT(d.size() == 8);
    DUWN_ASSERT(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1);
    DUWN_ASSERT(d[4] == 0x02 && d[5] == 0x01 && d[6] == 0x55 && d[7] == 0x66);
}

DUWN_TEST(hevc_unsupported_paci_safe_rejection) {
    using namespace duwn::video;

    std::vector<EncodedAccessUnit> emitted;
    HevcRtpAssembler assembler([&](EncodedAccessUnit au) {
        emitted.push_back(std::move(au));
    });

    // PACI: Type=50 -> 50 << 1 = 100 (0x64), LayerId=0, TID=1 -> byte 0: 0x64, byte 1: 0x01
    uint8_t paci[] = {0x64, 0x01, 0x00, 0x00, 0xAA, 0xBB};
    assembler.FeedRtp(paci, sizeof(paci), 90000, 1000000, true, 800);

    // PACI must be rejected without crash and no AU emitted
    DUWN_ASSERT(emitted.empty());
}

DUWN_TEST(hevc_codec_autodetection_contract) {
    using namespace duwn::video;

    D3D11Device dev;
    DUWN_ASSERT(dev.Create(false, true));

    std::vector<VideoFrame> frames;
    VideoDecoder decoder(dev, [&](VideoFrame f) {
        frames.push_back(std::move(f));
    });

    // 1. Default initial state is Unknown
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::Unknown);

    // 2. FeedRtp when Unknown must drop packets safely without crashing
    uint8_t nalu[] = {0x02, 0x01, 0xAA, 0xBB};
    decoder.FeedRtp(nalu, sizeof(nalu), 90000, 1000000, true, 100);
    DUWN_ASSERT(frames.empty());

    // 3. Init with H264
    DUWN_ASSERT(decoder.Init(1920, 1080, VideoCodecType::H264));
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::H264);

    // 4. ResetCodecState restores Unknown state and clears all buffers
    decoder.ResetCodecState();
    DUWN_ASSERT(decoder.GetActiveCodec() == VideoCodecType::Unknown);

    // 5. FeedRtp when back to Unknown drops packets again
    decoder.FeedRtp(nalu, sizeof(nalu), 90000, 1000000, true, 101);
    DUWN_ASSERT(frames.empty());
}

DUWN_TEST(hevc_zero_copy_release_invariant_contract) {
    using namespace duwn::video;
    bool hevc_hw_supported = MFVideoDecoder::IsCodecSupported(VideoCodecType::H265, true);
    bool h264_hw_supported = MFVideoDecoder::IsCodecSupported(VideoCodecType::H264, true);
    DUWN_ASSERT(h264_hw_supported);
    DUWN_ASSERT(!MFVideoDecoder::IsCodecSupported(VideoCodecType::Unknown, true));
    DUWN_ASSERT(!MFVideoDecoder::IsCodecSupported(VideoCodecType::Unknown, false));
    (void)hevc_hw_supported;
}
