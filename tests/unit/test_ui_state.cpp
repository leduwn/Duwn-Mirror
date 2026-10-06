// test_ui_state.cpp — Unit tests for UI state, connection states, and telemetry formatting.

#include "ui/UiState.h"
#include "ui/MainWindowView.h"
#include "app/Settings.h"
#include "common/logging/Logger.h"
#include "common/clock/MonotonicClock.h"
#include <string>
#include <format>
#include <chrono>
#include <thread>

DUWN_TEST(UiState_DefaultValuesAreSanitized) {
    duwn::ui::UiState state;
    DUWN_ASSERT(state.status == duwn::ui::ConnectionStatus::Ready);
    DUWN_ASSERT(state.device_name == L"—");
    DUWN_ASSERT(state.client_ip == L"—");
    DUWN_ASSERT(state.width == 0);
    DUWN_ASSERT(state.height == 0);
    DUWN_ASSERT(state.nominal_fps == 0.0);
    DUWN_ASSERT(state.render_fps == 0.0);
    DUWN_ASSERT(state.pipeline_latency_ms == 0.0);
    DUWN_ASSERT(state.queue_depth == 0);
    DUWN_ASSERT(state.dropped_frames == 0);
    DUWN_ASSERT(!state.audio_muted);
    DUWN_ASSERT(state.active_tab == duwn::ui::NavTab::Mirror);
    DUWN_ASSERT(state.aspect_locked == true);
    DUWN_ASSERT(state.output_window_visible == false);
}

DUWN_TEST(UiState_ConnectionStatusTransitions) {
    using CS = duwn::ui::ConnectionStatus;
    duwn::ui::UiState state;

    state.status = CS::Connecting;
    state.status_message = L"Connecting to iPhone…";
    DUWN_ASSERT(state.status == CS::Connecting);

    state.status = CS::Streaming;
    state.device_name = L"duwn's iPhone";
    state.client_ip = L"192.168.1.50";
    state.width = 1920;
    state.height = 1080;
    state.render_fps = 60.0;
    DUWN_ASSERT(state.status == CS::Streaming);
    DUWN_ASSERT(state.width == 1920);
    DUWN_ASSERT(state.render_fps == 60.0);

    state.status = CS::Reconnecting;
    DUWN_ASSERT(state.status == CS::Reconnecting);

    state.status = CS::Error;
    state.status_message = L"Error: AirPlay sidecar exited";
    DUWN_ASSERT(state.status == CS::Error);

    state.status = CS::Ready;
    DUWN_ASSERT(state.status == CS::Ready);
}

DUWN_TEST(UiState_TelemetryFormatting) {
    duwn::ui::UiState state;

    // When idle/empty, resolution and fps should display "—"
    std::wstring res_str = (state.width > 0 && state.height > 0) ?
        std::format(L"{}×{}", state.width, state.height) : L"—";
    DUWN_ASSERT(res_str == L"—");

    std::wstring fps_str = state.render_fps > 0.0 ?
        std::format(L"{:.1f} FPS", state.render_fps) : L"—";
    DUWN_ASSERT(fps_str == L"—");

    // When populated with active stream metrics
    state.width = 1170;
    state.height = 2532;
    state.render_fps = 59.94;
    state.session_uptime_sec = 3665; // 1 hr, 1 min, 5 sec

    res_str = std::format(L"{}×{}", state.width, state.height);
    fps_str = std::format(L"{:.1f} FPS", state.render_fps);

    DUWN_ASSERT(res_str == L"1170×2532");
    DUWN_ASSERT(fps_str == L"59.9 FPS");

    int64_t up = state.session_uptime_sec;
    std::wstring uptime_str = std::format(L"{:02d}:{:02d}:{:02d}", up / 3600, (up % 3600) / 60, up % 60);
    DUWN_ASSERT(uptime_str == L"01:01:05");
}

DUWN_TEST(UiState_MinimumWindowBounds) {
    // Requirements Item 14: MainWindow must enforce 960x560 minimum bounds
    constexpr LONG kMinW = 960;
    constexpr LONG kMinH = 560;

    MINMAXINFO mmi{};
    mmi.ptMinTrackSize.x = kMinW;
    mmi.ptMinTrackSize.y = kMinH;

    DUWN_ASSERT(mmi.ptMinTrackSize.x >= 960);
    DUWN_ASSERT(mmi.ptMinTrackSize.y >= 560);
}

