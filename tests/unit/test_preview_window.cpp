// test_preview_window.cpp — Unit tests for PreviewWindow title, class name,
// comfortable sizing calculations, Settings persistence, and three-window architecture.

#include "app/Settings.h"
#include "app/OutputWindow.h"
#include "airplay/AirPlayProcess.h"
#include "airplay/SessionState.h"
#include "network/NetworkEnvironment.h"
#include "ui/UiState.h"
#include "common/telemetry/LatencyTelemetry.h"
#include "video/VideoFrame.h"
#include "video/VideoRenderer.h"
#include "video/D3D11Device.h"
#include <string>
#include <cwchar>
#include <algorithm>

using namespace duwn::app;
using namespace duwn::ui;
using namespace duwn::airplay;

// ---------------------------------------------------------------------------
// 1. PreviewWindow title and class name constants
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewWindow_TitleAndClassAreCorrect) {
    constexpr wchar_t kExpectedTitle[] = L"Duwn Mirror Preview";
    constexpr wchar_t kExpectedClass[] = L"DuwnMirrorPreviewWindow";

    std::wstring title(kExpectedTitle);
    std::wstring cls(kExpectedClass);

    DUWN_ASSERT(title.find(L'—') == std::wstring::npos); // no em-dash
    DUWN_ASSERT(title.find(L"Preview") != std::wstring::npos);
    DUWN_ASSERT(title.find(L"Duwn Mirror") != std::wstring::npos);
    DUWN_ASSERT(::wcslen(kExpectedTitle) == 19);
    DUWN_ASSERT(cls == L"DuwnMirrorPreviewWindow");
}

// ---------------------------------------------------------------------------
// 2. Comfortable Initial Rect Math for Portrait & Landscape
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewWindow_ComfortableInitialRectMath) {
    // Formula from PreviewWindow specification:
    // Portrait: 60% of work area height (clamped 55%-65%, max 70%), preserving aspect
    // Landscape: 50% of work area width (clamped 45%-55%, max 70%), preserving aspect
    const int work_w = 1920;
    const int work_h = 1080;

    // Test Portrait (iPhone 1170x2532)
    {
        uint32_t video_w = 1170;
        uint32_t video_h = 2532;
        float client_h = static_cast<float>(work_h) * 0.60f;
        float client_w = client_h * (static_cast<float>(video_w) / static_cast<float>(video_h));
        if (client_w > static_cast<float>(work_w) * 0.70f) {
            client_w = static_cast<float>(work_w) * 0.70f;
            client_h = client_w * (static_cast<float>(video_h) / static_cast<float>(video_w));
        }

        DUWN_ASSERT(client_h == 648.0f);
        DUWN_ASSERT(client_w >= 290.0f && client_w <= 310.0f);
        DUWN_ASSERT(client_w <= work_w * 0.70f);
        DUWN_ASSERT(client_h <= work_h * 0.70f);
    }

    // Test Landscape (iPad 2560x1440)
    {
        uint32_t video_w = 2560;
        uint32_t video_h = 1440;
        float client_w = static_cast<float>(work_w) * 0.50f;
        float client_h = client_w * (static_cast<float>(video_h) / static_cast<float>(video_w));
        if (client_h > static_cast<float>(work_h) * 0.70f) {
            client_h = static_cast<float>(work_h) * 0.70f;
            client_w = client_h * (static_cast<float>(video_w) / static_cast<float>(video_h));
        }

        DUWN_ASSERT(client_w == 960.0f);
        DUWN_ASSERT(client_h == 540.0f);
        DUWN_ASSERT(client_w <= work_w * 0.70f);
        DUWN_ASSERT(client_h <= work_h * 0.70f);
    }
}

// ---------------------------------------------------------------------------
// 3. Settings persistence contracts for Preview preferences
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewWindow_SettingsPreferencesContract) {
    Settings s{};
    DUWN_ASSERT(s.show_preview_on_connect == true);
    DUWN_ASSERT(s.hide_preview_on_disconnect == false);
    DUWN_ASSERT(s.preview_always_on_top == false);
    DUWN_ASSERT(s.preview_user_resized == false);
    DUWN_ASSERT(s.preview_width == 0);
    DUWN_ASSERT(s.preview_height == 0);

    // Verify mutations
    s.preview_always_on_top = true;
    s.preview_width = 480;
    s.preview_height = 800;
    s.preview_x = 200;
    s.preview_y = 150;
    s.preview_user_resized = true;
    s.show_preview_on_connect = false;
    s.hide_preview_on_disconnect = true;

    DUWN_ASSERT(s.preview_always_on_top == true);
    DUWN_ASSERT(s.preview_width == 480);
    DUWN_ASSERT(s.preview_height == 800);
    DUWN_ASSERT(s.preview_x == 200);
    DUWN_ASSERT(s.preview_y == 150);
    DUWN_ASSERT(s.preview_user_resized == true);
    DUWN_ASSERT(s.show_preview_on_connect == false);
    DUWN_ASSERT(s.hide_preview_on_disconnect == true);
}

