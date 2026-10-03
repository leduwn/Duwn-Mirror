#pragma once
// UiState.h — Reactive state and telemetry model for DUWN Mirror UI.
// All metrics strictly bound to core pipeline; zero hardcoded/mocked values.

#include "airplay/AirPlaySessionState.h"
#include "wired/WiredDeviceManager.h"
#include "wired/WiredControlClient.h"
#include <string>
#include <vector>
#include <cstdint>

namespace duwn::ui {

enum class NavTab {
    Mirror = 0,
    Video,
    Audio,
    Color,
    Settings,
    Diagnostics,
    Performance = Diagnostics,
    About = Diagnostics
};

enum class SettingsSubTab {
    General,
    Output,
    Network,
    Privacy,
    Advanced
};

enum class ConnectionStatus {
    Ready,          // Ready for AirPlay / USB connection (advertising)
    Connecting,     // Handshake in progress
    Streaming,      // Actively presenting frames
    Paused,         // Paused / static screen
    Reconnecting,   // Stream dropped, attempting reconnect
    Disconnected,   // Session terminated / disconnected
    Error,          // Fatal error or sidecar failure
    Idle = Ready,   // Alias for Ready
    Connected = Streaming // Alias for Streaming
};

struct AudioDeviceItem {
    std::wstring id;
    std::wstring name;
};

struct UiState {
    // Navigation
    NavTab active_tab{NavTab::Mirror};
    SettingsSubTab active_settings_sub_tab{SettingsSubTab::General};
    int connection_mode{0}; // 0 wireless AirPlay, 1 wired USB
    bool remember_selected_mode{true};
    int default_connection_mode{0};
    wired::WiredSnapshot wired{};
    bool wired_needs_mirroring_reconnect{false};

    // Wired USB Device Control (CoreDevice HID)
    wired::WiredControlState control_state{wired::WiredControlState::Disabled};
    bool device_control_enabled{false};
    std::wstring control_status_msg;
    std::wstring control_device_name{L"—"};
    std::wstring control_ios_version{L"—"};
    uint64_t control_events_sent{0};
    uint64_t control_events_failed{0};
    double control_latency_avg_ms{0.0};
    double control_latency_p50_ms{0.0};
    double control_latency_p95_ms{0.0};
    double control_latency_p99_ms{0.0};
    D2D1_RECT_F wired_interactive_preview_rect{0.0f, 0.0f, 0.0f, 0.0f};

    // Connection & Device Info (Real data only; "—" when unavailable)
    ConnectionStatus status{ConnectionStatus::Idle};
    airplay::AirPlaySessionState session_state{airplay::AirPlaySessionState::Idle};
    std::wstring device_name{L"—"};
    std::wstring model_name{L"—"};
    std::wstring product_type{L"—"};
    std::wstring model_db_match{L"—"};
    std::wstring os_version{L"—"};
    std::wstring status_message{L"Ready to connect"};
    std::wstring client_ip{L"—"};
    std::wstring transport_type{L"Local RTP/UDP"};
    std::wstring signal_quality{L"N/A"};