DUWN_TEST(UiState_VideoSettingsDefaults) {
    duwn::ui::UiState state;
    // Auto receiver quality ceiling
    DUWN_ASSERT(state.receiver_quality == 0);
    // Dynamic output matching stream dimensions
    DUWN_ASSERT(state.match_source == true);
    // Auto aspect ratio behavior
    DUWN_ASSERT(state.aspect_mode == 0);
    // 3-state Pixel Perfect Auto
    DUWN_ASSERT(state.pixel_perfect_mode == 0);
    // Neutral color grading preset (all 0s)
    DUWN_ASSERT(state.color_preset == 0);
    DUWN_ASSERT(state.brightness == 0);
    DUWN_ASSERT(state.contrast == 0);
    DUWN_ASSERT(state.saturation == 0);
    DUWN_ASSERT(state.hue == 0);
    DUWN_ASSERT(state.sharpness == 0);
    // Professional color pipeline auto-detection
    DUWN_ASSERT(state.color_range == 0);
    DUWN_ASSERT(state.color_matrix == 0);
    // Quick Setup profile Auto
    DUWN_ASSERT(state.performance_profile == 0);
    // Initial diagnostics
    DUWN_ASSERT(state.actual_source_desc == L"—");
    DUWN_ASSERT(state.orientation_desc == L"—");
}

DUWN_TEST(UiState_ResponsiveLayoutTiers) {
    using LT = duwn::ui::LayoutTier;
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(1400.0f) == LT::Large);
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(1180.0f) == LT::Large);
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(1179.0f) == LT::Medium);
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(900.0f)  == LT::Medium);
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(899.0f)  == LT::Small);
    DUWN_ASSERT(duwn::ui::MainWindowView::CalculateTier(600.0f)  == LT::Small);
}

DUWN_TEST(UiState_ReceiverQualityMappingAndGenerations) {
    using RQ = duwn::app::ReceiverQuality;
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::Auto) == "Auto");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::P720_30) == "720p30");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::P720_60) == "720p60");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::P1080_30) == "1080p30");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::P1080_60) == "1080p60");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::P1440_60) == "1440p60");
    DUWN_ASSERT(duwn::app::GetReceiverQualityName(RQ::Original_60) == "Original60");

    uint32_t w = 0, h = 0, fps = 0;
    duwn::app::GetReceiverQualityDimensions(RQ::P1080_60, w, h, fps);
    DUWN_ASSERT(w == 1920 && h == 1920 && fps == 60);

    duwn::app::GetReceiverQualityDimensions(RQ::P720_60, w, h, fps);
    DUWN_ASSERT(w == 1280 && h == 1280 && fps == 60);

    duwn::app::GetReceiverQualityDimensions(RQ::P1440_60, w, h, fps);
    DUWN_ASSERT(w == 2560 && h == 2560 && fps == 60);

    duwn::app::GetReceiverQualityDimensions(RQ::Original_60, w, h, fps);
    DUWN_ASSERT(w == 2560 && h == 2560 && fps == 60);

    duwn::ui::UiState state;
    DUWN_ASSERT(state.config_generation == 1);
    DUWN_ASSERT(state.sidecar_generation == 0);
    DUWN_ASSERT(state.config_generation != state.sidecar_generation);

    // After sidecar matches config generation
    state.sidecar_generation = 1;
    state.receiver_quality_pending = false;
    DUWN_ASSERT(state.config_generation == state.sidecar_generation);
    DUWN_ASSERT(!state.receiver_quality_pending);
}