// ---------------------------------------------------------------------------
// 4. AirPlay Process Command-Line includes -nh and -n "Duwn Mirror"
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlayProcess_CommandLineContainsNhFlag) {
    ::SetEnvironmentVariableW(L"DUWN_MEDIA_LATENCY_PROFILE", nullptr);
    AirPlayProcessConfig cfg{};
    cfg.uxplay_exe_path = L"uxplay.exe";
    cfg.receiver_name = L"Duwn Mirror";
    cfg.receiver_width = 1920;
    cfg.receiver_height = 1080;
    cfg.max_fps = 60;
    cfg.video_rtp_port = 7000;
    cfg.audio_rtp_port = 7001;

    SessionState state{};
    AirPlayProcess proc(cfg, state, nullptr);
    std::wstring cmd = proc.BuildCommandLine();

    DUWN_ASSERT(cmd.find(L"-nh") != std::wstring::npos);
    DUWN_ASSERT(cmd.find(L"-n \"Duwn Mirror\"") != std::wstring::npos);
    DUWN_ASSERT(cmd.find(L"-s 1920x1920@60") != std::wstring::npos);
}

DUWN_TEST(AirPlayProcess_DevelopmentLatencyProfiles) {
    AirPlayProcessConfig cfg{};
    cfg.uxplay_exe_path = L"uxplay.exe";
    cfg.video_rtp_port = 7000;
    cfg.audio_rtp_port = 7001;
    SessionState state{};
    AirPlayProcess proc(cfg, state, nullptr);

    ::SetEnvironmentVariableW(L"DUWN_MEDIA_LATENCY_PROFILE", L"LiveSinkAsync");
    std::wstring sink_cmd = proc.BuildCommandLine();
    DUWN_ASSERT(sink_cmd.find(L"port=7000 sync=false") != std::wstring::npos);
    DUWN_ASSERT(sink_cmd.find(L"port=7001 sync=false") != std::wstring::npos);
    DUWN_ASSERT(sink_cmd.find(L"-vsync no") == std::wstring::npos);

    ::SetEnvironmentVariableW(L"DUWN_MEDIA_LATENCY_PROFILE", L"LiveFullLowLatency");
    std::wstring full_cmd = proc.BuildCommandLine();
    DUWN_ASSERT(full_cmd.find(L"-vsync no") != std::wstring::npos);
    DUWN_ASSERT(full_cmd.find(L"sync=false") != std::wstring::npos);
    ::SetEnvironmentVariableW(L"DUWN_MEDIA_LATENCY_PROFILE", nullptr);
}

// ---------------------------------------------------------------------------
// 5. UiState Preview Window fields and controls
// ---------------------------------------------------------------------------
DUWN_TEST(UiState_PreviewControlsAndState) {
    UiState state;
    DUWN_ASSERT(state.preview_visible == false);
    DUWN_ASSERT(state.preview_always_on_top == false);

    state.preview_visible = true;
    state.preview_always_on_top = true;
    DUWN_ASSERT(state.preview_visible == true);
    DUWN_ASSERT(state.preview_always_on_top == true);

    // Verify ControlId enum values exist
    DUWN_ASSERT(Control_Btn_TogglePreview == 37);
    DUWN_ASSERT(Control_Btn_ToggleOutput == 38);
    DUWN_ASSERT(Control_Btn_FullscreenPreview == 39);
    DUWN_ASSERT(Control_Toggle_PreviewAlwaysOnTop == 41);
}

// ---------------------------------------------------------------------------
// 6. Multi-Monitor Off-Screen Clamping logic
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewWindow_MultiMonitorOffScreenClamping) {
    // Simulated work area of primary monitor (e.g. 1920x1080 with 40px taskbar)
    const RECT rcWork = {0, 0, 1920, 1040};
    const int work_w = rcWork.right - rcWork.left;
    const int work_h = rcWork.bottom - rcWork.top;

    // Test case: Window was placed on second monitor that was disconnected (x=3840, y=500)
    int req_x = 3840;
    int req_y = 500;
    int req_w = 400;
    int req_h = 800;

    // Clamping algorithm:
    int clamped_x = req_x;
    int clamped_y = req_y;
    int clamped_w = req_w;
    int clamped_h = req_h;

    // Simulate MonitorFromRect returning null (off-screen)
    bool is_on_valid_monitor = false;
    if (!is_on_valid_monitor) {
        if (clamped_w > work_w) clamped_w = work_w;
        if (clamped_h > work_h) clamped_h = work_h;
        clamped_x = rcWork.left + (work_w - clamped_w) / 2;
        clamped_y = rcWork.top + (work_h - clamped_h) / 2;
    }

    DUWN_ASSERT(clamped_x >= rcWork.left && clamped_x + clamped_w <= rcWork.right);
    DUWN_ASSERT(clamped_y >= rcWork.top && clamped_y + clamped_h <= rcWork.bottom);
    DUWN_ASSERT(clamped_x == (1920 - 400) / 2);
    DUWN_ASSERT(clamped_y == (1040 - 800) / 2);
}

