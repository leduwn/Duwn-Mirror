#pragma once
// App — top-level application coordinator.
// Owns all subsystems, wires callbacks, manages lifecycle.

#include "Settings.h"
#include "MainWindow.h"
#include "OutputWindow.h"
#include "PreviewWindow.h"
#include "airplay/AirPlayEngine.h"
#include "video/D3D11Device.h"
#include "video/VideoDecoder.h"
#include "video/FrameScheduler.h"
#include "video/VideoRenderer.h"
#include "video/WarpVideoRenderer.h"
#include "video/PipelineTier.h"
#include "video/EncodedAccessUnit.h"
#include "video/SourceQualityTracker.h"
#include "common/ipc/VideoIpcRing.h"
#include "audio/AudioEngine.h"
#include "audio/AudioRingBuffer.h"
#include "audio/WasapiOutput.h"
#include "audio/AudioDeviceManager.h"
#include "network/NetworkEnvironment.h"
#include "network/NetworkChangeMonitor.h"
#include "network/BleBeaconPublisher.h"
#include "wired/WiredDeviceManager.h"
#include "wired/WiredControlClient.h"
#include "sync/MasterClock.h"
#include "sync/AvSynchronizer.h"
#include "sync/DriftController.h"
#include "SessionMetadataCoordinator.h"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <windows.h>

namespace duwn::app {

// Posted from any thread to the main-window HWND to trigger clean shutdown.
// PostQuitMessage() must only be called from the main thread (the thread that
// runs the message loop). Render/audio threads post this message instead.
// wParam = exit code.
// Distinct from OutputWindow's WM_APP_UPDATE_ASPECT to avoid thread queue message collisions.
constexpr UINT WM_DUWN_FATAL = WM_APP + 100;
constexpr UINT WM_DUWN_RESTART_AIRPLAY = WM_APP + 101;
constexpr UINT WM_DUWN_WIRED_REFRESH = WM_APP + 102;
constexpr UINT WM_DUWN_NETWORK_CHANGED = WM_APP + 103;
constexpr UINT WM_DUWN_SESSION_PHASE = WM_APP + 104;

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run() noexcept;

    bool WaitForMediaReadiness(uint32_t timeout_ms = 2500) const noexcept;
    bool IsVideoMinReady() const noexcept {
        return m_video_min_ready.load(std::memory_order_acquire);
    }
    bool IsAudioMinReady() const noexcept {
        return m_audio_min_ready.load(std::memory_order_acquire);
    }
    bool IsNonCriticalReady() const noexcept {
        return m_noncritical_ready.load(std::memory_order_acquire);
    }
    bool IsMediaInfrastructureReady() const noexcept {
        return m_media_infrastructure_ready.load(std::memory_order_acquire);
    }
    network::FirewallValidationState GetFirewallValidationState() const noexcept {
        return m_firewall_validation_state.load(std::memory_order_acquire);
    }
    void SetMediaInfrastructureReady(bool ready) noexcept {
        m_media_infrastructure_ready.store(ready, std::memory_order_release);
    }

private:
    bool Init() noexcept;
    void Shutdown() noexcept;
    void ApplySettingChange(int control_id, int value) noexcept;
    void RestartAirPlaySidecar() noexcept;
    void SyncUiVideoSettings() noexcept;
    bool RecreateVideoPipeline() noexcept;
    void SwitchConnectionMode(ConnectionMode mode) noexcept;
    void RefreshWiredDevices() noexcept;
    void HandleWiredControlAction(int action_id) noexcept;
    void StartWiredControl() noexcept;
    void StopWiredControl() noexcept;
    video::AspectRatioMode GetEffectiveAspectRatioMode() const noexcept;
    video::OutputDimensions ComputeCurrentOutputDimensions(
        uint32_t src_w, uint32_t src_h,
        const SessionMetadataSnapshot* snap = nullptr) const noexcept;

    // AirPlay callbacks (called on AirPlay engine threads)
    void OnVideoData(const uint8_t* data, size_t size,
                     uint32_t rtp_ts, int64_t arrival_ns,
                     bool marker, uint16_t seq) noexcept;
    void OnAudioData(const uint8_t* data, size_t size,
                     uint32_t rtp_ts, int64_t arrival_ns) noexcept;
    void OnPhase(airplay::SessionPhase prev, airplay::SessionPhase next) noexcept;
    void OnMetadata(const airplay::StreamMetadata& meta) noexcept;

    // Frame present callback (from FrameScheduler, render thread).
    // Must NOT call PostQuitMessage() — use PostMessageW(m_main_hwnd, WM_DUWN_FATAL).
    void OnFramePresent(video::VideoFrame& frame) noexcept;

    // Metrics and session metadata
    void MetricsLoop(std::stop_token stop) noexcept;
    void PublishMetadataSnapshot() noexcept;
    SessionMetadataSnapshot GetMetadataSnapshot() const noexcept;
    void ProcessPendingSessionEvents() noexcept;
    void HandleSessionPhaseOnMainThread(const SessionPhaseEvent& ev, bool quality_applied) noexcept;
    std::string GetActiveTransportString() const noexcept;
    StreamingPolicy GetActiveStreamingPolicy() const noexcept;
    void UpdateWiredConnection() noexcept;
    void OnNetworkEnvironmentChanged(const network::NetworkEnvironmentInfo& new_env) noexcept;

