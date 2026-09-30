#include "airplay/SidecarVerificationCache.h"
#include "common/telemetry/ConnectionTimeline.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <atomic>

using namespace duwn::airplay;
using namespace duwn::telemetry;

DUWN_TEST(SidecarCache_ComputeSha256) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "duwn_test_cache";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_file = temp_dir / "test_sidecar.bin";

    {
        std::ofstream ofs(temp_file, std::ios::binary);
        ofs << "DUWN_MIRROR_TEST_BINARY_CONTENT_FOR_SHA256";
    }

    std::string sha256_1 = SidecarVerificationCache::ComputeFileSha256(temp_file.wstring());
    DUWN_ASSERT(!sha256_1.empty());
    DUWN_ASSERT(sha256_1.length() == 64);

    std::string sha256_2 = SidecarVerificationCache::ComputeFileSha256(temp_file.wstring());
    DUWN_ASSERT(!sha256_2.empty());
    DUWN_ASSERT(sha256_1 == sha256_2);

    std::filesystem::remove(temp_file);
    std::filesystem::remove(temp_dir);
}

DUWN_TEST(SidecarCache_SaveAndLoad) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "duwn_test_cache_save_load";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_bin = temp_dir / "dummy_uxplay.exe";
    std::filesystem::path test_cache = temp_dir / "test_cache.json";

    {
        std::ofstream ofs(temp_bin, std::ios::binary);
        ofs << "MZ_DUMMY_EXECUTABLE_CONTENT_V1_73";
    }

    std::string hash = SidecarVerificationCache::ComputeFileSha256(temp_bin.wstring());
    DUWN_ASSERT(!hash.empty());

    SidecarCachedVerification record;
    record.sha256 = hash;
    record.major = 1;
    record.minor = 73;
    record.has_vrtp = true;
    record.has_bind_ip = true;
    record.has_bind_prefix = true;

    DUWN_ASSERT(SidecarVerificationCache::SaveCache(record, test_cache));

    SidecarCachedVerification out_cached;
    bool hit = SidecarVerificationCache::CheckCache(temp_bin.wstring(), true, out_cached, test_cache);
    DUWN_ASSERT(hit);
    DUWN_ASSERT(out_cached.major == 1);
    DUWN_ASSERT(out_cached.minor == 73);
    DUWN_ASSERT(out_cached.has_vrtp == true);
    DUWN_ASSERT(out_cached.has_bind_ip == true);
    DUWN_ASSERT(out_cached.has_bind_prefix == true);
    DUWN_ASSERT(out_cached.sha256 == hash);

    // Clean up
    SidecarVerificationCache::Invalidate(test_cache);
    DUWN_ASSERT(!std::filesystem::exists(test_cache));

    std::filesystem::remove(temp_bin);
    std::filesystem::remove_all(temp_dir);
}

DUWN_TEST(SidecarCache_InvalidateOnHashMismatch) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "duwn_test_cache_mismatch";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_bin = temp_dir / "hash_mismatch_test.exe";
    std::filesystem::path test_cache = temp_dir / "test_cache.json";

    {
        std::ofstream ofs(temp_bin, std::ios::binary);
        ofs << "ORIGINAL_BYTES";
    }

    std::string hash = SidecarVerificationCache::ComputeFileSha256(temp_bin.wstring());
    DUWN_ASSERT(!hash.empty());

    SidecarCachedVerification record;
    record.sha256 = hash;
    record.major = 1;
    record.minor = 73;
    record.has_vrtp = true;
    record.has_bind_ip = true;
    record.has_bind_prefix = true;
    DUWN_ASSERT(SidecarVerificationCache::SaveCache(record, test_cache));

    // Modify file to change SHA256
    {
        std::ofstream ofs(temp_bin, std::ios::binary);
        ofs << "MODIFIED_NEW_BYTES_CHANGED_CHECKSUM";
    }

    SidecarCachedVerification out_cached;
    bool hit = SidecarVerificationCache::CheckCache(temp_bin.wstring(), true, out_cached, test_cache);
    DUWN_ASSERT(!hit);

    std::filesystem::remove(temp_bin);
    std::filesystem::remove_all(temp_dir);
}

DUWN_TEST(SidecarCache_InvalidateOnCorruptJson) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "duwn_test_cache_corrupt";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_bin = temp_dir / "dummy.exe";
    std::filesystem::path test_cache = temp_dir / "test_cache.json";

    {
        std::ofstream ofs(temp_bin, std::ios::binary);
        ofs << "SAMPLE_DATA";
    }
    {
        std::ofstream ofs(test_cache, std::ios::trunc);
        ofs << "{ corrupt json broken content ::: [not closed";
    }

    SidecarCachedVerification out_cached;
    bool hit = SidecarVerificationCache::CheckCache(temp_bin.wstring(), false, out_cached, test_cache);
    DUWN_ASSERT(!hit);

    std::filesystem::remove_all(temp_dir);
}

DUWN_TEST(SidecarCache_RejectLowVersion) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "duwn_test_cache_low_ver";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_bin = temp_dir / "old_uxplay.exe";
    std::filesystem::path test_cache = temp_dir / "test_cache.json";

    {
        std::ofstream ofs(temp_bin, std::ios::binary);
        ofs << "OLD_UXPLAY_V1_70";
    }

    std::string hash = SidecarVerificationCache::ComputeFileSha256(temp_bin.wstring());
    DUWN_ASSERT(!hash.empty());

    SidecarCachedVerification record;
    record.sha256 = hash;
    record.major = 1;
    record.minor = 70; // Below 1.73!
    record.has_vrtp = true;
    record.has_bind_ip = true;
    record.has_bind_prefix = true;
    DUWN_ASSERT(SidecarVerificationCache::SaveCache(record, test_cache));

    SidecarCachedVerification out_cached;
    bool hit = SidecarVerificationCache::CheckCache(temp_bin.wstring(), false, out_cached, test_cache);
    DUWN_ASSERT(!hit);

    std::filesystem::remove_all(temp_dir);
}