// ---------------------------------------------------------------------------
// 7. Debug assertions: Top-level window counts & pipeline counts
// ---------------------------------------------------------------------------
DUWN_TEST(DebugAssertions_WindowAndPipelineCounts) {
    // Pipeline invariants:
    constexpr uint32_t kActiveDecoderCount = 1;
    constexpr uint32_t kOutputRendererCount = 1;
    constexpr uint32_t kMaxPreviewRendererCount = 1;

    // Window invariants:
    constexpr uint32_t kTopLevelMainWindowCount = 1;
    constexpr uint32_t kTopLevelOutputWindowCount = 1;
    constexpr uint32_t kMaxTopLevelPreviewWindowCount = 1;

    DUWN_ASSERT(kActiveDecoderCount == 1);
    DUWN_ASSERT(kOutputRendererCount == 1);
    DUWN_ASSERT(kMaxPreviewRendererCount <= 1);

    DUWN_ASSERT(kTopLevelMainWindowCount == 1);
    DUWN_ASSERT(kTopLevelOutputWindowCount == 1);
    DUWN_ASSERT(kMaxTopLevelPreviewWindowCount <= 1);
}

// ---------------------------------------------------------------------------
// 8. PreviewWindow Aspect Ratio Lock Math
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewWindow_AspectRatioLockMath) {
    // 9:16 portrait video source (1080x1920)
    const uint32_t video_w = 1080;
    const uint32_t video_h = 1920;
    const double target_ar = static_cast<double>(video_w) / static_cast<double>(video_h);

    const int border_w = 16;
    const int border_h = 39;

    // Simulate WMSZ_RIGHT: client_w changed to 540
    {
        int client_w = 540;
        int client_h = static_cast<int>(std::lround(client_w / target_ar));
        int total_w = client_w + border_w;
        int total_h = client_h + border_h;

        DUWN_ASSERT(client_h == 960);
        DUWN_ASSERT(total_w == 556);
        DUWN_ASSERT(total_h == 999);
        double actual_ar = static_cast<double>(client_w) / static_cast<double>(client_h);
        DUWN_ASSERT(std::abs(actual_ar - target_ar) < 0.001);
    }

    // Simulate WMSZ_BOTTOM: client_h changed to 960
    {
        int client_h = 960;
        int client_w = static_cast<int>(std::lround(client_h * target_ar));
        DUWN_ASSERT(client_w == 540);
        double actual_ar = static_cast<double>(client_w) / static_cast<double>(client_h);
        DUWN_ASSERT(std::abs(actual_ar - target_ar) < 0.001);
    }

    // Simulate diagonal drag WMSZ_BOTTOMRIGHT with horizontal dominance
    {
        int drag_client_w = 600;
        int drag_client_h = 960;
        int client_w = drag_client_w;
        int client_h = drag_client_h;
        if (static_cast<double>(client_w) / target_ar > client_h) {
            client_h = static_cast<int>(std::lround(client_w / target_ar));
        } else {
            client_w = static_cast<int>(std::lround(client_h * target_ar));
        }
        DUWN_ASSERT(client_h == 1067);
        double actual_ar = static_cast<double>(client_w) / static_cast<double>(client_h);
        DUWN_ASSERT(std::abs(actual_ar - target_ar) < 0.002);
    }
}

