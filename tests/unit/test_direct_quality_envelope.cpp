#include "direct/quality/VideoQualityEvaluator.h"
#include "direct/DirectLatencyTracker.h"
#include "direct/DirectPacketizer.h"
#include "direct/encoder/DirectVideoEncoder.h"
#include <vector>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

using namespace duwn::direct::quality;
using namespace duwn::direct;

// 1. Exactness of Engineering Metrics (PSNR & SSIM)
DUWN_TEST(DirectQuality_PSNR_SSIM_MathematicalProperties) {
    const size_t w = 320;
    const size_t h = 240;

    auto ref = VideoQualityEvaluator::GenerateUiDetailPattern(w, h);
    auto identical = ref;

    // Identical frames must yield maximum theoretical fidelity
    double psnr_id = VideoQualityEvaluator::ComputePSNR(ref.data(), identical.data(), w, h);
    double ssim_id = VideoQualityEvaluator::ComputeSSIM(ref.data(), identical.data(), w, h);

    DUWN_ASSERT(psnr_id >= 99.0);
    DUWN_ASSERT(std::abs(ssim_id - 1.0) < 0.001);

    // Completely inverted frames must yield near-zero SSIM and very low PSNR
    std::vector<uint8_t> inverted(w * h);
    for (size_t i = 0; i < w * h; ++i) inverted[i] = 255 - ref[i];

    double psnr_inv = VideoQualityEvaluator::ComputePSNR(ref.data(), inverted.data(), w, h);
    double ssim_inv = VideoQualityEvaluator::ComputeSSIM(ref.data(), inverted.data(), w, h);

    DUWN_ASSERT(psnr_inv < 15.0);
    DUWN_ASSERT(ssim_inv < 0.20);
}

// 2. Full Test Matrix & Operating Point Envelope Sweep
struct OperatingPointResult {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fps{60};
    uint32_t bitrate_bps{0};
    double encode_p50_ms{0.0};
    double encode_p95_ms{0.0};
    double transport_ms{0.0};
    double decode_ms{0.0};
    double latency_e2e_ms{0.0};
    double packet_loss_pct{0.0};
    double psnr_db{0.0};
    double ssim{0.0};
    bool small_text_readable{false};
    bool high_motion_retained{false};
    std::string visual_summary;
};

static OperatingPointResult EvaluateOperatingPoint(uint32_t width, uint32_t height,
                                                  uint32_t fps, uint32_t bitrate_bps) {
    OperatingPointResult res;
    res.width = width;
    res.height = height;
    res.fps = fps;
    res.bitrate_bps = bitrate_bps;
    res.packet_loss_pct = 0.0; // Clean LAN target

    // Generate reference test frames
    auto ref_ui = VideoQualityEvaluator::GenerateUiDetailPattern(width, height);
    auto ref_motion = VideoQualityEvaluator::GenerateHighMotionPattern(width, height, 1);

    // Simulate H.264 quantizer distortion under tested bitrate
    auto test_ui = VideoQualityEvaluator::SimulateH264Distortion(ref_ui, width, height, bitrate_bps, fps);
    auto test_motion = VideoQualityEvaluator::SimulateH264Distortion(ref_motion, width, height, bitrate_bps, fps);

    // Evaluate engineering quality metrics
    auto eval_ui = VideoQualityEvaluator::Evaluate(ref_ui, test_ui, width, height, bitrate_bps);
    auto eval_motion = VideoQualityEvaluator::Evaluate(ref_motion, test_motion, width, height, bitrate_bps);

    // Composite metrics (weighting UI sharpness 60% and high-motion 40%)
    res.psnr_db = (eval_ui.psnr_db * 0.6) + (eval_motion.psnr_db * 0.4);
    res.ssim = (eval_ui.ssim * 0.6) + (eval_motion.ssim * 0.4);
    res.small_text_readable = eval_ui.small_text_readable;
    res.high_motion_retained = eval_motion.high_motion_retained;
    res.visual_summary = eval_ui.visual_summary;

    // Model hardware latency based on frame byte budget
    double bytes_per_frame = static_cast<double>(bitrate_bps) / (fps * 8.0);
    double datagram_count = std::ceil(bytes_per_frame / 1400.0);

    // Latency models for Apple VideoToolbox HW + Media Foundation HW
    // Base nominal encode: 3.0ms, slightly scaling with slice complexity
    res.encode_p50_ms = 3.0 + (bytes_per_frame / (64.0 * 1024.0)) * 1.2;
    res.encode_p95_ms = res.encode_p50_ms + 0.8;

    // Transport latency over LAN UDP (0.8ms base + serialization of datagrams)
    res.transport_ms = 0.8 + (datagram_count * 0.03);

    // Hardware decode latency (2.4ms base + byte throughput)
    res.decode_ms = 2.4 + (bytes_per_frame / (64.0 * 1024.0)) * 1.0;

    // Total capture callback -> Present latency (including 1.0ms render swapchain)
    res.latency_e2e_ms = res.encode_p50_ms + res.transport_ms + res.decode_ms + 1.0;

    return res;
}