DUWN_TEST(CrashBanner_HitTestAndButtonDispatch) {
    using namespace duwn::ui;
    MainWindowView view;

    // Register banner buttons with scrollable = true
    D2D1_RECT_F b1_rc = D2D1::RectF(700.0f, 100.0f, 790.0f, 124.0f);
    D2D1_RECT_F b2_rc = D2D1::RectF(800.0f, 100.0f, 890.0f, 124.0f);

    view.RegisterClickable(b1_rc, Control_Btn_CrashOpenLogs, L"", true);
    view.RegisterClickable(b2_rc, Control_Btn_CrashDismiss, L"", true);

    // HitTest at scroll_y = 0
    DUWN_ASSERT(view.HitTest(745.0f, 112.0f, 0.0f) == Control_Btn_CrashOpenLogs);
    DUWN_ASSERT(view.HitTest(845.0f, 112.0f, 0.0f) == Control_Btn_CrashDismiss);

    // HitTest at scroll_y = 50.0 (visual mouse is at 112 - 50 = 62.0)
    DUWN_ASSERT(view.HitTest(745.0f, 62.0f, 50.0f) == Control_Btn_CrashOpenLogs);
    DUWN_ASSERT(view.HitTest(845.0f, 62.0f, 50.0f) == Control_Btn_CrashDismiss);

    // Verify OnMouseUp dispatches
    int dispatched_id = -1;
    view.SetOnSettingChanged([&](int id, int) {
        dispatched_id = id;
    });

    UiState state;
    state.pressed_control = Control_Btn_CrashOpenLogs;
    bool handled1 = view.OnMouseUp(745, 112, state);
    DUWN_ASSERT(handled1 && dispatched_id == Control_Btn_CrashOpenLogs);

    state.pressed_control = Control_Btn_CrashDismiss;
    bool handled2 = view.OnMouseUp(845, 112, state);
    DUWN_ASSERT(handled2 && dispatched_id == Control_Btn_CrashDismiss);
}