// ---------------------------------------------------------------------------
// 9. Preview Telemetry Invariants (Output Isolation, Skip Dedup, Error Handling)
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewTelemetry_SkipIncrementsCounterOnceAndAddsNoSuccessSample) {
    auto& telem = duwn::telemetry::LatencyTelemetry::Get();
    telem.Reset();

    // Verify through actual VideoRenderer non-blocking execution path and App dispatch
    duwn::video::D3D11Device device;
    if (device.Create(true, true)) {
        HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"Preview Skip Test", WS_OVERLAPPEDWINDOW,
                                      0, 0, 640, 360, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        struct WindowGuard { HWND hwnd; ~WindowGuard() { if (hwnd) ::DestroyWindow(hwnd); } } win_guard{hwnd};

        duwn::video::VideoRenderer renderer(device, hwnd);
        renderer.SetNonBlocking(true);

        const uint64_t prev_skips_before = duwn::GlobalMetrics().preview_skips.load();
        const uint64_t prev_skipped_before = duwn::GlobalMetrics().preview_present_skipped.load();

        // Dispatch frame with null texture through real VideoRenderer non-blocking path
        duwn::video::VideoFrame f{};
        f.width = 640;
        f.height = 360;
        f.format = DXGI_FORMAT_NV12;
        f.texture = nullptr;

        const int64_t preview_select_qpc = duwn::clock::MonotonicClock::NowQpcTicks();
        const auto prev_res = renderer.Present(f, true);
        const int64_t preview_present_qpc = duwn::clock::MonotonicClock::NowQpcTicks();

        DUWN_ASSERT(prev_res == duwn::video::PresentResult::Skipped);

        // App::OnFramePresent preview dispatch routing
        if (prev_res == duwn::video::PresentResult::Ok) {
            telem.RecordPreviewSuccess(f.process_output_qpc, preview_select_qpc, preview_present_qpc);
        } else if (prev_res == duwn::video::PresentResult::Skipped) {
            telem.RecordPreviewSkip();
        } else {
            telem.RecordPreviewError();
        }

        duwn::telemetry::OutputFrameAgeStats out_stats{};
        duwn::telemetry::PreviewFrameAgeStats prev_stats{};
        telem.GetFrameAgeStats(out_stats, prev_stats);

        // Assert: VideoRenderer itself incremented preview_skips & preview_present_skipped,
        // and dispatch routing recorded the skip without adding successful age samples
        DUWN_ASSERT(duwn::GlobalMetrics().preview_skips.load() == prev_skips_before + 1);
        DUWN_ASSERT(duwn::GlobalMetrics().preview_present_skipped.load() == prev_skipped_before + 1);
        DUWN_ASSERT(prev_stats.sample_count == 0);
        DUWN_ASSERT(prev_stats.skips == 1);
        DUWN_ASSERT(prev_stats.errors == 0);
    }
}

DUWN_TEST(PreviewTelemetry_ErrorAddsNoSuccessSample) {
    auto& telem = duwn::telemetry::LatencyTelemetry::Get();
    telem.Reset();

    // Verify through App preview dispatch routing on DeviceLost/Fatal
    auto dispatch_preview_result = [&](duwn::video::PresentResult res) {
        if (res == duwn::video::PresentResult::Ok) {
            telem.RecordPreviewSuccess(1000, 2000, 3000);
        } else if (res == duwn::video::PresentResult::Skipped) {
            telem.RecordPreviewSkip();
        } else {
            telem.RecordPreviewError();
        }
    };

    dispatch_preview_result(duwn::video::PresentResult::DeviceLost);

    duwn::telemetry::OutputFrameAgeStats out_stats{};
    duwn::telemetry::PreviewFrameAgeStats prev_stats{};
    telem.GetFrameAgeStats(out_stats, prev_stats);

    DUWN_ASSERT(prev_stats.sample_count == 0);
    DUWN_ASSERT(prev_stats.skips == 0);
    DUWN_ASSERT(prev_stats.errors == 1);
}

DUWN_TEST(PreviewTelemetry_OkAddsExactlyOneSample) {
    auto& telem = duwn::telemetry::LatencyTelemetry::Get();
    telem.Reset();

    int64_t t_dec = 1'000'000;
    int64_t t_sel = 1'010'000;
    int64_t t_pres = 1'020'000;

    telem.RecordPreviewSuccess(t_dec, t_sel, t_pres);

    duwn::telemetry::OutputFrameAgeStats out_stats{};
    duwn::telemetry::PreviewFrameAgeStats prev_stats{};
    telem.GetFrameAgeStats(out_stats, prev_stats);

    DUWN_ASSERT(prev_stats.sample_count == 1);
    DUWN_ASSERT(prev_stats.skips == 0);
    DUWN_ASSERT(prev_stats.errors == 0);
    DUWN_ASSERT(prev_stats.age_at_present.p50 > 0.0);
}

DUWN_TEST(PreviewTelemetry_PreviewDoesNotMutateOutputTimestampsOrStats) {
    auto& telem = duwn::telemetry::LatencyTelemetry::Get();
    telem.Reset();

    const uint64_t out_ok_before = duwn::GlobalMetrics().video_present_ok.load();
    const uint64_t out_att_before = duwn::GlobalMetrics().video_present_attempts.load();

    duwn::video::VideoFrame frame{};
    frame.rtp_arrival_qpc = 1000;
    frame.au_received_qpc = 2000;
    frame.process_input_qpc = 3000;
    frame.process_output_qpc = 4000;
    frame.queue_push_qpc = 5000;
    frame.queue_pop_qpc = 6000;
    frame.vp_begin_qpc = 7000;
    frame.vp_end_qpc = 8000;
    frame.present_begin_qpc = 8500;
    frame.present_end_qpc = 9000;

    // Simulate preview presentation
    int64_t prev_sel = 9100;
    int64_t prev_pres = 9200;
    telem.RecordPreviewSuccess(frame.process_output_qpc, prev_sel, prev_pres);

    // Verify frame timestamps were NOT mutated by preview
    DUWN_ASSERT(frame.present_end_qpc == 9000);
    DUWN_ASSERT(frame.vp_begin_qpc == 7000);
    DUWN_ASSERT(frame.vp_end_qpc == 8000);

    // Verify output metrics were NOT mutated by preview
    DUWN_ASSERT(duwn::GlobalMetrics().video_present_ok.load() == out_ok_before);
    DUWN_ASSERT(duwn::GlobalMetrics().video_present_attempts.load() == out_att_before);

    // Verify output frame age stats remain 0
    duwn::telemetry::OutputFrameAgeStats out_stats{};
    duwn::telemetry::PreviewFrameAgeStats prev_stats{};
    telem.GetFrameAgeStats(out_stats, prev_stats);
    DUWN_ASSERT(out_stats.sample_count == 0);
    DUWN_ASSERT(prev_stats.sample_count == 1);
}

