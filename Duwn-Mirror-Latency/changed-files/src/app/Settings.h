#pragma once
// Settings — persistent application settings.
// Serialised to/from JSON in %LOCALAPPDATA%\Duwn Mirror\settings.json.

#include <string>
#include <string_view>
#include <cstdint>
#include "common/streaming/StreamingPolicy.h"

namespace duwn::app {

enum class ConnectionMode { WirelessAirPlay = 0, WiredUsb = 1 };

using StreamingMode = duwn::StreamingMode;

enum class ReceiverQuality {
    Auto = 0,
    P720_30 = 1,
    P720_60 = 2,
    P1080_30 = 3,
    P1080_60 = 4,
    P1440_60 = 5,
    Original_60 = 6,
};

enum class OutputQuality {
    Auto = 0,     // Follow Source / Original (no scaling)
    HD = 1,       // Long edge ~1280
    FullHD = 2,   // Long edge ~1920
    QHD_2K = 3,   // Long edge ~2560
    UHD_4K = 4,   // Long edge ~3840
    Original = 5, // 1:1 direct passthrough of visible source dimensions
    Custom = 6    // User-specified custom dimensions
};

enum class CaptureCanvas {
    FollowSource = 0,    // Output dimensions derived from source aspect ratio
    Canvas_16_9_HD = 1,     // 1280x720 fixed broadcast frame
    Canvas_16_9_FullHD = 2, // 1920x1080 fixed broadcast frame
    Canvas_16_9_2K = 3,     // 2560x1440 fixed broadcast frame
    Custom = 4           // Custom fixed broadcast frame
};

inline uint32_t GetOutputQualityLongEdge(OutputQuality q) noexcept {
    switch (q) {
    case OutputQuality::Auto:     return 0; // 0 = follow source / 1:1
    case OutputQuality::Original: return 0; // 0 = 1:1 direct passthrough
    case OutputQuality::HD:       return 1280;
    case OutputQuality::FullHD:   return 1920;
    case OutputQuality::QHD_2K:   return 2560;
    case OutputQuality::UHD_4K:   return 3840;
    default:                      return 0;
    }
}

enum class AspectMode {
    Auto = 0, // Match Source -> exact source aspect; Fixed canvas -> Fit
    Fit  = 1, // letterbox / pillarbox
    Fill = 2, // crop to fill
    Stretch = 3 // fill canvas with distortion
};

enum class PixelPerfectMode {
    Auto = 0, // 1:1 if match source or dimensions equal, else single final scale
    On   = 1, // 1:1 pixel centered without upscale if fits canvas
    Off  = 2  // standard scaling
};

enum class TransportMode {
    RtpUdpLegacy = 0, // UxPlay GStreamer rtph264pay -> UDP 127.0.0.1 -> DUWN RTP depacketizer
    DirectIpc    = 1, // UxPlay decrypted NAL -> Win32 shared memory ring -> DUWN decoder
};

enum class RendererMode { Auto = 0, HardwareD3D11, Compatibility, Software };
enum class PerformanceProfile { Auto = 0, HighQuality, Balanced, LowSpec, Custom };
enum class ScalingQuality { Auto = 0, Fast, Sharp, HighQuality };
enum class ColorRange { Auto = 0, Limited, Full };
enum class ColorMatrix { Auto = 0, BT601, BT709, BT2020 };
enum class ColorPreset { Neutral = 0, Vivid = 1, Soft = 2, Custom = 3 };

inline std::string_view GetReceiverQualityName(ReceiverQuality q) noexcept {
    switch (q) {
    case ReceiverQuality::Auto: return "Auto";
    case ReceiverQuality::P720_30: return "720p30";
    case ReceiverQuality::P720_60: return "720p60";
    case ReceiverQuality::P1080_30: return "1080p30";
    case ReceiverQuality::P1080_60: return "1080p60";
    case ReceiverQuality::P1440_60: return "1440p60";
    case ReceiverQuality::Original_60: return "Original60";
    default: return "Auto";
    }
}

inline void GetReceiverQualityDimensions(ReceiverQuality q, uint32_t& out_w, uint32_t& out_h, uint32_t& out_fps) noexcept {
    switch (q) {
    case ReceiverQuality::Auto:
        out_w = 1920; out_h = 1920; out_fps = 60; break;
    case ReceiverQuality::P720_30:
        out_w = 1280; out_h = 1280; out_fps = 30; break;
    case ReceiverQuality::P720_60:
        out_w = 1280; out_h = 1280; out_fps = 60; break;
    case ReceiverQuality::P1080_30:
        out_w = 1920; out_h = 1920; out_fps = 30; break;
    case ReceiverQuality::P1080_60:
        out_w = 1920; out_h = 1920; out_fps = 60; break;
    case ReceiverQuality::P1440_60:
        out_w = 2560; out_h = 2560; out_fps = 60; break;
    case ReceiverQuality::Original_60:
        out_w = 2560; out_h = 2560; out_fps = 60; break;
    default:
        out_w = 1920; out_h = 1920; out_fps = 60; break;
    }
}

struct WindowPreferences {
    int32_t x{100};
    int32_t y{100};
    int32_t width{1280};
    int32_t height{740};
    bool    maximized{false};
};

struct Settings {
    // Schema version
    static constexpr uint32_t kCurrentSchemaVersion = 2;
    uint32_t schema_version{kCurrentSchemaVersion};
    ConnectionMode connection_mode{ConnectionMode::WirelessAirPlay};
    bool remember_selected_mode{true};
    ConnectionMode default_connection_mode{ConnectionMode::WirelessAirPlay};