    // Stream Details (0 / "—" when not streaming)
    uint32_t width{0};
    uint32_t height{0};
    double nominal_fps{0.0};
    double render_fps{0.0};
    double decode_time_ms{0.0};
    double pipeline_latency_ms{0.0};
    uint32_t queue_depth{0};
    uint64_t total_frames_presented{0};
    uint64_t dropped_frames{0};
    bool zero_copy{false};
    bool audio_active{false};
    bool audio_muted{false};
    std::wstring decoder_name{L"—"};
    std::wstring transport_name{L"Local RTP/UDP"};
    std::wstring gpu_name{L"—"};
    std::wstring orientation_desc{L"—"};
    std::wstring actual_source_desc{L"—"};
    uint32_t requested_width{1920};
    uint32_t requested_height{1080};
    uint32_t requested_fps{60};
    uint32_t coded_width{0};
    uint32_t coded_height{0};
    double video_bitrate_mbps{0.0};
    double media_bitrate_mbps{0.0}; // Canonical 1-sec RTP video payload bitrate in Megabits/sec (excludes IP/UDP/RTP headers)
    std::wstring requested_quality_class{L"—"};
    int quality_effectiveness{0}; // 0 Unknown, 1 DeliveredAsRequested, 2 SourceLimited, 3 PartiallyDelivered
    std::wstring quality_state_desc{L"—"};
    std::wstring quality_effectiveness_raw{L"UNKNOWN"};
    uint32_t output_width{1920};
    uint32_t output_height{1080};
    uint32_t selected_output_width{1920};
    uint32_t selected_output_height{1080};
    uint32_t capture_width{0};
    uint32_t capture_height{0};
    uint32_t preview_width{0};
    uint32_t preview_height{0};
    double decoded_fps{0.0};
    double source_fps{0.0};
    std::wstring renderer_name{L"—"};
    std::wstring scaler_name{L"—"};
    std::wstring color_processing_name{L"—"};
    int renderer_mode{0};
    int performance_profile{0};
    int streaming_mode{0};
    uint32_t custom_video_freshness_ms{25};
    uint32_t custom_video_queue_frames{2};
    int receiver_quality{0};
    int output_quality{0};
    int capture_canvas{0};
    int aspect_mode{0};
    int pixel_perfect_mode{0};
    int color_preset{0};
    int scaling_quality{0};
    int color_range{0};
    int color_matrix{0};
    bool match_source{true};
    bool pixel_perfect{false};
    bool receiver_quality_pending{false};
    uint64_t config_generation{1};
    uint64_t sidecar_generation{0};
    uint32_t sidecar_pid{0};
    bool advanced_color_expanded{false};
    int brightness{0};
    int contrast{0};
    int saturation{0};
    int hue{0};
    int sharpness{0};
    bool filter_supported[5]{false, false, false, false, false};

    // Extended Real-time Pipeline Telemetry (from GlobalMetrics)
    uint64_t video_rtp_packets{0};
    uint64_t audio_rtp_packets{0};
    double av_offset_ms{0.0};
    double pts_delta_avg_ms{0.0};
    double render_time_ms{0.0};
    uint64_t audio_underruns{0};
    uint64_t client_drops{0};
    uint64_t session_q_full{0};
    double display_wait_ms{0.0};

    // Session Metrics
    int64_t session_uptime_sec{0};

    // Audio device selection (populated by App from AudioDeviceManager)
    std::wstring audio_device_id{};           // empty = system default
    std::wstring audio_device_name{L"System Default"};
    std::wstring resolved_audio_device_name{L"—"};
    bool         audio_fallback_active{false};
    float        audio_volume{1.0f};          // 0.0f .. 1.0f
    std::vector<AudioDeviceItem> available_audio_devices;
    double       audio_buffer_ms{0.0};
    uint64_t     audio_underrun_count{0};
    std::wstring audio_session_display_name{L"Duwn Mirror"};

    // General / language
    std::wstring language{L"auto"};
    bool is_first_run{false};
    bool show_crash_banner{false};
    std::wstring crash_banner_file;
    bool start_on_boot{false};
    bool start_minimized{false};
    bool minimize_to_tray{true};
    bool remember_window_pos{true};
    bool debug_log{false};
    bool allow_public_networks{false};
    bool is_public_network{false};
    bool public_rules_missing{false};
    bool public_rules_unexpected{false};

    // Output settings
    bool auto_open_output_window{true};
    bool output_start_fullscreen{false};
    int  preferred_monitor{0};
    bool hide_cursor{false};
    bool remember_output_pos{true};

    // Window & Output Control Toggles
    bool output_window_visible{true};
    bool output_fullscreen{false};
    bool aspect_locked{true};
    bool always_on_top{false};
    bool show_output_toolbar{true};
    bool is_screen_only{false};
    bool preview_visible{false};
    bool preview_always_on_top{false};

    // UI Interactive States (Hit testing / hover / click)
    int hovered_control{0};
    int pressed_control{0};
    int open_dropdown{0};        // 0 = none, otherwise ControlId of open dropdown trigger
    int active_color_slider{-1}; // 0..4 when dragging slider, -1 otherwise
    uint32_t anim_tick{0};       // For spinner & connection pulse
};

// Control IDs for hit testing
enum ControlId : int {
    Control_None = 0,