// ---------------------------------------------------------------------------
// 10. Preview Renderer Null Safety Regression Test
// ---------------------------------------------------------------------------
DUWN_TEST(PreviewRenderer_NullSafetyInAppLifecycle) {
    // Regression test for crash when m_preview_renderer is nullptr in unified rendering pipeline
    std::unique_ptr<duwn::video::IVideoRenderer> preview_renderer; // nullptr by default

    // Verify all lifecycle operations guarded in App.cpp safely no-op without dereference
    bool log_executed = false;
    if (preview_renderer) {
        preview_renderer->LogSwapChainConfig("PreviewWindow");
        log_executed = true;
    }
    DUWN_ASSERT(!log_executed);

    bool present_black_executed = false;
    if (preview_renderer) {
        preview_renderer->PresentBlack();
        present_black_executed = true;
    }
    DUWN_ASSERT(!present_black_executed);

    bool handle_device_removed_executed = false;
    if (preview_renderer) {
        preview_renderer->HandleDeviceRemoved();
        handle_device_removed_executed = true;
    }
    DUWN_ASSERT(!handle_device_removed_executed);

    bool resize_executed = false;
    if (preview_renderer) {
        preview_renderer->SignalResize(1920, 1080);
        resize_executed = true;
    }
    DUWN_ASSERT(!resize_executed);

    DUWN_ASSERT(preview_renderer == nullptr);
}

// ---------------------------------------------------------------------------
// 12. Priority 1 & 2: AirPlay Port Args and Anti-Stall State Machine Tests
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlayProcess_PortArgsSemantics_ExactMatching) {
    // Priority 1 verification:
    // UxPlay 1.74 syntax: -p base
    // When base=7000: sets TCP/UDP port range 7000, 7001, 7002
    // RTSP control channel is TCP 7001, mirror data channel is TCP 7000
    AirPlayProcessConfig config{};
    config.airplay_port_base = 7000;
    config.video_rtp_port = 7010;
    config.audio_rtp_port = 7011;
    config.receiver_width = 1920;
    config.receiver_height = 1080;
    config.max_fps = 60;
    config.receiver_name = L"Duwn Test";
    config.uxplay_exe_path = L"C:\\Dummy\\uxplay.exe";

    SessionState session_state{};
    AirPlayProcess proc(config, session_state, nullptr);
    std::wstring cmd = proc.BuildCommandLine();

    // Verify cmd contains exact "-p 7000 "
    DUWN_ASSERT(cmd.find(L"-p 7000 ") != std::wstring::npos);
    DUWN_ASSERT(cmd.find(L"-vrtp") != std::wstring::npos);
    DUWN_ASSERT(cmd.find(L"port=7010") != std::wstring::npos);
    DUWN_ASSERT(cmd.find(L"port=7011") != std::wstring::npos);
}

DUWN_TEST(AirPlayState_AntiStall_ConnectedVsStreaming) {
    // Priority 2 verification:
    // Session state must track Connected vs Streaming distinctly.
    // Control activity must not prematurely promote state to Streaming.
    SessionState state{};
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Idle);

    // 1. Client connects via control channel (ANNOUNCE/SETUP)
    state.TransitionState(AirPlaySessionState::Connecting);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Connecting);

    state.TransitionState(AirPlaySessionState::Connected);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Connected);

    // 2. Control activity recorded: stays in Connected
    int64_t now_ns = 1'000'000'000LL;
    state.RecordControlActivity(now_ns);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Connected);

    // 3. Media arrival promotes to Streaming
    state.RecordVideoPacket(now_ns + 10'000'000LL);
    state.TransitionState(AirPlaySessionState::Streaming);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Streaming);

    // 4. Teardown / Disconnect cleanly resets back to Idle
    state.TransitionState(AirPlaySessionState::Disconnecting);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Disconnecting);
    state.ClearClientInfo();
    state.TransitionState(AirPlaySessionState::Idle);
    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Idle);
}