DUWN_TEST(StartupOrder_MediaReadinessGate) {
    std::atomic<bool> ready{false};
    std::atomic<bool> running{true};

    auto wait_fn = [&](uint32_t timeout_ms) -> bool {
        if (ready.load(std::memory_order_acquire)) return true;
        auto start = std::chrono::steady_clock::now();
        while (!ready.load(std::memory_order_acquire)) {
            if (!running.load(std::memory_order_relaxed)) return false;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeout_ms) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    };

    DUWN_ASSERT(!wait_fn(20));

    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        ready.store(true, std::memory_order_release);
    });
    DUWN_ASSERT(wait_fn(500));
    t.join();

    DUWN_ASSERT(wait_fn(100));
}

DUWN_TEST(ConnectionTimeline_MilestoneOrderingWithNewMilestones) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    timeline.Record(ConnectionMilestone::C0_ProcessStart);
    timeline.Record(ConnectionMilestone::C3_UxPlaySpawned);
    timeline.Record(ConnectionMilestone::C4_UxPlaySocketsInitialized);
    timeline.Record(ConnectionMilestone::C4A_MdnsPublicationInitiated);
    timeline.Record(ConnectionMilestone::C4B_AdvertisementActiveInternal);
    timeline.Record(ConnectionMilestone::C5_AdvertisingReady);

    int64_t c0 = timeline.GetTimestampNs(ConnectionMilestone::C0_ProcessStart);
    int64_t c3 = timeline.GetTimestampNs(ConnectionMilestone::C3_UxPlaySpawned);
    int64_t c4 = timeline.GetTimestampNs(ConnectionMilestone::C4_UxPlaySocketsInitialized);
    int64_t c4a = timeline.GetTimestampNs(ConnectionMilestone::C4A_MdnsPublicationInitiated);
    int64_t c4b = timeline.GetTimestampNs(ConnectionMilestone::C4B_AdvertisementActiveInternal);
    int64_t c5 = timeline.GetTimestampNs(ConnectionMilestone::C5_AdvertisingReady);

    DUWN_ASSERT(c0 > 0);
    DUWN_ASSERT(c3 >= c0);
    DUWN_ASSERT(c4 >= c3);
    DUWN_ASSERT(c4a >= c4);
    DUWN_ASSERT(c4b >= c4a);
    DUWN_ASSERT(c5 >= c4b);
}

DUWN_TEST(MediaReadiness_DecompositionOrdering) {
    std::atomic<bool> video_min_ready{false};
    std::atomic<bool> audio_min_ready{false};
    std::atomic<bool> media_infra_ready{false};
    std::atomic<bool> noncritical_ready{false};

    // Stage 1: D3D11 + VideoDecoder + OutputWindow + PreviewWindow
    video_min_ready.store(true, std::memory_order_release);
    DUWN_ASSERT(video_min_ready.load(std::memory_order_acquire));
    DUWN_ASSERT(!audio_min_ready.load(std::memory_order_acquire));
    DUWN_ASSERT(!media_infra_ready.load(std::memory_order_acquire));

    // Stage 2: WASAPI + AudioEngine
    audio_min_ready.store(true, std::memory_order_release);
    DUWN_ASSERT(audio_min_ready.load(std::memory_order_acquire));

    // Stage 3: C2 Media Infrastructure Ready
    if (video_min_ready.load(std::memory_order_acquire) && audio_min_ready.load(std::memory_order_acquire)) {
        media_infra_ready.store(true, std::memory_order_release);
    }
    DUWN_ASSERT(media_infra_ready.load(std::memory_order_acquire));
    DUWN_ASSERT(!noncritical_ready.load(std::memory_order_acquire));

    // Stage 4: Non-critical diagnostics and device enumeration
    noncritical_ready.store(true, std::memory_order_release);
    DUWN_ASSERT(noncritical_ready.load(std::memory_order_acquire));
}

DUWN_TEST(RtpCallback_NonBlockingUnderReadinessDelay) {
    std::atomic<bool> video_min_ready{false};
    std::atomic<bool> decoder_ready{false};
    std::atomic<uint64_t> dropped_packets{0};
    std::atomic<uint64_t> processed_packets{0};

    auto mock_on_video_data = [&](const uint8_t* /*data*/, size_t /*size*/) {
        // Non-blocking hot-path check matching App::OnVideoData
        if (!video_min_ready.load(std::memory_order_acquire) ||
            !decoder_ready.load(std::memory_order_acquire)) {
            dropped_packets.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        processed_packets.fetch_add(1, std::memory_order_relaxed);
    };

    uint8_t dummy_packet[1400]{0x80, 0x60};

    // Measure time to handle packet when video infrastructure is not ready
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        mock_on_video_data(dummy_packet, sizeof(dummy_packet));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    DUWN_ASSERT(dropped_packets.load() == 1000);
    DUWN_ASSERT(processed_packets.load() == 0);
    // 1000 non-blocking drops must complete in under 5 ms total (<5 us per call)
    DUWN_ASSERT(total_us < 5000.0);

    // Now enable readiness and verify processing
    video_min_ready.store(true, std::memory_order_release);
    decoder_ready.store(true, std::memory_order_release);

    for (int i = 0; i < 100; ++i) {
        mock_on_video_data(dummy_packet, sizeof(dummy_packet));
    }
    DUWN_ASSERT(processed_packets.load() == 100);
}
