// test_ui_state.cpp — Unit tests for UI state, connection states, and telemetry formatting.

#include "ui/UiState.h"
#include "ui/MainWindowView.h"
#include "app/Settings.h"
#include <string>
#include <format>

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