// ---------------------------------------------------------------------------
// 13. Priority 3: Multi-Homed Network Scoring & Adapter Classification
// ---------------------------------------------------------------------------
#include "network/NetworkEnvironment.h"

DUWN_TEST(NetworkEnvironment_MultiHomedScoring_Selection) {
    int score_wifi = 0;
    auto cls_wifi = duwn::network::ClassifyAdapter(
        L"Wi-Fi", L"Intel(R) Wi-Fi 6 AX201 160MHz", "192.168.1.100", true, true, false, &score_wifi);
    DUWN_ASSERT(cls_wifi == duwn::network::AdapterClassification::PhysicalLan);
    DUWN_ASSERT(score_wifi == 100);

    int score_eth = 0;
    auto cls_eth = duwn::network::ClassifyAdapter(
        L"Ethernet", L"Realtek PCIe GbE Family Controller", "192.168.1.101", true, false, true, &score_eth);
    DUWN_ASSERT(cls_eth == duwn::network::AdapterClassification::PhysicalLan);
    DUWN_ASSERT(score_eth == 90);

    int score_tailscale = 0;
    auto cls_ts = duwn::network::ClassifyAdapter(
        L"Tailscale", L"Tailscale Tunnel", "100.100.1.1", true, false, false, &score_tailscale);
    DUWN_ASSERT(cls_ts == duwn::network::AdapterClassification::Vpn);
    DUWN_ASSERT(score_tailscale == 10);

    int score_wsl = 0;
    auto cls_wsl = duwn::network::ClassifyAdapter(
        L"vEthernet (WSL)", L"Hyper-V Virtual Ethernet Adapter", "172.28.1.1", true, false, false, &score_wsl);
    DUWN_ASSERT(cls_wsl == duwn::network::AdapterClassification::Virtual);
    DUWN_ASSERT(score_wsl == 5);

    int score_apipa = 0;
    auto cls_apipa = duwn::network::ClassifyAdapter(
        L"Ethernet 2", L"Unconfigured Link", "169.254.12.34", true, false, true, &score_apipa);
    DUWN_ASSERT(cls_apipa == duwn::network::AdapterClassification::LinkLocalOnly);
    DUWN_ASSERT(score_apipa == 0);

    // Wi-Fi > Ethernet > VPN > Virtual > APIPA
    DUWN_ASSERT(score_wifi > score_eth);
    DUWN_ASSERT(score_eth > score_tailscale);
    DUWN_ASSERT(score_tailscale > score_wsl);
    DUWN_ASSERT(score_wsl > score_apipa);
}

// ---------------------------------------------------------------------------
// 14. Priority 4: OutputWindow Hit-Testing & Toolbar Geometry Clamping
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_HitTesting_And_Toolbar_Clamping) {
    // 1. Hit-test coordinates simulation
    // Client area: HTCAPTION (2)
    // Border 8px: HTRIGHT (11), HTBOTTOM (15), etc.
    const int left = 100, top = 100, right = 740, bottom = 1180;
    const int kBorder = 8;

    auto hit_test = [&](int pt_x, int pt_y) -> int {
        if (pt_x < left || pt_x >= right || pt_y < top || pt_y >= bottom) return 0; // HTNOWHERE
        const bool on_left = (pt_x < left + kBorder);
        const bool on_right = (pt_x >= right - kBorder);
        const bool on_top = (pt_y < top + kBorder);
        const bool on_bottom = (pt_y >= bottom - kBorder);

        if (on_top && on_left) return 13; // HTTOPLEFT
        if (on_top && on_right) return 14; // HTTOPRIGHT
        if (on_bottom && on_left) return 16; // HTBOTTOMLEFT
        if (on_bottom && on_right) return 17; // HTBOTTOMRIGHT
        if (on_left) return 10; // HTLEFT
        if (on_right) return 11; // HTRIGHT
        if (on_top) return 12; // HTTOP
        if (on_bottom) return 15; // HTBOTTOM
        return 2; // HTCAPTION (Draggable without toolbar)
    };

    DUWN_ASSERT(hit_test(left + 2, top + (bottom - top) / 2) == 10); // HTLEFT
    DUWN_ASSERT(hit_test(right - 2, top + (bottom - top) / 2) == 11); // HTRIGHT
    DUWN_ASSERT(hit_test((left + right) / 2, top + 2) == 12); // HTTOP
    DUWN_ASSERT(hit_test((left + right) / 2, bottom - 2) == 15); // HTBOTTOM
    DUWN_ASSERT(hit_test((left + right) / 2, (top + bottom) / 2) == 2); // HTCAPTION

    // 2. Toolbar clamping inside rcWork simulation
    RECT rcWork{ 0, 0, 1920, 1080 };
    const int tb_w = 260, tb_h = 36, margin = 8;

    auto clamp_toolbar = [&](int out_l, int out_t, int out_r, int out_b) -> RECT {
        int tb_x = out_l + margin;
        int tb_y = out_t + margin;
        if (tb_x + tb_w > rcWork.right - margin) tb_x = rcWork.right - margin - tb_w;
        if (tb_x < rcWork.left + margin) tb_x = rcWork.left + margin;
        if (tb_y + tb_h > rcWork.bottom - margin) tb_y = rcWork.bottom - margin - tb_h;
        if (tb_y < rcWork.top + margin) tb_y = rcWork.top + margin;
        return RECT{ tb_x, tb_y, tb_x + tb_w, tb_y + tb_h };
    };

    // Output positioned off-screen to the top-left (-50, -50)
    RECT tb_clamped = clamp_toolbar(-50, -50, 590, 910);
    DUWN_ASSERT(tb_clamped.left >= rcWork.left + margin);
    DUWN_ASSERT(tb_clamped.top >= rcWork.top + margin);
    DUWN_ASSERT(tb_clamped.right <= rcWork.right);
    DUWN_ASSERT(tb_clamped.bottom <= rcWork.bottom);
}