DUWN_TEST(DirectQuality_SyntheticOperatingPointMatrixModel) {
    const std::vector<uint32_t> kBitrates = {
        2'000'000,   // 2 Mbps (Low / Starved)
        4'000'000,   // 4 Mbps (Sub-floor)
        6'000'000,   // 6 Mbps (Candidate Floor for 1080p)
        8'000'000,   // 8 Mbps
        12'000'000,  // 12 Mbps (Nominal Target)
        16'000'000,  // 16 Mbps
        20'000'000,  // 20 Mbps
        25'000'000   // 25 Mbps (High ceiling)
    };

    const std::vector<std::pair<uint32_t, uint32_t>> kResolutions = {
        {1920, 1080}, // 1080p
        {2560, 1440}  // 2.5K Retina / iPad native
    };

    std::vector<OperatingPointResult> results;

    printf("\n===================================================================================================\n");
    printf("DUWN DIRECT PHASE 6A: QUALITY / LATENCY SYNTHETIC MODEL BENCHMARK\n");
    printf("===================================================================================================\n");
    printf("%-10s %-8s %-10s %-10s %-9s %-9s %-8s %-7s %-12s\n",
           "Resolution", "Bitrate", "Enc P50/95", "Transport", "Decode", "T E2E", "PSNR", "SSIM", "Readability");
    printf("---------------------------------------------------------------------------------------------------\n");

    for (const auto& [w, h] : kResolutions) {
        for (uint32_t br : kBitrates) {
            auto r = EvaluateOperatingPoint(w, h, 60, br);
            results.push_back(r);

            char res_str[16];
            snprintf(res_str, sizeof(res_str), "%ux%u", w, h);
            char br_str[16];
            snprintf(br_str, sizeof(br_str), "%.1fM", br / 1'000'000.0);
            char enc_str[16];
            snprintf(enc_str, sizeof(enc_str), "%.1f/%.1f", r.encode_p50_ms, r.encode_p95_ms);

            printf("%-10s %-8s %-10s %-7.2fms %-7.2fms %-7.2fms %-6.1fdB %-6.3f %s\n",
                   res_str, br_str, enc_str, r.transport_ms, r.decode_ms,
                   r.latency_e2e_ms, r.psnr_db, r.ssim,
                   r.small_text_readable ? "PASS (Sharp)" : "FAIL (Blur)");
        }
        printf("---------------------------------------------------------------------------------------------------\n");
    }

    // 1. Identify Quality Floor (Lowest Acceptable Point) for 1080p
    // Below 6 Mbps at 1080p, text readability fails (SSIM < 0.88, PSNR < 31 dB)
    auto it_1080_floor = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 1920 && r.bitrate_bps == 6'000'000;
    });
    DUWN_ASSERT(it_1080_floor != results.end());
    DUWN_ASSERT(it_1080_floor->small_text_readable == true);
    DUWN_ASSERT(it_1080_floor->psnr_db >= 31.0);
    DUWN_ASSERT(it_1080_floor->ssim >= 0.88);

    // Below floor (4 Mbps) text readability degrades
    auto it_1080_subfloor = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 1920 && r.bitrate_bps == 4'000'000;
    });
    DUWN_ASSERT(it_1080_subfloor != results.end());
    DUWN_ASSERT(it_1080_subfloor->small_text_readable == false); // Subfloor fails quality threshold

    // 2. Identify Best Balanced Point for 1080p (12 Mbps)
    auto it_1080_balanced = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 1920 && r.bitrate_bps == 12'000'000;
    });
    DUWN_ASSERT(it_1080_balanced != results.end());
    DUWN_ASSERT(it_1080_balanced->psnr_db >= 38.0);
    DUWN_ASSERT(it_1080_balanced->ssim >= 0.95);
    DUWN_ASSERT(it_1080_balanced->latency_e2e_ms <= 15.0); // Within low-latency envelope

    // 3. Identify Highest Quality Low-Latency Point for 1080p (20 Mbps)
    auto it_1080_high = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 1920 && r.bitrate_bps == 20'000'000;
    });
    DUWN_ASSERT(it_1080_high != results.end());
    DUWN_ASSERT(it_1080_high->psnr_db >= 42.0);
    DUWN_ASSERT(it_1080_high->ssim >= 0.98);
    DUWN_ASSERT(it_1080_high->latency_e2e_ms <= 16.0);

    // 4. Identify Operating Points for 2560x1440 (2.5K)
    // 1440p has 1.77x more pixels than 1080p.
    // Empirical floor: 8 Mbps is sub-floor (bpp = 0.036, fine text blurs).
    // Quality floor is 12 Mbps, Balanced is 16 Mbps, High is 25 Mbps.
    auto it_1440_subfloor = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 2560 && r.bitrate_bps == 8'000'000;
    });
    DUWN_ASSERT(it_1440_subfloor != results.end());
    DUWN_ASSERT(it_1440_subfloor->small_text_readable == false);

    auto it_1440_floor = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 2560 && r.bitrate_bps == 12'000'000;
    });
    DUWN_ASSERT(it_1440_floor != results.end());
    DUWN_ASSERT(it_1440_floor->small_text_readable == true);
    DUWN_ASSERT(it_1440_floor->psnr_db >= 40.0);
    DUWN_ASSERT(it_1440_floor->ssim >= 0.98);

    auto it_1440_balanced = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 2560 && r.bitrate_bps == 16'000'000;
    });
    DUWN_ASSERT(it_1440_balanced != results.end());
    DUWN_ASSERT(it_1440_balanced->psnr_db >= 45.0);
    DUWN_ASSERT(it_1440_balanced->latency_e2e_ms <= 16.5);

    auto it_1440_high = std::find_if(results.begin(), results.end(), [](const OperatingPointResult& r) {
        return r.width == 2560 && r.bitrate_bps == 25'000'000;
    });
    DUWN_ASSERT(it_1440_high != results.end());
    DUWN_ASSERT(it_1440_high->psnr_db >= 48.0);
    DUWN_ASSERT(it_1440_high->ssim >= 0.99);
}
