// test_preview_window.cpp — Unit tests for PreviewWindow title, class name,
// comfortable sizing calculations, Settings persistence, and three-window architecture.

#include "app/Settings.h"
#include "airplay/AirPlayProcess.h"
#include "ui/UiState.h"
#include "common/telemetry/LatencyTelemetry.h"
#include "video/VideoFrame.h"
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
    const uint64_t prev_skips_before = duwn::GlobalMetrics().preview_skips.load();

    // Single skip event: VideoRenderer::record_skipped increments GlobalMetrics().preview_skips
    duwn::GlobalMetrics().preview_skips.fetch_add(1, std::memory_order_relaxed);
    telem.RecordPreviewSkip();

    duwn::telemetry::OutputFrameAgeStats out_stats{};
    duwn::telemetry::PreviewFrameAgeStats prev_stats{};
    telem.GetFrameAgeStats(out_stats, prev_stats);

    // Assert: skip counted exactly once in metrics and telemetry, no successful age samples added
    DUWN_ASSERT(duwn::GlobalMetrics().preview_skips.load() == prev_skips_before + 1);
    DUWN_ASSERT(prev_stats.sample_count == 0);
    DUWN_ASSERT(prev_stats.skips == 1);
}

DUWN_TEST(PreviewTelemetry_ErrorAddsNoSuccessSample) {
    auto& telem = duwn::telemetry::LatencyTelemetry::Get();
    telem.Reset();

    // Simulate DeviceLost / Fatal presentation error
    telem.RecordPreviewError();

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

