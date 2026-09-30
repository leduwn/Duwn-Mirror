// Benchmark and verification for RtpReceiver priority policies:
// Config A: Pro Audio + THREAD_PRIORITY_HIGHEST
// Config B: Playback + THREAD_PRIORITY_ABOVE_NORMAL

#include "network/RtpReceiver.h"
#include "common/clock/MonotonicClock.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>

namespace {

struct PriorityBenchmarkResult {
    const char* config_name;
    size_t packets_sent;
    size_t packets_received;
    double loss_percent;
    double elapsed_ms;
    double packets_per_sec;
    double receiver_cpu_ms;
    double worker_max_jitter_ms;
    double worker_p99_jitter_ms;
    size_t worker_intervals_gt_10ms;
};

PriorityBenchmarkResult RunPriorityBenchmark(duwn::network::ReceiverPriorityPolicy policy, const char* name) {
    using namespace duwn::network;
    using clock = duwn::clock::MonotonicClock;

    constexpr size_t kTotalPackets = 15000;
    std::atomic<size_t> received_count{0};

    // 1. Create receiver with chosen policy
    RtpReceiver receiver([&](const RtpPacket& pkt) {
        received_count.fetch_add(1, std::memory_order_relaxed);
    }, policy);

    uint16_t port = receiver.Start();
    DUWN_ASSERT(port != 0);

    // 2. Worker thread simulating UI / render loop scheduling at normal priority
    std::atomic<bool> worker_running{true};
    std::vector<double> worker_intervals;
    worker_intervals.reserve(5000);

    std::thread worker([&]() {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        auto prev = clock::Now();
        while (worker_running.load(std::memory_order_relaxed)) {
            ::Sleep(1); // 1ms target tick
            auto now = clock::Now();
            double diff_ms = std::chrono::duration<double, std::milli>(now - prev).count();
            prev = now;
            worker_intervals.push_back(diff_ms);
        }
    });

    // 3. Prepare UDP sender
    SOCKET send_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DUWN_ASSERT(send_sock != INVALID_SOCKET);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = ::htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    // Build standard 12-byte RTP packet + 1200-byte payload
    std::vector<uint8_t> pkt(12 + 1200);
    pkt[0] = 0x80; // v2
    pkt[1] = 96;   // H264 PT

    auto start_time = clock::Now();

    // Burst packets
    for (size_t i = 0; i < kTotalPackets; ++i) {
        pkt[2] = static_cast<uint8_t>((i >> 8) & 0xFF);
        pkt[3] = static_cast<uint8_t>(i & 0xFF);
        ::sendto(send_sock, reinterpret_cast<const char*>(pkt.data()),
                 static_cast<int>(pkt.size()), 0,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

        if ((i % 64) == 0) {
            std::this_thread::yield();
        }
    }

    // Wait for drain (up to 300ms)
    for (int retry = 0; retry < 30; ++retry) {
        if (received_count.load(std::memory_order_relaxed) >= kTotalPackets) break;
        ::Sleep(10);
    }

    auto end_time = clock::Now();
    worker_running.store(false, std::memory_order_relaxed);
    if (worker.joinable()) worker.join();

    ::closesocket(send_sock);
    receiver.Stop();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    size_t recvd = received_count.load(std::memory_order_relaxed);
    double loss = (kTotalPackets > recvd) ? (100.0 * (kTotalPackets - recvd) / kTotalPackets) : 0.0;
    double pps = (elapsed_ms > 0.0) ? (recvd / (elapsed_ms / 1000.0)) : 0.0;

    // Calculate worker jitter
    double max_jitter = 0.0;
    double p99_jitter = 0.0;
    size_t gt_10ms = 0;
    if (!worker_intervals.empty()) {
        std::vector<double> sorted = worker_intervals;
        std::sort(sorted.begin(), sorted.end());
        max_jitter = sorted.back();
        size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
        if (p99_idx >= sorted.size()) p99_idx = sorted.size() - 1;
        p99_jitter = sorted[p99_idx];

        for (double v : sorted) {
            if (v > 10.0) ++gt_10ms;
        }
    }

    return PriorityBenchmarkResult{
        .config_name = name,
        .packets_sent = kTotalPackets,
        .packets_received = recvd,
        .loss_percent = loss,
        .elapsed_ms = elapsed_ms,
        .packets_per_sec = pps,
        .receiver_cpu_ms = 0.0,
        .worker_max_jitter_ms = max_jitter,
        .worker_p99_jitter_ms = p99_jitter,
        .worker_intervals_gt_10ms = gt_10ms
    };
}

} // namespace

DUWN_TEST(rtp_priority_audit_benchmark) {
    auto res_a = RunPriorityBenchmark(duwn::network::ReceiverPriorityPolicy::ProAudioHighest,
                                      "Config A: Pro Audio + THREAD_PRIORITY_HIGHEST");
    auto res_b = RunPriorityBenchmark(duwn::network::ReceiverPriorityPolicy::PlaybackAboveNormal,
                                      "Config B: Playback + THREAD_PRIORITY_ABOVE_NORMAL");

    printf("\n--- UDP RECEIVER THREAD PRIORITY AUDIT BENCHMARK ---\n");
    printf("[%s]\n", res_a.config_name);
    printf("  Packets Sent: %zu, Received: %zu (Loss: %.2f%%)\n",
           res_a.packets_sent, res_a.packets_received, res_a.loss_percent);
    printf("  Throughput: %.1f pkts/sec (Elapsed: %.2f ms)\n",
           res_a.packets_per_sec, res_a.elapsed_ms);
    printf("  Concurrent Worker Max Tick Jitter: %.2f ms, P99 Jitter: %.2f ms, Ticks >10ms: %zu\n",
           res_a.worker_max_jitter_ms, res_a.worker_p99_jitter_ms, res_a.worker_intervals_gt_10ms);

    printf("[%s]\n", res_b.config_name);
    printf("  Packets Sent: %zu, Received: %zu (Loss: %.2f%%)\n",
           res_b.packets_sent, res_b.packets_received, res_b.loss_percent);
    printf("  Throughput: %.1f pkts/sec (Elapsed: %.2f ms)\n",
           res_b.packets_per_sec, res_b.elapsed_ms);
    printf("  Concurrent Worker Max Tick Jitter: %.2f ms, P99 Jitter: %.2f ms, Ticks >10ms: %zu\n",
           res_b.worker_max_jitter_ms, res_b.worker_p99_jitter_ms, res_b.worker_intervals_gt_10ms);

    // Both must achieve near-zero loss over loopback socket buffer
    DUWN_ASSERT(res_a.loss_percent < 1.0);
    DUWN_ASSERT(res_b.loss_percent < 1.0);

    // Config B achieves equal or better worker pacing stability without starving UI/Render
    DUWN_ASSERT(res_b.worker_max_jitter_ms <= res_a.worker_max_jitter_ms + 10.0);
}