    // Window & Output Preferences
    WindowPreferences window_preferences{};
    bool aspect_ratio_locked{true};
    bool audio_muted{false};
    bool always_on_top{false};
    std::wstring default_receiver_name{L"DuwnMirror"};

    // Streaming
    StreamingMode streaming_mode{StreamingMode::SmoothLive};
    uint32_t custom_video_freshness_ms{25};
    uint32_t custom_video_queue_frames{2};
    TransportMode transport_mode{TransportMode::RtpUdpLegacy};

    // Video
    bool     gpu_decode{true};
    AspectMode aspect_mode{AspectMode::Auto};
    bool     vsync{true};
    RendererMode renderer_mode{RendererMode::Auto};
    PerformanceProfile performance_profile{PerformanceProfile::Auto};
    ReceiverQuality receiver_quality{ReceiverQuality::Auto};
    uint32_t receiver_width{1920};
    uint32_t receiver_height{1080};
    uint32_t receiver_fps{60};
    OutputQuality output_quality{OutputQuality::Auto};
    CaptureCanvas capture_canvas{CaptureCanvas::FollowSource};
    uint32_t output_width{1920};
    uint32_t output_height{1080};
    bool match_source{true};
    PixelPerfectMode pixel_perfect{PixelPerfectMode::Auto};
    ScalingQuality scaling_quality{ScalingQuality::Auto};
    int32_t brightness{0};
    int32_t contrast{0};
    int32_t saturation{0};
    int32_t hue{0};
    int32_t sharpness{0};
    ColorRange color_range{ColorRange::Auto};
    ColorMatrix color_matrix{ColorMatrix::Auto};
    ColorPreset color_preset{ColorPreset::Neutral};

    // Audio
    bool         monitor_enabled{false};
    std::wstring monitor_device_id{}; // empty = default
    float        monitor_volume{1.0f};
    int32_t      audio_sync_offset_ms{0}; // user-adjustable A/V offset trim

    // Receiver
    std::wstring airplay_name{L"DuwnMirror"};

    // Paths
    std::wstring uxplay_exe_path{L"duwn-airplay\\uxplay.exe"}; // relative to app dir

    // General
    std::wstring language{L"auto"};  // "auto" | "en-US" | "vi-VN"
    bool start_on_boot{false};
    bool start_minimized{false};
    bool minimize_to_tray{true};
    bool remember_window_pos{true};
    bool allow_public_networks{false};

    // Output Window preferences
    bool auto_open_output_window{false};
    bool output_start_fullscreen{false};
    int32_t preferred_monitor{0};
    bool hide_cursor{false};
    bool remember_output_pos{true};

    // Preview Window preferences
    static constexpr int32_t kDefaultWindowPos = static_cast<int32_t>(0x80000000); // Matches CW_USEDEFAULT
    int32_t preview_x{kDefaultWindowPos};
    int32_t preview_y{kDefaultWindowPos};
    uint32_t preview_width{0};
    uint32_t preview_height{0};
    bool preview_user_resized{false};
    bool show_preview_on_connect{true};
    bool hide_preview_on_disconnect{false};
    bool preview_always_on_top{false};

    // Advanced
    bool debug_log{false};

    // First-run & crash recovery
    bool first_run_completed{false};
    bool unclean_shutdown{false};
    std::wstring last_crash_file{};

    // Migration and validation
    static bool MigrateSettingsV0ToV1(Settings& s) noexcept;
    static bool MigrateSettingsV1ToV2(Settings& s) noexcept;
    static bool ValidateSettings(const Settings& s, std::string* out_reason = nullptr) noexcept;

    // Load/save
    static Settings Load() noexcept;
    void Save() const noexcept;
};

} // namespace duwn::app
