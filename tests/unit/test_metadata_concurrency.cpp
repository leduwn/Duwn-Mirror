#include "app/SessionMetadataCoordinator.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace duwn::app;
using namespace duwn::airplay;

DUWN_TEST(Metadata_SingleThreadTransitions) {
    Settings s;
    s.receiver_quality = ReceiverQuality::Auto;
    s.streaming_mode = StreamingMode::SmoothLive;

    SessionMetadataCoordinator coord;
    coord.Initialize(s);

    DUWN_ASSERT(!coord.IsQualityPending());
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::Auto);
    DUWN_ASSERT(coord.GetConfigGeneration() == 1);
    DUWN_ASSERT(coord.GetSidecarGeneration() == 1);

    // UI changes quality to 720p60
    s.receiver_quality = ReceiverQuality::P720_60;
    coord.UpdateRequestedSettings(s, true);

    DUWN_ASSERT(coord.GetConfigGeneration() == 2);
    DUWN_ASSERT(coord.IsReceiverConfigDirty());
    DUWN_ASSERT(coord.IsQualityPending());
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::Auto); // Still previous quality

    // Background worker delivers a stale event from generation 1
    coord.PostPhaseEvent(SessionPhase::Connecting, SessionPhase::Streaming, 1);
    bool applied = false;
    coord.ProcessPendingEvents([&](const SessionPhaseEvent&, bool q_applied) {
        if (q_applied) applied = true;
    });

    DUWN_ASSERT(!applied);
    DUWN_ASSERT(coord.IsQualityPending());
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::Auto);

    // UI restarts sidecar with generation 2
    coord.MarkSidecarRestarted(2, ReceiverQuality::P720_60);
    DUWN_ASSERT(!coord.IsQualityPending());
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::P720_60);

    // Background worker delivers event matching generation 2
    coord.PostPhaseEvent(SessionPhase::Advertising, SessionPhase::Streaming, 2);
    applied = false;
    coord.ProcessPendingEvents([&](const SessionPhaseEvent& ev, bool q_applied) {
        DUWN_ASSERT(ev.generation == 2);
        if (q_applied) applied = true;
    });

    DUWN_ASSERT(applied);
    DUWN_ASSERT(!coord.IsQualityPending());
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::P720_60);
}

DUWN_TEST(Metadata_StaleEventRejection) {
    Settings s;
    s.receiver_quality = ReceiverQuality::P1440_60;
    SessionMetadataCoordinator coord;
    coord.Initialize(s);
    coord.MarkSidecarRestarted(5, ReceiverQuality::P1440_60);

    DUWN_ASSERT(coord.GetSidecarGeneration() == 5);

    // Stale events with generation < 5 should be ignored
    coord.PostPhaseEvent(SessionPhase::Connecting, SessionPhase::Streaming, 3);
    coord.PostPhaseEvent(SessionPhase::Streaming, SessionPhase::Advertising, 4);

    size_t processed_count = 0;
    coord.ProcessPendingEvents([&](const SessionPhaseEvent&, bool) {
        processed_count++;
    });

    // Both stale events should have been dropped
    DUWN_ASSERT(processed_count == 0);
    DUWN_ASSERT(coord.GetSidecarGeneration() == 5);
    DUWN_ASSERT(coord.GetActiveReceiverQuality() == ReceiverQuality::P1440_60);
}

DUWN_TEST(Metadata_ConcurrentConfigAndBackgroundCallbacks) {
    Settings s;
    s.receiver_quality = ReceiverQuality::Auto;
    s.streaming_mode = StreamingMode::LowLatency;
    s.output_width = 1920;
    s.output_height = 1080;

    SessionMetadataCoordinator coord;
    coord.Initialize(s);

    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> metrics_read_count{0};
    std::atomic<uint64_t> events_posted_count{0};

    // Thread 1: Config-owning UI thread (updates settings and processes events)
    std::thread ui_thread([&]() {
        ReceiverQuality qualities[] = {
            ReceiverQuality::Auto,
            ReceiverQuality::P720_30,
            ReceiverQuality::P720_60,
            ReceiverQuality::P1080_30,
            ReceiverQuality::P1080_60,
            ReceiverQuality::P1440_60
        };

        for (int i = 0; i < 300 && !stop_flag.load(std::memory_order_relaxed); ++i) {
            s.receiver_quality = qualities[i % 6];
            coord.UpdateRequestedSettings(s, true);

            // Periodically simulate sidecar restart matching generation
            if (i % 3 == 0) {
                uint64_t current_cfg = coord.GetConfigGeneration();
                coord.MarkSidecarRestarted(current_cfg, s.receiver_quality);
            }

            coord.ProcessPendingEvents([](const SessionPhaseEvent&, bool) {});
            coord.PublishSnapshot("LocalRtpUdp", StreamingPolicy{});
            std::this_thread::yield();
        }
    });

    // Thread 2: Background AirPlay worker (posts session phase events with mixed generations)
    std::thread worker1([&]() {
        while (!stop_flag.load(std::memory_order_relaxed)) {
            uint64_t cfg_gen = coord.GetConfigGeneration();
            uint64_t ev_gen = (cfg_gen > 1 && (events_posted_count % 3 == 0)) ? cfg_gen - 1 : cfg_gen;
            coord.PostPhaseEvent(SessionPhase::Connecting, SessionPhase::Streaming, ev_gen);
            events_posted_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 3: Second worker simulating disconnects
    std::thread worker2([&]() {
        while (!stop_flag.load(std::memory_order_relaxed)) {
            uint64_t cfg_gen = coord.GetConfigGeneration();
            coord.PostPhaseEvent(SessionPhase::Streaming, SessionPhase::Reconnecting, cfg_gen);
            events_posted_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 4: Metrics loop thread (constantly takes snapshots and verifies invariants)
    std::thread metrics_thread([&]() {
        while (!stop_flag.load(std::memory_order_relaxed)) {
            SessionMetadataSnapshot snap = coord.GetSnapshot();

            // Invariant check: if quality is not pending, active must equal requested
            if (!snap.receiver_quality_pending) {
                DUWN_ASSERT(snap.active_receiver_quality == snap.req_receiver_quality);
            }

            DUWN_ASSERT(snap.req_receiver_width > 0);
            DUWN_ASSERT(snap.req_receiver_height > 0);
            metrics_read_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Run concurrency test for 500 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop_flag.store(true, std::memory_order_release);

    ui_thread.join();
    worker1.join();
    worker2.join();
    metrics_thread.join();

    DUWN_ASSERT(metrics_read_count.load() > 50);
    DUWN_ASSERT(events_posted_count.load() > 50);

    // Final drain on UI thread
    coord.ProcessPendingEvents([](const SessionPhaseEvent&, bool) {});
    coord.PublishSnapshot("LocalRtpUdp", StreamingPolicy{});
    SessionMetadataSnapshot final_snap = coord.GetSnapshot();
    if (!final_snap.receiver_quality_pending) {
        DUWN_ASSERT(final_snap.active_receiver_quality == final_snap.req_receiver_quality);
    }
}