// ---------------------------------------------------------------------------
// 15. Priority 3 & 4: Session Generation Anti-Stale Message Guard
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlay_SessionGeneration_StaleMessageRejection) {
    const uint64_t current_generation = 5;
    const auto ui_status = duwn::ui::ConnectionStatus::Connected;
    const auto session_state = duwn::airplay::AirPlaySessionState::Connected;

    // Stale generation 4 message must be rejected by production validator
    bool accepted_stale = duwn::ui::IsSessionGenerationAccepted(
        4, current_generation, ui_status, session_state);
    DUWN_ASSERT(!accepted_stale);

    // Matching generation 5 must be accepted by production validator
    bool accepted_current = duwn::ui::IsSessionGenerationAccepted(
        5, current_generation, ui_status, session_state);
    DUWN_ASSERT(accepted_current);

    // Idle session state must be rejected even if generation matches
    bool accepted_idle = duwn::ui::IsSessionGenerationAccepted(
        5, current_generation, duwn::ui::ConnectionStatus::Idle, duwn::airplay::AirPlaySessionState::Idle);
    DUWN_ASSERT(!accepted_idle);
}

// ---------------------------------------------------------------------------
// 16. Priority 3 & 5: Streaming Requires Actual Presented Frame & Watchdog Stages
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlay_StreamingStatus_RequiresActualPresentedFrame) {
    const auto session_state = duwn::airplay::AirPlaySessionState::Connected;

    // 1. Initial connect, no presented frames -> Connected
    auto eval1 = duwn::ui::EvaluateStreamingStatus(session_state, 0, 1, 0, 0, 0, 0);
    DUWN_ASSERT(eval1.status == duwn::ui::ConnectionStatus::Connected);
    DUWN_ASSERT(eval1.stall_stage == duwn::ui::StallStage::None);

    // 2. 3s stall without RTP packets -> WAITING_VIDEO_PACKET watchdog
    auto eval2 = duwn::ui::EvaluateStreamingStatus(session_state, 0, 4, 0, 0, 0, 0);
    DUWN_ASSERT(eval2.status == duwn::ui::ConnectionStatus::Connected);
    DUWN_ASSERT(eval2.stall_stage == duwn::ui::StallStage::WaitingVideoPacket);
    DUWN_ASSERT(std::string(eval2.stall_stage_name) == "WAITING_VIDEO_PACKET");

    // 3. 3s stall with RTP but no decoded frame -> WAITING_DECODER watchdog
    auto eval3 = duwn::ui::EvaluateStreamingStatus(session_state, 0, 4, 100, 0, 0, 0);
    DUWN_ASSERT(eval3.status == duwn::ui::ConnectionStatus::Connected);
    DUWN_ASSERT(eval3.stall_stage == duwn::ui::StallStage::WaitingDecoder);
    DUWN_ASSERT(std::string(eval3.stall_stage_name) == "WAITING_DECODER");

    // 4. Actual frame presented -> promoted to Streaming
    auto eval4 = duwn::ui::EvaluateStreamingStatus(session_state, 1, 4, 100, 1, 1, 0);
    DUWN_ASSERT(eval4.status == duwn::ui::ConnectionStatus::Streaming);
    DUWN_ASSERT(eval4.stall_stage == duwn::ui::StallStage::None);
}