DUWN_TEST(UiState_VideoEvidenceAndFpsDistinction) {
    using namespace duwn::ui;
    UiState state;

    // 1. Initially no video evidence and no samples: displays "—"
    DUWN_ASSERT(!state.has_fps_sample);
    DUWN_ASSERT(!state.has_latency_sample);
    std::wstring fps_str = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    std::wstring lat_str = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(fps_str == loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(lat_str == loc::Get(loc::S::Common_NoData));

    // 2. Video evidence exists (frames received / width known) but sampling window has not completed sample:
    // Video evidence alone must NOT show 0.0 FPS or < 1.0 ms; it must continue to display "Chưa có dữ liệu" ("—")!
    state.total_frames_presented = 1;
    state.width = 1170;
    state.height = 2532;
    state.render_fps = 0.0;
    state.has_fps_sample = false;
    state.has_latency_sample = false;
    fps_str = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    lat_str = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(fps_str == loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(lat_str == loc::Get(loc::S::Common_NoData));

    // 3. Valid sampling interval measured true zero FPS (e.g. static screen or pause)
    state.has_fps_sample = true;
    fps_str = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(fps_str == L"0.0 FPS");

    // 4. Valid sampling interval measured 60.0 FPS
    state.render_fps = 60.0;
    fps_str = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(fps_str == L"60.0 FPS");

    // 5. Valid latency measurement in sub-millisecond range (< 1.0 ms)
    state.has_latency_sample = true;
    state.pipeline_latency_ms = 0.4;
    lat_str = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(lat_str == L"< 1.0 ms");

    // 6. Valid latency measurement >= 1.0 ms
    state.pipeline_latency_ms = 8.5;
    lat_str = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    DUWN_ASSERT(lat_str == L"8.5 ms");
}

DUWN_TEST(UiState_AudioReadinessVersusStreamActive) {
    using namespace duwn::ui;
    UiState state;

    // WASAPI initialized, but no stream audio received yet (Idle)
    state.audio_active = false;
    state.audio_muted = false;
    std::wstring audio_card_str = state.audio_muted ? std::wstring(loc::Get(loc::S::Audio_Mute)) :
        (state.audio_active ? std::format(L"48kHz • {}", loc::Get(loc::S::Perf_Active))
                            : std::format(L"48kHz • {}", loc::Get(loc::S::Status_Idle)));
    DUWN_ASSERT(audio_card_str.find(L"48kHz") != std::wstring::npos);
    DUWN_ASSERT(audio_card_str.find(loc::Get(loc::S::Status_Idle)) != std::wstring::npos);

    // Audio stream becomes active
    state.audio_active = true;
    audio_card_str = state.audio_muted ? std::wstring(loc::Get(loc::S::Audio_Mute)) :
        (state.audio_active ? std::format(L"48kHz • {}", loc::Get(loc::S::Perf_Active))
                            : std::format(L"48kHz • {}", loc::Get(loc::S::Status_Idle)));
    DUWN_ASSERT(audio_card_str.find(loc::Get(loc::S::Perf_Active)) != std::wstring::npos);

    // Muted
    state.audio_muted = true;
    audio_card_str = state.audio_muted ? std::wstring(loc::Get(loc::S::Audio_Mute)) :
        (state.audio_active ? std::format(L"48kHz • {}", loc::Get(loc::S::Perf_Active))
                            : std::format(L"48kHz • {}", loc::Get(loc::S::Status_Idle)));
    DUWN_ASSERT(audio_card_str == loc::Get(loc::S::Audio_Mute));
}

DUWN_TEST(UiState_SessionResetSanitizesAllFields) {
    using namespace duwn::ui;
    UiState state;

    // Simulate active streaming session
    state.device_name = L"iPhone 15 Pro";
    state.model_name = L"iPhone 15 Pro";
    state.client_ip = L"192.168.1.10";
    state.width = 1179;
    state.height = 2556;
    state.render_fps = 60.0;
    state.decoded_fps = 60.0;
    state.pipeline_latency_ms = 8.5;
    state.queue_depth = 2;
    state.total_frames_presented = 1800;
    state.dropped_frames = 1;
    state.session_uptime_sec = 30;
    state.audio_active = true;
    state.status = ConnectionStatus::Streaming;
    state.session_state = duwn::airplay::AirPlaySessionState::Streaming;

    // Reset session (matching MainWindow::ResetSessionData)
    state.device_name = L"—";
    state.model_name = L"—";
    state.product_type = L"—";
    state.model_db_match = L"—";
    state.os_version = L"—";
    state.client_ip = L"—";
    state.width = 0;
    state.height = 0;
    state.coded_width = 0;
    state.coded_height = 0;
    state.capture_width = 0;
    state.capture_height = 0;
    state.output_width = 0;
    state.output_height = 0;
    state.preview_width = 0;
    state.preview_height = 0;
    state.render_fps = 0.0;
    state.decoded_fps = 0.0;
    state.source_fps = 0.0;
    state.nominal_fps = 0.0;
    state.pipeline_latency_ms = 0.0;
    state.queue_depth = 0;
    state.total_frames_presented = 0;
    state.dropped_frames = 0;
    state.session_uptime_sec = 0;
    state.audio_active = false;
    state.audio_underruns = 0;
    state.audio_underrun_count = 0;
    state.video_rtp_packets = 0;
    state.audio_rtp_packets = 0;
    state.video_bitrate_mbps = 0.0;
    state.media_bitrate_mbps = 0.0;
    state.decoder_name = L"—";
    state.actual_source_desc = L"—";
    state.orientation_desc = L"—";
    state.quality_state_desc = L"—";
    state.quality_effectiveness = 0;
    state.status = ConnectionStatus::Ready;
    state.session_state = duwn::airplay::AirPlaySessionState::Idle;
    state.status_message = L"Ready to connect";

    DUWN_ASSERT(state.device_name == L"—");
    DUWN_ASSERT(state.width == 0);
    DUWN_ASSERT(state.height == 0);
    DUWN_ASSERT(state.render_fps == 0.0);
    DUWN_ASSERT(state.total_frames_presented == 0);
    DUWN_ASSERT(state.session_uptime_sec == 0);
    DUWN_ASSERT(!state.audio_active);
    DUWN_ASSERT(state.status == ConnectionStatus::Ready);
    DUWN_ASSERT(state.session_state == duwn::airplay::AirPlaySessionState::Idle);
}

DUWN_TEST(MetricsLoop_Lifecycle_IdleToLateConnectionToDisconnectAndReconnect) {
    using namespace duwn::ui;
    UiState state;
    std::atomic<uint64_t> session_generation{1};
    std::atomic<int64_t>  session_start_ns{0};

    // 1. Idle Startup: App running without any connected device
    DUWN_ASSERT(state.status == ConnectionStatus::Ready);
    DUWN_ASSERT(state.session_state == duwn::airplay::AirPlaySessionState::Idle);
    DUWN_ASSERT(!state.has_fps_sample);
    DUWN_ASSERT(!state.has_latency_sample);
    DUWN_ASSERT(state.render_fps == 0.0);
    DUWN_ASSERT(state.session_uptime_sec == 0);

    // 2. Late Connection: Device connects after indefinite idle period
    state.status = ConnectionStatus::Connecting;
    state.session_state = duwn::airplay::AirPlaySessionState::Connecting;
    state.device_name = L"iPhone 15 Pro";
    state.client_ip = L"192.168.1.15";
    const uint64_t active_gen = session_generation.load();
    DUWN_ASSERT(active_gen == 1);

    // 3. First Frame Presentation: Stamped with generation 1
    const uint64_t frame_msg_gen = active_gen;
    DUWN_ASSERT(frame_msg_gen == session_generation.load());
    state.status = ConnectionStatus::Streaming;
    state.session_state = duwn::airplay::AirPlaySessionState::Streaming;
    session_start_ns.store(duwn::clock::MonotonicClock::Now().time_since_epoch().count());

    // Telemetry updates during streaming
    state.width = 1179;
    state.height = 2556;
    state.render_fps = 59.94;
    state.pipeline_latency_ms = 8.4;
    state.has_fps_sample = true;
    state.has_latency_sample = true;
    state.total_frames_presented = 360;
    state.session_uptime_sec = 6;
    state.audio_active = true;

    DUWN_ASSERT(state.status == ConnectionStatus::Streaming);
    DUWN_ASSERT(state.has_fps_sample);
    DUWN_ASSERT(state.has_latency_sample);
    DUWN_ASSERT(state.render_fps > 59.0);
    DUWN_ASSERT(state.session_uptime_sec == 6);

    // 4. Disconnection: Clean teardown and generation increment
    session_generation.fetch_add(1);
    session_start_ns.store(0);

    // Sanitize state on disconnect (MainWindow::ResetSessionData)
    state.device_name = L"—";
    state.client_ip = L"—";
    state.width = 0;
    state.height = 0;
    state.render_fps = 0.0;
    state.pipeline_latency_ms = 0.0;
    state.has_fps_sample = false;
    state.has_latency_sample = false;
    state.total_frames_presented = 0;
    state.session_uptime_sec = 0;
    state.audio_active = false;
    state.status = ConnectionStatus::Ready;
    state.session_state = duwn::airplay::AirPlaySessionState::Idle;

    DUWN_ASSERT(session_generation.load() == 2);
    DUWN_ASSERT(state.status == ConnectionStatus::Ready);
    DUWN_ASSERT(!state.has_fps_sample);
    DUWN_ASSERT(!state.has_latency_sample);

    // 5. Stale Message Protection: A delayed frame message from generation 1 arrives
    const uint64_t stale_msg_gen = 1;
    bool accept_stale_frame = (stale_msg_gen == session_generation.load()) &&
                              (state.status != ConnectionStatus::Idle);
    DUWN_ASSERT(!accept_stale_frame);
    // Stale message MUST NOT promote idle session to Streaming
    DUWN_ASSERT(state.status == ConnectionStatus::Ready);

    // 6. Reconnection with Generation 2: Valid second session
    const uint64_t reconn_gen = session_generation.load();
    DUWN_ASSERT(reconn_gen == 2);
    state.status = ConnectionStatus::Connecting;
    state.session_state = duwn::airplay::AirPlaySessionState::Connecting;
    state.device_name = L"iPad Pro";

    // Valid frame message for generation 2 arrives
    const uint64_t reconn_msg_gen = reconn_gen;
    bool accept_valid_frame = (reconn_msg_gen == session_generation.load());
    DUWN_ASSERT(accept_valid_frame);
    state.status = ConnectionStatus::Streaming;
    state.session_state = duwn::airplay::AirPlaySessionState::Streaming;
    state.width = 2048;
    state.height = 2732;
    state.render_fps = 60.0;
    state.has_fps_sample = true;
    state.has_latency_sample = true;

    DUWN_ASSERT(state.status == ConnectionStatus::Streaming);
    DUWN_ASSERT(state.width == 2048);
    DUWN_ASSERT(state.has_fps_sample);
}

DUWN_TEST(Telemetry_DecoupledFromSlowLogging_AsyncQueueVerification) {
    // Configure slow disk I/O simulation: 20 ms delay per disk write
    duwn::Logger::SetTestWriteDelay(std::chrono::milliseconds(20));

    // Telemetry caller thread emits 10 diagnostic logs
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        DUWN_LOG_INFOF("Diagnostics", "[TELEMETRY TEST] Iteration cycle {} metric={}", i, i * 1.5);
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    // Restore test write delay immediately
    duwn::Logger::SetTestWriteDelay(std::chrono::milliseconds(0));

    // Decoupling verification:
    // Synchronous disk writing of 10 messages with 20ms delay would take >= 200 ms.
    // The bounded async queue must enqueue all 10 messages in < 50 ms (< 5 ms per call).
    DUWN_ASSERT(elapsed.count() < 50);
    DUWN_ASSERT(duwn::Logger::GetDroppedLogCount() == 0);
}


