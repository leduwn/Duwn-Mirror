#include "Settings.h"
#include "common/logging/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <format>
#include <algorithm>

namespace duwn::app {

namespace fs = std::filesystem;

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(std::string_view s) noexcept {
    if (s.empty()) return {};
    int size = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring w(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
    return w;
}

static fs::path SettingsPath() noexcept {
    PWSTR local_app_data = nullptr;
    HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data);
    if (FAILED(hr) || !local_app_data) {
        return fs::current_path() / L"settings.json";
    }
    fs::path base{local_app_data};
    ::CoTaskMemFree(local_app_data);
    return base / L"Duwn Mirror" / L"settings.json";
}

// Simple JSON extraction helpers
static std::string_view FindJsonKeyValue(std::string_view json, std::string_view key) noexcept {
    std::string search_key = "\"" + std::string(key) + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string_view::npos) return {};

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return {};

    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string_view::npos) return {};

    if (json[pos] == '"') {
        size_t end_pos = json.find('"', pos + 1);
        if (end_pos == std::string_view::npos) return {};
        return json.substr(pos + 1, end_pos - (pos + 1));
    } else if (json[pos] == '{') {
        size_t end_pos = json.find('}', pos + 1);
        if (end_pos == std::string_view::npos) return {};
        return json.substr(pos, end_pos - pos + 1);
    } else {
        size_t end_pos = json.find_first_of(",}\r\n", pos);
        if (end_pos == std::string_view::npos) end_pos = json.size();
        return json.substr(pos, end_pos - pos);
    }
}

static bool ParseBool(std::string_view val, bool def_val) noexcept {
    if (val.empty()) return def_val;
    if (val == "true" || val == "1") return true;
    if (val == "false" || val == "0") return false;
    return def_val;
}

static int32_t ParseInt(std::string_view val, int32_t def_val) noexcept {
    if (val.empty()) return def_val;
    try {
        return std::stoi(std::string(val));
    } catch (...) {
        return def_val;
    }
}

bool Settings::ValidateSettings(const Settings& s, std::string* out_reason) noexcept {
    if (s.schema_version == 0) {
        if (out_reason) *out_reason = "schema_version is 0";
        return false;
    }
    if (static_cast<int>(s.streaming_mode) < 0 || static_cast<int>(s.streaming_mode) > 3 ||
        s.custom_video_freshness_ms < 5 || s.custom_video_freshness_ms > 100 ||
        s.custom_video_queue_frames < 1 || s.custom_video_queue_frames > 3) {
        if (out_reason) *out_reason = "invalid receiver streaming policy";
        return false;
    }
    if (s.window_preferences.width <= 0 || s.window_preferences.height <= 0) {
        if (out_reason) *out_reason = "window_preferences dimensions non-positive";
        return false;
    }
    if (s.receiver_width < 320 || s.receiver_width > 3840) {
        if (out_reason) *out_reason = std::format("receiver_width {} out of range [320, 3840]", s.receiver_width);
        return false;
    }
    if (s.receiver_height < 180 || s.receiver_height > 3840) {
        if (out_reason) *out_reason = std::format("receiver_height {} out of range [180, 3840]", s.receiver_height);
        return false;
    }
    if (s.receiver_fps < 15 || s.receiver_fps > 120) {
        if (out_reason) *out_reason = std::format("receiver_fps {} out of range [15, 120]", s.receiver_fps);
        return false;
    }
    if (s.output_width < 320 || s.output_width > 3840) {
        if (out_reason) *out_reason = std::format("output_width {} out of range [320, 3840]", s.output_width);
        return false;
    }
    if (s.output_height < 180 || s.output_height > 3840) {
        if (out_reason) *out_reason = std::format("output_height {} out of range [180, 3840]", s.output_height);
        return false;
    }
    if (s.brightness < -100 || s.brightness > 100) {
        if (out_reason) *out_reason = std::format("brightness {} out of range [-100, 100]", s.brightness);
        return false;
    }
    if (s.contrast < -100 || s.contrast > 100) {
        if (out_reason) *out_reason = std::format("contrast {} out of range [-100, 100]", s.contrast);
        return false;
    }
    if (s.saturation < -100 || s.saturation > 100) {
        if (out_reason) *out_reason = std::format("saturation {} out of range [-100, 100]", s.saturation);
        return false;
    }
    if (s.hue < -100 || s.hue > 100) {
        if (out_reason) *out_reason = std::format("hue {} out of range [-100, 100]", s.hue);
        return false;
    }
    if (s.sharpness < 0 || s.sharpness > 100) {
        if (out_reason) *out_reason = std::format("sharpness {} out of range [0, 100]", s.sharpness);
        return false;
    }
    if (s.monitor_volume < 0.0f || s.monitor_volume > 2.0f) {
        if (out_reason) *out_reason = std::format("monitor_volume {} out of range [0.0, 2.0]", s.monitor_volume);
        return false;
    }
    if (s.audio_sync_offset_ms < -1000 || s.audio_sync_offset_ms > 1000) {
        if (out_reason) *out_reason = std::format("audio_sync_offset_ms {} out of range [-1000, 1000]", s.audio_sync_offset_ms);
        return false;
    }
    return true;
}

