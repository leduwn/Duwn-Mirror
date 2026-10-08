#pragma once
// SessionMetadataCoordinator.h — Strict ownership and concurrency control
// for session metadata, requested settings snapshots, and generation tracking.

#include "Settings.h"
#include "airplay/AirPlayEngine.h"
#include <mutex>
#include <vector>
#include <atomic>
#include <string>
#include <functional>

namespace duwn::app {

struct SessionMetadataSnapshot {
    StreamingMode   req_streaming_mode{StreamingMode::SmoothLive};
    uint32_t        req_custom_freshness_ms{25};
    uint32_t        req_custom_queue_frames{2};
    ReceiverQuality req_receiver_quality{ReceiverQuality::Auto};
    uint32_t        req_receiver_width{1920};
    uint32_t        req_receiver_height{1080};
    uint32_t        req_receiver_fps{60};
    TransportMode   req_transport_mode{TransportMode::RtpUdpLegacy};
    std::wstring    monitor_device_id;

    CaptureCanvas   capture_canvas{CaptureCanvas::FollowSource};
    OutputQuality   output_quality{OutputQuality::Auto};
    AspectMode      aspect_mode{AspectMode::Auto};
    uint32_t        output_width{1920};
    uint32_t        output_height{1080};

    StreamingPolicy active_policy{};
    StreamingMode   active_streaming_mode{StreamingMode::SmoothLive};
    ReceiverQuality active_receiver_quality{ReceiverQuality::Auto};
    bool            receiver_quality_pending{false};
    bool            preview_visible{false};
    std::string     active_transport{"LocalRtpUdp"};
};

struct SessionPhaseEvent {
    airplay::SessionPhase prev{airplay::SessionPhase::Idle};
    airplay::SessionPhase next{airplay::SessionPhase::Idle};
    uint64_t generation{0};
};

class SessionMetadataCoordinator {
public:
    SessionMetadataCoordinator() noexcept {
        m_config_generation.store(1, std::memory_order_relaxed);
        m_active_sidecar_generation.store(1, std::memory_order_relaxed);
        m_receiver_config_dirty.store(false, std::memory_order_relaxed);
        m_active_receiver_quality.store(ReceiverQuality::Auto, std::memory_order_relaxed);
    }

    void Initialize(const Settings& s) noexcept {
        std::lock_guard lock(m_settings_mutex);
        m_req_settings = s;
        m_active_receiver_quality.store(s.receiver_quality, std::memory_order_release);
        m_config_generation.store(1, std::memory_order_release);
        m_active_sidecar_generation.store(1, std::memory_order_release);
        m_receiver_config_dirty.store(false, std::memory_order_release);
    }

    void UpdateRequestedSettings(const Settings& s, bool quality_changed) noexcept {
        std::lock_guard lock(m_settings_mutex);
        m_req_settings = s;
        if (quality_changed) {
            m_config_generation.fetch_add(1, std::memory_order_acq_rel);
            m_receiver_config_dirty.store(true, std::memory_order_release);
        }
    }

    void MarkSidecarRestarted(uint64_t gen, ReceiverQuality applied_quality) noexcept {
        m_active_receiver_quality.store(applied_quality, std::memory_order_release);
        m_receiver_config_dirty.store(false, std::memory_order_release);
        m_active_sidecar_generation.store(gen, std::memory_order_release);
    }

    template <typename Callback>
    void ProcessPendingEvents(Callback&& on_event) noexcept {
        std::vector<SessionPhaseEvent> events;
        {
            std::lock_guard lock(m_queue_mutex);
            events.swap(m_pending_events);
        }

        for (const auto& ev : events) {
            const uint64_t current_side_gen = m_active_sidecar_generation.load(std::memory_order_acquire);
            if (ev.generation < current_side_gen && ev.generation != 0) {
                continue;
            }

            if (ev.generation > 0) {
                m_active_sidecar_generation.store(ev.generation, std::memory_order_release);
            }

            const uint64_t cfg_gen = m_config_generation.load(std::memory_order_acquire);
            const bool is_dirty = m_receiver_config_dirty.load(std::memory_order_acquire);

            bool quality_applied = false;
            if (ev.generation == cfg_gen && !is_dirty) {
                ReceiverQuality target_q = ReceiverQuality::Auto;
                {
                    std::lock_guard lock(m_settings_mutex);
                    target_q = m_req_settings.receiver_quality;
                }
                m_active_receiver_quality.store(target_q, std::memory_order_release);
                quality_applied = true;
            }

            on_event(ev, quality_applied);
        }
    }