    // Hardware capability and performance tier reporting
    video::PipelineTier DeterminePipelineTier() const noexcept;
    void LogCapabilityReport() const noexcept;

    // Signal fatal error from any thread. Thread-safe: PostMessageW is thread-safe.
    // Main message loop handles WM_DUWN_FATAL: sets running=false, PostQuitMessage.
    // render/audio threads must NOT call PostQuitMessage or set m_running directly.
    void PostFatalShutdown(int exit_code) noexcept {
        HWND hwnd = m_main_hwnd.load(std::memory_order_acquire);
        if (!hwnd) return; // HWND not yet created or already destroyed
        if (!::PostMessageW(hwnd, WM_DUWN_FATAL,
                            static_cast<WPARAM>(exit_code), 0)) {
            // PostMessageW fails if message queue is full or HWND is invalid.
            // Nothing further can be done from a background thread.
        }
    }

    Settings  m_settings;

    std::unique_ptr<MainWindow>      m_window;        // Control Window (title bar, status)
    std::unique_ptr<OutputWindow>    m_output_window; // Borderless video output — OBS target
    std::unique_ptr<PreviewWindow>   m_preview_window;// User-facing preview viewer window
    std::unique_ptr<video::D3D11Device>   m_d3d;
    std::unique_ptr<video::VideoDecoder>  m_video_decoder;
    std::unique_ptr<video::FrameScheduler> m_scheduler;
    std::unique_ptr<video::IVideoRenderer> m_renderer;
    std::unique_ptr<video::IVideoRenderer> m_preview_renderer;
    std::unique_ptr<audio::AudioRingBuffer> m_audio_ring;
    std::unique_ptr<audio::AudioEngine>   m_audio_engine;
    std::unique_ptr<audio::WasapiOutput>  m_wasapi;
    audio::AudioDeviceManager             m_audio_device_mgr;
    std::unique_ptr<airplay::AirPlayEngine> m_airplay;
    wired::WiredDeviceManager m_wired_devices;
    wired::WiredControlClient m_wired_control;
    std::wstring m_wired_bind_ipv4;
    bool m_wired_control_requested{false};
    unsigned m_wired_network_wait_ticks{0};
    std::atomic_bool m_wired_reconnect_hint{false};

    sync::AvSynchronizer   m_av_sync;
    sync::DriftController  m_drift;
    network::NetworkEnvironmentInfo m_net_env;
    std::unique_ptr<network::NetworkChangeMonitor> m_net_monitor;
    std::unique_ptr<network::BleBeaconPublisher>  m_ble_beacon;

    std::jthread m_metrics_thread;
    std::atomic_bool m_running{false};

    // HWND of the main (control) window, set during Init.
    // Accessed from render thread via PostMessageW (thread-safe).
    std::atomic<HWND> m_main_hwnd{nullptr};

    // Current stream dimensions (updated on metadata callback).
    // 0 until first metadata received — VideoRenderer also starts at 0.
    std::atomic<uint32_t> m_stream_width{0};
    std::atomic<uint32_t> m_stream_height{0};
    std::atomic_bool       m_decoder_ready{false};
    std::atomic_bool       m_match_source{false};
    std::atomic<ConnectionMode> m_connection_mode{ConnectionMode::WirelessAirPlay};
    std::atomic<int>       m_active_renderer{0}; // 0 unavailable, 1 hardware D3D11, 2 WARP
    std::atomic<uint32_t>  m_active_filter_caps{0};

    // Direct IPC Video Consumer
    std::unique_ptr<ipc::VideoIpcConsumer> m_ipc_consumer;

    // AirPlay Process / Sidecar Generation and Metadata Synchronization
    SessionMetadataCoordinator m_meta_coord;
    std::atomic<bool>      m_sidecar_restart_posted{false};

    // Output window aspect stabilization state
    uint64_t               m_last_aspect_generation{0};
    uint32_t               m_last_aspect_w{0};
    uint32_t               m_last_aspect_h{0};
    uint32_t               m_pending_aspect_w{0};
    uint32_t               m_pending_aspect_h{0};
    uint32_t               m_pending_aspect_count{0};

    // Preview presentation state
    std::atomic<bool>      m_preview_shown{false};
    uint32_t               m_last_preview_src_w{0};
    uint32_t               m_last_preview_src_h{0};
    std::atomic<uint64_t>  m_render_generation{0};
    std::mutex             m_decoder_mutex;

    // Startup firewall reconciliation state
    bool                   m_startup_public_rules_missing{false};
    bool                   m_startup_public_rules_unexpected{false};

    // Source quality and capability tracker
    video::SourceQualityTracker m_source_quality_tracker;

    // Media readiness decomposition (C2)
    std::atomic<bool>      m_video_min_ready{false};
    std::atomic<bool>      m_audio_min_ready{false};
    std::atomic<bool>      m_noncritical_ready{false};
    std::atomic<bool>      m_media_infrastructure_ready{false};

    // Firewall validation state machine
    std::atomic<network::FirewallValidationState> m_firewall_validation_state{network::FirewallValidationState::Unknown};
};

} // namespace duwn::app