bool Settings::MigrateSettingsV0ToV1(Settings& s) noexcept {
    s.schema_version = 1;
    s.first_run_completed = true;
    return true;
}

bool Settings::MigrateSettingsV1ToV2(Settings& s) noexcept {
    s.schema_version = 2;
    // Historical default: receiver_quality was absent; map to Auto
    s.receiver_quality = ReceiverQuality::Auto;
    GetReceiverQualityDimensions(s.receiver_quality, s.receiver_width, s.receiver_height, s.receiver_fps);

    // If output was default 1080p, ensure match_source is true and output_quality is Auto
    if (s.output_width == 1920 && s.output_height == 1080) {
        s.match_source = true;
        s.output_quality = OutputQuality::Auto;
    }

    // Default new video modes introduced in V2
    s.aspect_mode = AspectMode::Auto;
    s.pixel_perfect = PixelPerfectMode::Auto;
    s.scaling_quality = ScalingQuality::Auto;
    s.performance_profile = PerformanceProfile::Auto;

    // Fix historical color test defaults anomaly (7, 0, 2, 4, 7) -> neutral
    if (s.brightness == 7 && s.contrast == 0 && s.saturation == 2 && s.hue == 4 && s.sharpness == 7) {
        s.brightness = 0;
        s.contrast = 0;
        s.saturation = 0;
        s.hue = 0;
        s.sharpness = 0;
        s.color_preset = ColorPreset::Neutral;
        DUWN_LOG_INFO("Settings", "Migrated historical test color defaults (7,0,2,4,7) to neutral (0,0,0,0,0)");
    }
    return true;
}