    void PublishSnapshot(const std::string& active_transport,
                         const StreamingPolicy& active_policy,
                         bool preview_visible = false) noexcept {
        SessionMetadataSnapshot snap;
        {
            std::lock_guard lock(m_settings_mutex);
            snap.req_streaming_mode = m_req_settings.streaming_mode;
            snap.req_custom_freshness_ms = m_req_settings.custom_video_freshness_ms;
            snap.req_custom_queue_frames = m_req_settings.custom_video_queue_frames;
            snap.req_receiver_quality = m_req_settings.receiver_quality;
            snap.req_receiver_width = m_req_settings.receiver_width;
            snap.req_receiver_height = m_req_settings.receiver_height;
            snap.req_receiver_fps = m_req_settings.receiver_fps;
            snap.req_transport_mode = m_req_settings.transport_mode;
            snap.monitor_device_id = m_req_settings.AudioOutputSelectionId();

            snap.capture_canvas = m_req_settings.capture_canvas;
            snap.output_quality = m_req_settings.output_quality;
            snap.aspect_mode = m_req_settings.aspect_mode;
            snap.output_width = m_req_settings.output_width;
            snap.output_height = m_req_settings.output_height;

            snap.active_streaming_mode = m_req_settings.streaming_mode;
        }

        snap.active_transport = active_transport;
        snap.active_policy = active_policy;
        snap.preview_visible = preview_visible;

        const bool is_dirty = m_receiver_config_dirty.load(std::memory_order_acquire);
        const uint64_t cfg_gen = m_config_generation.load(std::memory_order_acquire);
        const uint64_t side_gen = m_active_sidecar_generation.load(std::memory_order_acquire);
        const bool is_pending = is_dirty || (cfg_gen != side_gen);

        snap.receiver_quality_pending = is_pending;
        snap.active_receiver_quality = is_pending
            ? m_active_receiver_quality.load(std::memory_order_acquire)
            : snap.req_receiver_quality;

        std::lock_guard lock(m_snapshot_mutex);
        m_snapshot = std::move(snap);
    }

    void PostPhaseEvent(airplay::SessionPhase prev, airplay::SessionPhase next, uint64_t generation) noexcept {
        std::lock_guard lock(m_queue_mutex);
        m_pending_events.push_back({prev, next, generation});
    }

    SessionMetadataSnapshot GetSnapshot() const noexcept {
        std::lock_guard lock(m_snapshot_mutex);
        return m_snapshot;
    }

    uint64_t GetConfigGeneration() const noexcept {
        return m_config_generation.load(std::memory_order_acquire);
    }

    uint64_t GetSidecarGeneration() const noexcept {
        return m_active_sidecar_generation.load(std::memory_order_acquire);
    }

    bool IsReceiverConfigDirty() const noexcept {
        return m_receiver_config_dirty.load(std::memory_order_acquire);
    }

    ReceiverQuality GetActiveReceiverQuality() const noexcept {
        return m_active_receiver_quality.load(std::memory_order_acquire);
    }

    bool IsQualityPending() const noexcept {
        const bool is_dirty = m_receiver_config_dirty.load(std::memory_order_acquire);
        const uint64_t cfg_gen = m_config_generation.load(std::memory_order_acquire);
        const uint64_t side_gen = m_active_sidecar_generation.load(std::memory_order_acquire);
        return is_dirty || (cfg_gen != side_gen);
    }

private:
    mutable std::mutex       m_settings_mutex;
    Settings                 m_req_settings{};

    std::atomic<uint64_t>    m_config_generation{1};
    std::atomic<uint64_t>    m_active_sidecar_generation{1};
    std::atomic<bool>        m_receiver_config_dirty{false};
    std::atomic<ReceiverQuality> m_active_receiver_quality{ReceiverQuality::Auto};

    mutable std::mutex              m_queue_mutex;
    std::vector<SessionPhaseEvent>  m_pending_events;

    mutable std::mutex       m_snapshot_mutex;
    SessionMetadataSnapshot  m_snapshot;
};

} // namespace duwn::app