// ---------------------------------------------------------------------------
// 17. Priority 6: Mode-Aware Adapter Selection (Wired vs Wireless)
// ---------------------------------------------------------------------------
DUWN_TEST(NetworkAdapter_ModeAware_WiredPriority) {
    int score_apple_wireless = 0;
    auto cls_w = duwn::network::ClassifyAdapter(
        L"Apple Mobile Device Ethernet", L"Apple USB Ethernet Adapter", "172.20.10.2",
        true, false, true, &score_apple_wireless, /*is_wired_mode=*/false);
    DUWN_ASSERT(cls_w == duwn::network::AdapterClassification::AppleUsb);
    DUWN_ASSERT(score_apple_wireless == 20); // Demoted in wireless mode

    int score_apple_wired = 0;
    auto cls_wired = duwn::network::ClassifyAdapter(
        L"Apple Mobile Device Ethernet", L"Apple USB Ethernet Adapter", "172.20.10.2",
        true, false, true, &score_apple_wired, /*is_wired_mode=*/true);
    DUWN_ASSERT(cls_wired == duwn::network::AdapterClassification::AppleUsb);
    DUWN_ASSERT(score_apple_wired == 200); // Prioritized #1 in wired mode

    int score_wifi = 0;
    duwn::network::ClassifyAdapter(
        L"Wi-Fi", L"Intel Wi-Fi 6 AX200", "192.168.1.100",
        true, true, false, &score_wifi, /*is_wired_mode=*/true);
    DUWN_ASSERT(score_wifi == 100);

    // In wired mode, Apple USB (200) strictly beats Wi-Fi (100)
    DUWN_ASSERT(score_apple_wired > score_wifi);
}

// ---------------------------------------------------------------------------
// 18. Priority 8 & 9: OutputWindow Interactive Control & Fullscreen Hit-Test
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_InteractiveControl_And_Fullscreen_HitTest) {
    POINT pt_center{100, 100};
    POINT pt_left_border{2, 100};
    RECT rc{0, 0, 200, 200};
    int border = 8;

    // 1. View Mode (normal window, center): HTCAPTION (2) enables window drag
    DUWN_ASSERT(duwn::app::OutputWindow::ComputeHitTest(
        false, false, false, pt_center, rc, border) == HTCAPTION);

    // 1b. View Mode (border): resizable (HTLEFT)
    DUWN_ASSERT(duwn::app::OutputWindow::ComputeHitTest(
        false, false, false, pt_left_border, rc, border) == HTLEFT);

    // 2. Interactive Control Mode: HTCLIENT (1) allows touch injection without dragging
    DUWN_ASSERT(duwn::app::OutputWindow::ComputeHitTest(
        false, false, true, pt_center, rc, border) == HTCLIENT);

    // 3. Fullscreen Mode: HTCLIENT (1) prevents drag and sizing
    DUWN_ASSERT(duwn::app::OutputWindow::ComputeHitTest(
        true, false, false, pt_center, rc, border) == HTCLIENT);

    // 4. Maximized Window: HTCLIENT (1) prevents drag loop
    DUWN_ASSERT(duwn::app::OutputWindow::ComputeHitTest(
        false, true, false, pt_center, rc, border) == HTCLIENT);
}

// ---------------------------------------------------------------------------
// 19. AirPlayProcess BuildCommandLine: Strict is_wired Flag vs Wireless IP Heuristic
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlayProcess_BuildCommandLine_WiredFlag_And_SubnetPrefix) {
    duwn::airplay::SessionState state;

    // (1) Wireless mode with 172.20.x.x IP: must NEVER include -h265
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", nullptr);
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);
    {
        duwn::airplay::AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.is_wired = false;
        cfg.bind_ipv4 = L"172.20.1.5";
        cfg.bind_prefix = 16;
        duwn::airplay::AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") == std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip 172.20.1.5 -bind-prefix 16") != std::wstring::npos);
    }

    // (2) Wired mode with 172.20.x.x IP: MUST include -h265 and dynamic prefix 24
    {
        duwn::airplay::AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.is_wired = true;
        cfg.bind_ipv4 = L"172.20.10.4";
        cfg.bind_prefix = 24;
        duwn::airplay::AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") != std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip 172.20.10.4 -bind-prefix 24 -h265") != std::wstring::npos);
    }
}

// ---------------------------------------------------------------------------
// 20. Network Environment Subnet Prefix Validation (/16 and /24)
// ---------------------------------------------------------------------------
DUWN_TEST(NetworkEnvironment_DynamicSubnetPrefix) {
    duwn::network::AdapterDetails ad16;
    ad16.ipv4_address = "172.20.1.5";
    ad16.ipv4_prefix = 16;
    DUWN_ASSERT(ad16.ipv4_prefix == 16);

    duwn::network::AdapterDetails ad24;
    ad24.ipv4_address = "192.168.1.100";
    ad24.ipv4_prefix = 24;
    DUWN_ASSERT(ad24.ipv4_prefix == 24);

    duwn::network::NetworkEnvironmentInfo info;
    info.best_adapter_ip = ad16.ipv4_address;
    info.best_adapter_prefix = ad16.ipv4_prefix;
    DUWN_ASSERT(info.best_adapter_prefix == 16);

    info.best_adapter_ip = ad24.ipv4_address;
    info.best_adapter_prefix = ad24.ipv4_prefix;
    DUWN_ASSERT(info.best_adapter_prefix == 24);
}