Settings Settings::Load() noexcept {
    Settings s{};
    auto path = SettingsPath();

    std::ifstream file(path);
    if (!file.is_open()) {
        DUWN_LOG_INFOF("Settings", "No existing settings.json found at '{}'; using defaults",
            WideToUtf8(path.wstring()));
        return s;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Parse window_preferences
    auto win_obj = FindJsonKeyValue(json, "window_preferences");
    if (!win_obj.empty()) {
        s.window_preferences.x = ParseInt(FindJsonKeyValue(win_obj, "x"), s.window_preferences.x);
        s.window_preferences.y = ParseInt(FindJsonKeyValue(win_obj, "y"), s.window_preferences.y);
        s.window_preferences.width = ParseInt(FindJsonKeyValue(win_obj, "width"), s.window_preferences.width);
        s.window_preferences.height = ParseInt(FindJsonKeyValue(win_obj, "height"), s.window_preferences.height);
        s.window_preferences.maximized = ParseBool(FindJsonKeyValue(win_obj, "maximized"), s.window_preferences.maximized);
    }

    s.aspect_ratio_locked = ParseBool(FindJsonKeyValue(json, "aspect_ratio_locked"), s.aspect_ratio_locked);
    s.audio_muted         = ParseBool(FindJsonKeyValue(json, "audio_muted"), s.audio_muted);
    s.always_on_top       = ParseBool(FindJsonKeyValue(json, "always_on_top"), s.always_on_top);

    auto def_name = FindJsonKeyValue(json, "default_receiver_name");
    if (!def_name.empty()) {
        s.default_receiver_name = Utf8ToWide(def_name);
        s.airplay_name = s.default_receiver_name;
    }

    s.gpu_decode = ParseBool(FindJsonKeyValue(json, "gpu_decode"), s.gpu_decode);
    s.vsync      = ParseBool(FindJsonKeyValue(json, "vsync"), s.vsync);
    s.debug_log  = ParseBool(FindJsonKeyValue(json, "debug_log"), s.debug_log);
    s.streaming_mode = static_cast<StreamingMode>(std::clamp(
        ParseInt(FindJsonKeyValue(json, "streaming_mode"), 0), 0, 3));
    s.custom_video_freshness_ms = static_cast<uint32_t>(std::clamp(
        ParseInt(FindJsonKeyValue(json, "custom_video_freshness_ms"), 25), 5, 100));
    s.custom_video_queue_frames = static_cast<uint32_t>(std::clamp(
        ParseInt(FindJsonKeyValue(json, "custom_video_queue_frames"), 2), 1, 3));
    s.connection_mode = static_cast<ConnectionMode>(
        std::clamp(ParseInt(FindJsonKeyValue(json, "connection_mode"), 0), 0, 1));
    s.remember_selected_mode = ParseBool(FindJsonKeyValue(json, "remember_selected_mode"), true);
    s.default_connection_mode = static_cast<ConnectionMode>(
        std::clamp(ParseInt(FindJsonKeyValue(json, "default_connection_mode"), 0), 0, 1));

    // General
    auto lang_val = FindJsonKeyValue(json, "language");
    if (!lang_val.empty()) s.language = Utf8ToWide(lang_val);
    s.start_on_boot         = ParseBool(FindJsonKeyValue(json, "start_on_boot"),         s.start_on_boot);
    s.start_minimized       = ParseBool(FindJsonKeyValue(json, "start_minimized"),       s.start_minimized);
    s.minimize_to_tray      = ParseBool(FindJsonKeyValue(json, "minimize_to_tray"),      s.minimize_to_tray);
    s.remember_window_pos   = ParseBool(FindJsonKeyValue(json, "remember_window_pos"),   s.remember_window_pos);
    s.allow_public_networks = ParseBool(FindJsonKeyValue(json, "allow_public_networks"), s.allow_public_networks);

    // Output Window preferences
    s.auto_open_output_window = ParseBool(FindJsonKeyValue(json, "auto_open_output_window"), s.auto_open_output_window);
    s.output_start_fullscreen = ParseBool(FindJsonKeyValue(json, "output_start_fullscreen"), s.output_start_fullscreen);
    s.preferred_monitor       = ParseInt(FindJsonKeyValue(json, "preferred_monitor"),         s.preferred_monitor);
    s.hide_cursor             = ParseBool(FindJsonKeyValue(json, "hide_cursor"),             s.hide_cursor);
    s.remember_output_pos     = ParseBool(FindJsonKeyValue(json, "remember_output_pos"),     s.remember_output_pos);

    uint32_t raw_schema = static_cast<uint32_t>(ParseInt(FindJsonKeyValue(json, "schema_version"), 0));

    int raw_renderer = ParseInt(FindJsonKeyValue(json, "renderer_mode"), 0);
    s.renderer_mode = static_cast<RendererMode>(std::clamp(raw_renderer, 0, 3));

    auto rx_q_val = FindJsonKeyValue(json, "receiver_quality");
    if (!rx_q_val.empty()) {
        s.receiver_quality = static_cast<ReceiverQuality>(std::clamp(ParseInt(rx_q_val, 0), 0, 6));
    } else {
        s.receiver_quality = ReceiverQuality::Auto;
    }
    GetReceiverQualityDimensions(s.receiver_quality, s.receiver_width, s.receiver_height, s.receiver_fps);

    auto out_w_val = FindJsonKeyValue(json, "output_width");
    auto out_h_val = FindJsonKeyValue(json, "output_height");
    uint32_t parsed_out_w = static_cast<uint32_t>(std::clamp(ParseInt(out_w_val, 1920), 320, 3840));
    uint32_t parsed_out_h = static_cast<uint32_t>(std::clamp(ParseInt(out_h_val, 1080), 180, 2160));
    s.output_width = parsed_out_w;
    s.output_height = parsed_out_h;

    auto match_src_val = FindJsonKeyValue(json, "match_source");
    s.match_source = ParseBool(match_src_val, true);

    auto out_q_val = FindJsonKeyValue(json, "output_quality");
    if (!out_q_val.empty()) {
        s.output_quality = static_cast<OutputQuality>(std::clamp(ParseInt(out_q_val, 0), 0, 6));
    } else {
        if (s.match_source) {
            s.output_quality = OutputQuality::Auto;
        } else if (parsed_out_w == 1280 && parsed_out_h == 720) {
            s.output_quality = OutputQuality::HD;
        } else if (parsed_out_w == 1920 && parsed_out_h == 1080) {
            s.output_quality = OutputQuality::FullHD;
        } else if (parsed_out_w == 2560 && parsed_out_h == 1440) {
            s.output_quality = OutputQuality::QHD_2K;
        } else if (parsed_out_w == 3840 && parsed_out_h == 2160) {
            s.output_quality = OutputQuality::UHD_4K;
        } else {
            s.output_quality = OutputQuality::Custom;
        }
    }

    auto cap_canvas_val = FindJsonKeyValue(json, "capture_canvas");
    if (!cap_canvas_val.empty()) {
        s.capture_canvas = static_cast<CaptureCanvas>(std::clamp(ParseInt(cap_canvas_val, 0), 0, 4));
    } else {
        s.capture_canvas = s.match_source ? CaptureCanvas::FollowSource : CaptureCanvas::Custom;
    }

    auto aspect_val = FindJsonKeyValue(json, "aspect_mode");
    s.aspect_mode = static_cast<AspectMode>(std::clamp(ParseInt(aspect_val, 0), 0, 3));

    auto pp_val = FindJsonKeyValue(json, "pixel_perfect");
    s.pixel_perfect = static_cast<PixelPerfectMode>(std::clamp(ParseInt(pp_val, 0), 0, 2));

    auto scale_val = FindJsonKeyValue(json, "scaling_quality");
    s.scaling_quality = static_cast<ScalingQuality>(std::clamp(ParseInt(scale_val, 0), 0, 3));

    auto profile_val = FindJsonKeyValue(json, "performance_profile");
    s.performance_profile = static_cast<PerformanceProfile>(std::clamp(ParseInt(profile_val, 0), 0, 4));

    int raw_b = ParseInt(FindJsonKeyValue(json, "brightness"), 0);
    int raw_c = ParseInt(FindJsonKeyValue(json, "contrast"), 0);
    int raw_s = ParseInt(FindJsonKeyValue(json, "saturation"), 0);
    int raw_h = ParseInt(FindJsonKeyValue(json, "hue"), 0);
    int raw_sh = ParseInt(FindJsonKeyValue(json, "sharpness"), 0);

    s.brightness = std::clamp(raw_b, -100, 100);
    s.contrast = std::clamp(raw_c, -100, 100);
    s.saturation = std::clamp(raw_s, -100, 100);
    s.hue = std::clamp(raw_h, -100, 100);
    s.sharpness = std::clamp(raw_sh, 0, 100);
    auto preset_val = FindJsonKeyValue(json, "color_preset");
    if (!preset_val.empty()) {
        s.color_preset = static_cast<ColorPreset>(std::clamp(ParseInt(preset_val, 0), 0, 3));
    } else {
        s.color_preset = (s.brightness == 0 && s.contrast == 0 && s.saturation == 0 && s.hue == 0 && s.sharpness == 0)
            ? ColorPreset::Neutral : ColorPreset::Custom;
    }

    s.color_range = static_cast<ColorRange>(std::clamp(ParseInt(FindJsonKeyValue(json, "color_range"), 0), 0, 2));
    s.color_matrix = static_cast<ColorMatrix>(std::clamp(ParseInt(FindJsonKeyValue(json, "color_matrix"), 0), 0, 3));

    auto prev_x_val = FindJsonKeyValue(json, "preview_x");
    if (!prev_x_val.empty()) s.preview_x = ParseInt(prev_x_val, Settings::kDefaultWindowPos);
    auto prev_y_val = FindJsonKeyValue(json, "preview_y");
    if (!prev_y_val.empty()) s.preview_y = ParseInt(prev_y_val, Settings::kDefaultWindowPos);
    auto prev_w_val = FindJsonKeyValue(json, "preview_width");
    if (!prev_w_val.empty()) s.preview_width = static_cast<uint32_t>(ParseInt(prev_w_val, 0));
    auto prev_h_val = FindJsonKeyValue(json, "preview_height");
    if (!prev_h_val.empty()) s.preview_height = static_cast<uint32_t>(ParseInt(prev_h_val, 0));
    auto prev_resized_val = FindJsonKeyValue(json, "preview_user_resized");
    if (!prev_resized_val.empty()) s.preview_user_resized = (prev_resized_val == "true");
    s.show_preview_on_connect = ParseBool(FindJsonKeyValue(json, "show_preview_on_connect"), s.show_preview_on_connect);
    s.hide_preview_on_disconnect = ParseBool(FindJsonKeyValue(json, "hide_preview_on_disconnect"), s.hide_preview_on_disconnect);
    s.preview_always_on_top = ParseBool(FindJsonKeyValue(json, "preview_always_on_top"), s.preview_always_on_top);

    auto fr_val = FindJsonKeyValue(json, "first_run_completed");
    if (!fr_val.empty()) {
        s.first_run_completed = (fr_val == "true" || fr_val == "1");
    } else {
        // Migration: existing settings file without this key means user has already run Duwn Mirror
        s.first_run_completed = true;
    }

    s.unclean_shutdown = ParseBool(FindJsonKeyValue(json, "unclean_shutdown"), false);
    auto lcf_val = FindJsonKeyValue(json, "last_crash_file");
    if (!lcf_val.empty()) {
        s.last_crash_file = Utf8ToWide(lcf_val);
    }

    // Safe schema detection & sequential migration
    if (raw_schema == 0) {
        MigrateSettingsV0ToV1(s);
        raw_schema = 1;
    }

    if (raw_schema < kCurrentSchemaVersion) {
        DUWN_LOG_INFOF("Settings", "Detected settings schema v{}, migrating to v{}...", raw_schema, kCurrentSchemaVersion);

        // Safe migration: create backup of original settings file before writing migrated settings
        fs::path backup_path = path.parent_path() / L"settings.backup.json";
        std::error_code ec;
        fs::copy_file(path, backup_path, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            DUWN_LOG_INFOF("Settings", "Created safe settings backup at '{}'", WideToUtf8(backup_path.wstring()));
        }

        bool migration_ok = true;
        if (raw_schema == 1) {
            migration_ok = MigrateSettingsV1ToV2(s);
        }

        std::string val_reason;
        if (migration_ok && ValidateSettings(s, &val_reason)) {
            s.schema_version = kCurrentSchemaVersion;
            s.Save();
            DUWN_LOG_INFOF("Settings", "Successfully migrated and saved settings schema v{}", kCurrentSchemaVersion);
        } else {
            DUWN_LOG_ERRORF("Settings", "Settings migration/validation failed (reason: {})! Preserving original settings without overwriting.", val_reason);
        }
    } else {
        s.schema_version = raw_schema;
        std::string val_reason;
        if (!ValidateSettings(s, &val_reason)) {
            DUWN_LOG_WARNF("Settings", "Loaded settings from '{}' failed validation (reason: {}); clamping fields to safe ranges",
                WideToUtf8(path.wstring()), val_reason);
        }
    }

    DUWN_LOG_INFOF("Settings", "Loaded settings (schema v{}) from '{}'", s.schema_version, WideToUtf8(path.wstring()));
    return s;
}

void Settings::Save() const noexcept {
    auto path = SettingsPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        DUWN_LOG_ERRORF("Settings", "Failed to open '{}' for writing", WideToUtf8(path.wstring()));
        return;
    }

    std::string json = std::format(
        "{{\n"
        "  \"schema_version\": {},\n"
        "  \"window_preferences\": {{\n"
        "    \"x\": {},\n"
        "    \"y\": {},\n"
        "    \"width\": {},\n"
        "    \"height\": {},\n"
        "    \"maximized\": {}\n"
        "  }},\n"
        "  \"aspect_ratio_locked\": {},\n"
        "  \"audio_muted\": {},\n"
        "  \"always_on_top\": {},\n"
        "  \"default_receiver_name\": \"{}\",\n"
        "  \"gpu_decode\": {},\n"
        "  \"vsync\": {},\n"
        "  \"debug_log\": {},\n"
        "  \"streaming_mode\": {},\n"
        "  \"custom_video_freshness_ms\": {},\n"
        "  \"custom_video_queue_frames\": {},\n"
        "  \"connection_mode\": {},\n"
        "  \"remember_selected_mode\": {},\n"
        "  \"default_connection_mode\": {},\n"
        "  \"renderer_mode\": {},\n"
        "  \"performance_profile\": {},\n"
        "  \"receiver_quality\": {},\n"
        "  \"receiver_width\": {},\n"
        "  \"receiver_height\": {},\n"
        "  \"receiver_fps\": {},\n"
        "  \"output_quality\": {},\n"
        "  \"capture_canvas\": {},\n"
        "  \"output_width\": {},\n"
        "  \"output_height\": {},\n"
        "  \"match_source\": {},\n"
        "  \"aspect_mode\": {},\n"
        "  \"pixel_perfect\": {},\n"
        "  \"scaling_quality\": {},\n"
        "  \"brightness\": {},\n"
        "  \"contrast\": {},\n"
        "  \"saturation\": {},\n"
        "  \"hue\": {},\n"
        "  \"sharpness\": {},\n"
        "  \"color_preset\": {},\n"
        "  \"color_range\": {},\n"
        "  \"color_matrix\": {},\n"
        "  \"language\": \"{}\",\n"
        "  \"start_on_boot\": {},\n"
        "  \"start_minimized\": {},\n"
        "  \"minimize_to_tray\": {},\n"
        "  \"remember_window_pos\": {},\n"
        "  \"allow_public_networks\": {},\n"
        "  \"auto_open_output_window\": {},\n"
        "  \"output_start_fullscreen\": {},\n"
        "  \"preferred_monitor\": {},\n"
        "  \"hide_cursor\": {},\n"
        "  \"remember_output_pos\": {},\n"
        "  \"preview_x\": {},\n"
        "  \"preview_y\": {},\n"
        "  \"preview_width\": {},\n"
        "  \"preview_height\": {},\n"
        "  \"preview_user_resized\": {},\n"
        "  \"show_preview_on_connect\": {},\n"
        "  \"hide_preview_on_disconnect\": {},\n"
        "  \"preview_always_on_top\": {},\n"
        "  \"first_run_completed\": {},\n"
        "  \"unclean_shutdown\": {},\n"
        "  \"last_crash_file\": \"{}\"\n"
        "}}\n",
        schema_version,
        window_preferences.x,
        window_preferences.y,
        window_preferences.width,
        window_preferences.height,
        window_preferences.maximized ? "true" : "false",
        aspect_ratio_locked ? "true" : "false",
        audio_muted ? "true" : "false",
        always_on_top ? "true" : "false",
        WideToUtf8(default_receiver_name.empty() ? airplay_name : default_receiver_name),
        gpu_decode ? "true" : "false",
        vsync ? "true" : "false",
        debug_log ? "true" : "false",
        static_cast<int>(streaming_mode), custom_video_freshness_ms, custom_video_queue_frames,
        static_cast<int>(connection_mode),
        remember_selected_mode ? "true" : "false",
        static_cast<int>(default_connection_mode),
        static_cast<int>(renderer_mode), static_cast<int>(performance_profile),
        static_cast<int>(receiver_quality),
        receiver_width, receiver_height, receiver_fps,
        static_cast<int>(output_quality), static_cast<int>(capture_canvas),
        output_width, output_height, match_source ? "true" : "false",
        static_cast<int>(aspect_mode),
        static_cast<int>(pixel_perfect), static_cast<int>(scaling_quality),
        brightness, contrast, saturation, hue, sharpness,
        static_cast<int>(color_preset),
        static_cast<int>(color_range), static_cast<int>(color_matrix),
        WideToUtf8(language),
        start_on_boot           ? "true" : "false",
        start_minimized         ? "true" : "false",
        minimize_to_tray        ? "true" : "false",
        remember_window_pos     ? "true" : "false",
        allow_public_networks   ? "true" : "false",
        auto_open_output_window ? "true" : "false",
        output_start_fullscreen ? "true" : "false",
        preferred_monitor,
        hide_cursor             ? "true" : "false",
        remember_output_pos     ? "true" : "false",
        preview_x,
        preview_y,
        preview_width,
        preview_height,
        preview_user_resized    ? "true" : "false",
        show_preview_on_connect ? "true" : "false",
        hide_preview_on_disconnect ? "true" : "false",
        preview_always_on_top   ? "true" : "false",
        first_run_completed     ? "true" : "false",
        unclean_shutdown        ? "true" : "false",
        WideToUtf8(last_crash_file)
    );

    file << json;
    file.flush();
    DUWN_LOG_INFOF("Settings", "Saved settings to '{}'", WideToUtf8(path.wstring()));

    // Sync start_on_boot to HKCU Run registry key
    {
        static constexpr wchar_t kRunKey[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        static constexpr wchar_t kRunValue[] = L"DUWNMirror";
        HKEY hkey = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
            if (start_on_boot) {
                // Get path of current exe
                wchar_t exe_path[MAX_PATH] = {};
                ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
                ::RegSetValueExW(hkey, kRunValue, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(exe_path),
                    static_cast<DWORD>((::wcslen(exe_path) + 1) * sizeof(wchar_t)));
            } else {
                ::RegDeleteValueW(hkey, kRunValue);
            }
            ::RegCloseKey(hkey);
        }
    }
}

} // namespace duwn::app