    // Navigation
    Control_Nav_Mirror = 10,
    Control_Nav_Video,
    Control_Nav_Audio,
    Control_Nav_Color,
    Control_Nav_Settings,
    Control_Nav_Diagnostics,
    Control_Nav_Performance = Control_Nav_Diagnostics,
    Control_Nav_About = Control_Nav_Diagnostics,
    Control_Mode_Wireless = 20,
    Control_Mode_Wired,
    Control_Wired_Refresh,
    Control_Wired_Start,
    Control_Wired_Troubleshoot,
    Control_Wired_ToggleControl = 193,
    Control_Wired_Btn_Home = 194,
    Control_Wired_Btn_Lock = 195,
    Control_Wired_Btn_VolDown = 196,
    Control_Wired_Btn_VolUp = 197,
    Control_Wired_Btn_Mute = 198,
    Control_Wired_Btn_Siri = 199,

    // Primary Stream & Output Controls
    Control_Btn_OutputWindow = 30,
    Control_Btn_Fullscreen,
    Control_Btn_AspectLock,
    Control_Btn_AlwaysOnTop,
    Control_Btn_Mute,
    Control_Btn_Disconnect,
    Control_Btn_ToggleScreenOnly = 36,
    Control_Btn_TogglePreview = 37,
    Control_Btn_ToggleOutput = 38,
    Control_Btn_FullscreenPreview = 39,
    Control_Toggle_PreviewAlwaysOnTop = 41,
    Control_Slider_QuickVolume = 42,
    Control_Slider_AudioVolume = 43,
    Control_Toggle_OutputToolbar = 44,

    // Advanced / Diagnostics Action
    Control_Btn_FlushPipeline,

    // Center Session Action (e.g. Open/Hide Output Window)
    Control_Btn_CenterAction = 40,

    // Header Action
    Control_Btn_HeaderAbout = 50,
    Control_Set_Renderer = 60,
    Control_Set_Profile,
    Control_Set_Receiver,
    Control_Set_Output,
    Control_Set_CaptureCanvas,
    Control_Set_Scaling,
    Control_Set_PixelPerfect,
    Control_Set_Brightness,
    Control_Set_Contrast,
    Control_Set_Saturation,
    Control_Set_Hue,
    Control_Set_Sharpness,
    Control_Set_ResetColor,
    Control_Set_ColorRange,
    Control_Set_ColorMatrix,
    Control_Set_CustomOutput,
    Control_Set_AspectMode,
    Control_Set_ColorPreset,
    Control_Toggle_AdvancedColor,
    Control_Reset_Brightness,
    Control_Reset_Contrast,
    Control_Reset_Saturation,
    Control_Reset_Hue,
    Control_Reset_Sharpness,
    Control_Set_StreamingMode,
    Control_Set_VideoFreshness,
    Control_Set_VideoQueueFrames,

    // Settings sub-tabs
    Control_SubTab_General = 200,
    Control_SubTab_Output,
    Control_SubTab_Network,
    Control_SubTab_Privacy,
    Control_SubTab_Advanced,
    Control_SubTab_Audio,

    // General settings (210..229)
    Control_Set_Language = 210,
    Control_Toggle_StartOnBoot,
    Control_Toggle_StartMinimized,
    Control_Toggle_MinimizeToTray,
    Control_Toggle_RememberWindowPos,
    Control_Toggle_AllowPublicNetworks,
    Control_Toggle_RememberMode,
    Control_Set_DefaultMode,
    Control_Btn_OpenNetworkSettings,
    Control_Btn_AllowOnThisNetwork,
    Control_Btn_RepairPublicRules,
    Control_Btn_DisablePublicRules,

    // Output settings (230..249)
    Control_Toggle_AutoOpenOutput = 230,
    Control_Toggle_StartFullscreen,
    Control_Set_PreferredMonitor,
    Control_Toggle_HideCursor,
    Control_Toggle_RememberOutputPos,

    // Audio settings (250..269)
    Control_Set_AudioDevice = 250,
    Control_Toggle_AudioMute,
    Control_Set_AudioVolume,
    Control_Set_AudioSyncOffset,
    Control_Btn_TestAudio,
    Control_Toggle_DebugLog = 270,
    Control_Btn_OpenLogs,
    Control_Btn_OpenSettingsFile,
    Control_Btn_FirstRunContinue = 280,
    Control_Btn_CrashOpenLogs = 281,
    Control_Btn_CrashDismiss = 282,
    Control_Btn_CopyDiagnostics = 283,
    Control_Btn_OpenNotices = 284,
    Control_Btn_OpenGitHub = 285,

    // Dropdown item IDs: base + (dropdown_type * 16) + item_index
    Control_Dropdown_Item_Base = 300
};

} // namespace duwn::ui
