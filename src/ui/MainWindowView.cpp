#include "MainWindowView.h"
#include "Loc.h"
#include "common/Version.h"
#include "common/streaming/StreamingPolicy.h"
#include <algorithm>
#include <format>
#include <cmath>

namespace duwn::ui {

MainWindowView::MainWindowView() noexcept = default;

bool MainWindowView::Init(HWND hwnd) noexcept {
    m_hwnd = hwnd;
    RECT rc{};
    ::GetClientRect(m_hwnd, &rc);
    m_width  = std::max(100L, rc.right - rc.left);
    m_height = std::max(100L, rc.bottom - rc.top);
    return m_renderer.Init(hwnd);
}

void MainWindowView::Shutdown() noexcept {
    m_renderer.Shutdown();
    m_clickables.clear();
    m_hwnd = nullptr;
}

void MainWindowView::Resize(uint32_t width, uint32_t height) noexcept {
    m_width  = std::max(100u, width);
    m_height = std::max(100u, height);
    m_renderer.Resize(m_width, m_height);
}

void MainWindowView::RegisterClickable(const D2D1_RECT_F& rect, int control_id, std::wstring_view tooltip, bool scrollable) noexcept {
    m_clickables.push_back(ClickableRegion{ rect, control_id, std::wstring(tooltip), scrollable });
}

int MainWindowView::HitTest(float fx, float fy, float scroll_y) const noexcept {
    for (auto it = m_clickables.rbegin(); it != m_clickables.rend(); ++it) {
        float test_y = it->scrollable ? (fy + scroll_y) : fy;
        float test_x = fx;
        if (test_x >= it->rect.left && test_x <= it->rect.right &&
            test_y >= it->rect.top && test_y <= it->rect.bottom) {
            if (it->scrollable && m_viewport_bottom > m_viewport_top) {
                float visual_top = it->rect.top - scroll_y;
                float visual_bottom = it->rect.bottom - scroll_y;
                if (visual_bottom < m_viewport_top || visual_top > m_viewport_bottom) {
                    continue; // clipped by viewport
                }
            }
            return it->control_id;
        }
    }
    return Control_None;
}

float MainWindowView::GetCurrentScrollY(NavTab tab) const noexcept {
    switch (tab) {
    case NavTab::Mirror:      return m_scroll_y_mirror;
    case NavTab::Video:       return m_scroll_y_video;
    case NavTab::Audio:       return m_scroll_y_audio;
    case NavTab::Color:       return m_scroll_y_color;
    case NavTab::Settings:    return m_scroll_y_settings;
    case NavTab::Diagnostics: return m_scroll_y_diagnostics;
    default:                  return 0.0f;
    }
}

void MainWindowView::SetCurrentScrollY(NavTab tab, float y) noexcept {
    switch (tab) {
    case NavTab::Mirror:      m_scroll_y_mirror = y; break;
    case NavTab::Video:       m_scroll_y_video = y; break;
    case NavTab::Audio:       m_scroll_y_audio = y; break;
    case NavTab::Color:       m_scroll_y_color = y; break;
    case NavTab::Settings:    m_scroll_y_settings = y; break;
    case NavTab::Diagnostics: m_scroll_y_diagnostics = y; break;
    }
}

void MainWindowView::RenderScrollbar(float scroll_y, float content_h, float viewport_h, const D2D1_RECT_F& viewport_rc) noexcept {
    if (content_h <= viewport_h || viewport_h <= 0.0f) return;

    const float sb_w = 6.0f;
    const float sb_x = viewport_rc.right - sb_w - 3.0f;
    const float sb_top = viewport_rc.top + 4.0f;
    const float sb_bottom = viewport_rc.bottom - 4.0f;
    const float sb_track_h = sb_bottom - sb_top;
    if (sb_track_h <= 20.0f) return;

    const float thumb_h = std::clamp(sb_track_h * (viewport_h / content_h), 28.0f, sb_track_h);
    const float max_scroll = content_h - viewport_h;
    const float scroll_ratio = std::clamp(scroll_y / max_scroll, 0.0f, 1.0f);
    const float thumb_y = sb_top + (sb_track_h - thumb_h) * scroll_ratio;

    // Track
    D2D1_RECT_F track_rc = D2D1::RectF(sb_x, sb_top, sb_x + sb_w, sb_bottom);
    m_renderer.FillRoundedRect(track_rc, 3.0f, m_renderer.BrushCardBorder());

    // Thumb
    D2D1_RECT_F thumb_rc = D2D1::RectF(sb_x, thumb_y, sb_x + sb_w, thumb_y + thumb_h);
    ID2D1SolidColorBrush* thumb_brush = m_is_dragging_scrollbar
        ? m_renderer.BrushBrandBlue()
        : m_renderer.BrushTextMuted();
    m_renderer.FillRoundedRect(thumb_rc, 3.0f, thumb_brush);
}

int MainWindowView::ColorValueAt(int control_id, float x) const noexcept {
    for (const auto& item : m_clickables) {
        if (item.control_id == control_id) {
            const float left = item.rect.left + 95.0f;
            const float right = item.rect.right - 95.0f;
            if (right <= left) return 0;
            const float frac = std::clamp((x - left) / (right - left), 0.0f, 1.0f);
            if (control_id == Control_Set_Sharpness) {
                return static_cast<int>(std::round(frac * 100.0f));
            } else {
                return static_cast<int>(std::round(frac * 200.0f - 100.0f));
            }
        }
    }
    return 0;
}

float MainWindowView::VolumeValueAt(int control_id, float x) const noexcept {
    for (const auto& item : m_clickables) {
        if (item.control_id == control_id) {
            const float left = item.rect.left;
            const float right = item.rect.right;
            if (right <= left) return 1.0f;
            return std::clamp((x - left) / (right - left), 0.0f, 1.0f);
        }
    }
    return 1.0f;
}

bool MainWindowView::OnMouseMove(int x, int y, UiState& state) noexcept {
    m_renderer.GetDpi(&m_dpi_x, &m_dpi_y);
    float dip_x = static_cast<float>(x) * 96.0f / (m_dpi_x > 0.0f ? m_dpi_x : 96.0f);
    float dip_y = static_cast<float>(y) * 96.0f / (m_dpi_y > 0.0f ? m_dpi_y : 96.0f);

    float cur_scroll = GetCurrentScrollY(state.active_tab);

    if (m_is_dragging_scrollbar) {
        const float sb_top = m_viewport_top + 4.0f;
        const float sb_bottom = m_viewport_bottom - 4.0f;
        const float sb_track_h = sb_bottom - sb_top;
        if (sb_track_h > 20.0f && m_content_height > m_viewport_height) {
            const float thumb_h = std::clamp(sb_track_h * (m_viewport_height / m_content_height), 28.0f, sb_track_h);
            const float travel = sb_track_h - thumb_h;
            if (travel > 0.0f) {
                const float max_scroll = m_content_height - m_viewport_height;
                const float delta_mouse_y = dip_y - m_drag_start_mouse_y;
                float new_scroll = m_drag_start_scroll_y + (delta_mouse_y / travel) * max_scroll;
                new_scroll = std::clamp(new_scroll, 0.0f, max_scroll);
                SetCurrentScrollY(state.active_tab, new_scroll);
                if (state.open_dropdown != 0) state.open_dropdown = 0;
                return true;
            }
        }
    }

    if (state.pressed_control >= Control_Set_Brightness &&
        state.pressed_control <= Control_Set_Sharpness && m_on_setting_changed) {
        m_on_setting_changed(state.pressed_control, ColorValueAt(state.pressed_control, dip_x));
        return true;
    }

    if ((state.pressed_control == Control_Slider_QuickVolume ||
         state.pressed_control == Control_Slider_AudioVolume) && m_on_volume_changed) {
        float vol = VolumeValueAt(state.pressed_control, dip_x);
        state.audio_volume = vol;
        m_on_volume_changed(vol);
        return true;
    }

    int hit = HitTest(dip_x, dip_y, cur_scroll);
    if (hit != state.hovered_control) {
        state.hovered_control = hit;
        if (hit != Control_None) {
            ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
        } else {
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        }
        return true;
    }
    return false;
}

bool MainWindowView::OnMouseDown(int x, int y, UiState& state) noexcept {
    m_renderer.GetDpi(&m_dpi_x, &m_dpi_y);
    float dip_x = static_cast<float>(x) * 96.0f / (m_dpi_x > 0.0f ? m_dpi_x : 96.0f);
    float dip_y = static_cast<float>(y) * 96.0f / (m_dpi_y > 0.0f ? m_dpi_y : 96.0f);

    float cur_scroll = GetCurrentScrollY(state.active_tab);

    // Check scrollbar hit
    if (m_content_height > m_viewport_height && m_viewport_height > 0.0f) {
        const float sb_w = 6.0f;
        const float sb_x = m_viewport_right - sb_w - 3.0f;
        const float sb_top = m_viewport_top + 4.0f;
        const float sb_bottom = m_viewport_bottom - 4.0f;
        const float sb_track_h = sb_bottom - sb_top;

        if (dip_x >= sb_x - 4.0f && dip_x <= m_viewport_right &&
            dip_y >= sb_top && dip_y <= sb_bottom && sb_track_h > 20.0f) {

            const float thumb_h = std::clamp(sb_track_h * (m_viewport_height / m_content_height), 28.0f, sb_track_h);
            const float max_scroll = m_content_height - m_viewport_height;
            const float scroll_ratio = std::clamp(cur_scroll / max_scroll, 0.0f, 1.0f);
            const float thumb_y = sb_top + (sb_track_h - thumb_h) * scroll_ratio;

            if (dip_y >= thumb_y && dip_y <= thumb_y + thumb_h) {
                m_is_dragging_scrollbar = true;
                m_drag_start_mouse_y = dip_y;
                m_drag_start_scroll_y = cur_scroll;
                return true;
            } else if (dip_y < thumb_y) {
                float new_scroll = std::max(0.0f, cur_scroll - m_viewport_height * 0.8f);
                SetCurrentScrollY(state.active_tab, new_scroll);
                if (state.open_dropdown != 0) state.open_dropdown = 0;
                return true;
            } else {
                float new_scroll = std::min(max_scroll, cur_scroll + m_viewport_height * 0.8f);
                SetCurrentScrollY(state.active_tab, new_scroll);
                if (state.open_dropdown != 0) state.open_dropdown = 0;
                return true;
            }
        }
    }

    int hit = HitTest(dip_x, dip_y, cur_scroll);

    if (state.open_dropdown != 0) {
        if (hit >= Control_Dropdown_Item_Base) {
            const int item_index = hit - Control_Dropdown_Item_Base;
            const int target_dropdown = state.open_dropdown;
            state.open_dropdown = 0;
            state.hovered_control = Control_None;
            state.pressed_control = Control_None;

            if (target_dropdown == Control_Set_Language) {
                const wchar_t* langs[] = { L"auto", L"en-US", L"vi-VN" };
                if (item_index >= 0 && item_index < 3) {
                    state.language = langs[item_index];
                    if (m_on_language_changed) m_on_language_changed(langs[item_index]);
                }
            } else if (target_dropdown == Control_Set_AudioDevice) {
                if (item_index == 0) {
                    state.audio_device_id = L"";
                    state.audio_device_name = loc::Get(loc::S::Audio_SystemDefault);
                    if (m_on_audio_device_changed) m_on_audio_device_changed(L"");
                } else if (static_cast<size_t>(item_index - 1) < state.available_audio_devices.size()) {
                    const auto& dev = state.available_audio_devices[item_index - 1];
                    state.audio_device_id = dev.id;
                    state.audio_device_name = dev.name;
                    if (m_on_audio_device_changed) m_on_audio_device_changed(dev.id);
                }
            } else {
                if (m_on_setting_changed) m_on_setting_changed(target_dropdown, item_index);
            }
            return true;
        } else {
            const int old_dropdown = state.open_dropdown;
            state.open_dropdown = 0;
            state.hovered_control = Control_None;
            state.pressed_control = Control_None;
            // If another trigger was clicked, switch to it
            if (hit == Control_Set_Profile || hit == Control_Set_Receiver ||
                hit == Control_Set_StreamingMode || hit == Control_Set_VideoFreshness ||
                hit == Control_Set_VideoQueueFrames ||
                hit == Control_Set_Output || hit == Control_Set_CaptureCanvas || hit == Control_Set_AspectMode ||
                hit == Control_Set_Scaling || hit == Control_Set_PixelPerfect ||
                hit == Control_Set_Renderer || hit == Control_Set_ColorPreset ||
                hit == Control_Set_ColorRange || hit == Control_Set_ColorMatrix ||
                hit == Control_Set_Language || hit == Control_Set_AudioDevice ||
                hit == Control_Set_PreferredMonitor) {
                if (hit != old_dropdown) state.open_dropdown = hit;
            }
            return true;
        }
    }

    if (hit != Control_None) {
        state.pressed_control = hit;
        if (hit >= Control_Set_Brightness && hit <= Control_Set_Sharpness && m_on_setting_changed) {
            state.active_color_slider = hit - Control_Set_Brightness;
            m_on_setting_changed(hit, ColorValueAt(hit, dip_x));
        }
        if ((hit == Control_Slider_QuickVolume || hit == Control_Slider_AudioVolume) && m_on_volume_changed) {
            float vol = VolumeValueAt(hit, dip_x);
            state.audio_volume = vol;
            m_on_volume_changed(vol);
        }
        return true;
    }

    // Touch interaction on interactive preview in Wired mode
    if (state.connection_mode == 1 &&
        (state.control_state == wired::WiredControlState::Ready ||
         state.control_state == wired::WiredControlState::Active)) {
        float test_y = dip_y + cur_scroll;
        if (dip_x >= m_wired_preview_rect.left && dip_x <= m_wired_preview_rect.right &&
            test_y >= m_wired_preview_rect.top  && test_y <= m_wired_preview_rect.bottom) {
            m_touch_down = true;
            m_touch_start_x = dip_x;
            m_touch_start_y = test_y;
            return true;
        }
    }

    return false;
}

bool MainWindowView::OnDoubleClick(int x, int y, UiState& /*state*/) noexcept {
    m_renderer.GetDpi(&m_dpi_x, &m_dpi_y);
    float dip_x = static_cast<float>(x) * 96.0f / (m_dpi_x > 0.0f ? m_dpi_x : 96.0f);
    float dip_y = static_cast<float>(y) * 96.0f / (m_dpi_y > 0.0f ? m_dpi_y : 96.0f);

    float cur_scroll = GetCurrentScrollY(NavTab::Settings);
    int hit = HitTest(dip_x, dip_y, cur_scroll);
    if (hit >= Control_Set_Brightness && hit <= Control_Set_Sharpness) {
        if (m_on_setting_changed) {
            m_on_setting_changed(hit, 0);
            return true;
        }
    }
    return false;
}

bool MainWindowView::OnMouseUp(int x, int y, UiState& state) noexcept {
    if (m_is_dragging_scrollbar) {
        m_is_dragging_scrollbar = false;
        return true;
    }

    m_renderer.GetDpi(&m_dpi_x, &m_dpi_y);
    float dip_x = static_cast<float>(x) * 96.0f / (m_dpi_x > 0.0f ? m_dpi_x : 96.0f);
    float dip_y = static_cast<float>(y) * 96.0f / (m_dpi_y > 0.0f ? m_dpi_y : 96.0f);

    float cur_scroll = GetCurrentScrollY(state.active_tab);
    float test_y = dip_y + cur_scroll;

    if (m_touch_down) {
        m_touch_down = false;
        float dx = dip_x - m_touch_start_x;
        float dy = test_y - m_touch_start_y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 8.0f) {
            uint16_t hx = 0, hy = 0;
            if (wired::WiredControlClient::ScreenToHidCoordinates(
                    dip_x, test_y, m_wired_preview_rect, 0, hx, hy)) {
                if (m_on_touch_tap) m_on_touch_tap(hx, hy);
            }
        } else {
            uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (wired::WiredControlClient::ScreenToHidCoordinates(
                    m_touch_start_x, m_touch_start_y, m_wired_preview_rect, 0, x1, y1) &&
                wired::WiredControlClient::ScreenToHidCoordinates(
                    dip_x, test_y, m_wired_preview_rect, 0, x2, y2)) {
                if (m_on_touch_drag) m_on_touch_drag(x1, y1, x2, y2);
            }
        }
        return true;
    }

    int hit = HitTest(dip_x, dip_y, cur_scroll);
    int pressed = state.pressed_control;
    state.pressed_control = Control_None;
    state.active_color_slider = -1;

    if (hit != Control_None && hit == pressed) {
        switch (hit) {
        case Control_Nav_Mirror:
            state.active_tab = NavTab::Mirror;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Mirror);
            break;
        case Control_Nav_Video:
            state.active_tab = NavTab::Video;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Video);
            break;
        case Control_Nav_Audio:
            state.active_tab = NavTab::Audio;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Audio);
            break;
        case Control_Nav_Color:
            state.active_tab = NavTab::Color;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Color);
            break;
        case Control_Nav_Settings:
            state.active_tab = NavTab::Settings;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Settings);
            break;
        case Control_Nav_Diagnostics:
        case Control_Btn_HeaderAbout:
            state.active_tab = NavTab::Diagnostics;
            if (m_on_tab_changed) m_on_tab_changed(NavTab::Diagnostics);
            break;

        case Control_Btn_ToggleScreenOnly:
            state.is_screen_only = !state.is_screen_only;
            if (m_on_toggle_screen_only) m_on_toggle_screen_only();
            break;

        case Control_Btn_TogglePreview:
            state.preview_visible = !state.preview_visible;
            if (m_on_toggle_preview) m_on_toggle_preview();
            break;
        case Control_Btn_ToggleOutput:
        case Control_Btn_OutputWindow:
        case Control_Btn_CenterAction:
            state.output_window_visible = !state.output_window_visible;
            if (m_on_toggle_output) m_on_toggle_output();
            break;
        case Control_Toggle_OutputToolbar:
            state.show_output_toolbar = !state.show_output_toolbar;
            if (m_on_toggle_output_toolbar) m_on_toggle_output_toolbar(state.show_output_toolbar);
            if (m_on_setting_changed) m_on_setting_changed(Control_Toggle_OutputToolbar, state.show_output_toolbar ? 1 : 0);
            break;
        case Control_Btn_FullscreenPreview:
            if (m_on_fullscreen_preview) m_on_fullscreen_preview();
            break;
        case Control_Toggle_PreviewAlwaysOnTop:
            state.preview_always_on_top = !state.preview_always_on_top;
            if (m_on_toggle_preview_always_on_top) m_on_toggle_preview_always_on_top();
            break;
        case Control_Btn_Fullscreen:
            state.output_fullscreen = !state.output_fullscreen;
            if (m_on_toggle_fullscreen) m_on_toggle_fullscreen();
            break;
        case Control_Btn_AspectLock:
            state.aspect_locked = !state.aspect_locked;
            if (m_on_toggle_aspect_lock) m_on_toggle_aspect_lock();
            break;
        case Control_Btn_AlwaysOnTop:
            state.always_on_top = !state.always_on_top;
            if (m_on_toggle_always_on_top) m_on_toggle_always_on_top();
            break;
        case Control_Btn_Mute:
            state.audio_muted = !state.audio_muted;
            if (m_on_toggle_mute) m_on_toggle_mute();
            break;
        case Control_Btn_Disconnect:
            if (state.status == ConnectionStatus::Streaming ||
                state.status == ConnectionStatus::Connecting ||
                state.session_state == airplay::AirPlaySessionState::Streaming ||
                state.session_state == airplay::AirPlaySessionState::Connected ||
                state.session_state == airplay::AirPlaySessionState::Paused) {
                if (m_on_disconnect) m_on_disconnect();
            }
            break;
        case Control_Btn_FlushPipeline:
            if (m_on_flush_pipeline) m_on_flush_pipeline();
            break;
        case Control_Mode_Wireless:
        case Control_Mode_Wired:
        case Control_Wired_Refresh:
        case Control_Wired_Start:
        case Control_Wired_Troubleshoot:
        case Control_Wired_ToggleControl:
        case Control_Wired_Btn_Home:
        case Control_Wired_Btn_Lock:
        case Control_Wired_Btn_VolDown:
        case Control_Wired_Btn_VolUp:
        case Control_Wired_Btn_Mute:
        case Control_Wired_Btn_Siri:
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        // Settings sub-tab navigation
        case Control_SubTab_General:
            state.active_settings_sub_tab = SettingsSubTab::General;
            m_scroll_y_settings = 0.0f;
            break;
        case Control_SubTab_Output:
            state.active_settings_sub_tab = SettingsSubTab::Output;
            m_scroll_y_settings = 0.0f;
            break;
        case Control_SubTab_Network:
            state.active_settings_sub_tab = SettingsSubTab::Network;
            m_scroll_y_settings = 0.0f;
            break;
        case Control_SubTab_Privacy:
            state.active_settings_sub_tab = SettingsSubTab::Privacy;
            m_scroll_y_settings = 0.0f;
            break;
        case Control_SubTab_Advanced:
            state.active_settings_sub_tab = SettingsSubTab::Advanced;
            m_scroll_y_settings = 0.0f;
            break;

        // General page
        case Control_Set_Language:
            state.open_dropdown = (state.open_dropdown == hit) ? 0 : hit;
            break;
        case Control_Toggle_StartOnBoot:
            state.start_on_boot = !state.start_on_boot;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.start_on_boot ? 1 : 0);
            break;
        case Control_Toggle_StartMinimized:
            state.start_minimized = !state.start_minimized;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.start_minimized ? 1 : 0);
            break;
        case Control_Toggle_MinimizeToTray:
            state.minimize_to_tray = !state.minimize_to_tray;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.minimize_to_tray ? 1 : 0);
            break;
        case Control_Toggle_RememberWindowPos:
            state.remember_window_pos = !state.remember_window_pos;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.remember_window_pos ? 1 : 0);
            break;
        case Control_Toggle_RememberMode:
            state.remember_selected_mode = !state.remember_selected_mode;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.remember_selected_mode ? 1 : 0);
            break;
        case Control_Set_DefaultMode:
            state.default_connection_mode = 1 - state.default_connection_mode;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.default_connection_mode);
            break;
        case Control_Toggle_DebugLog:
            state.debug_log = !state.debug_log;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.debug_log ? 1 : 0);
            break;
        case Control_Btn_OpenLogs:
        case Control_Btn_OpenSettingsFile:
        case Control_Btn_CrashOpenLogs:
        case Control_Btn_CrashDismiss:
        case Control_Btn_FirstRunContinue:
        case Control_Btn_CopyDiagnostics:
        case Control_Btn_OpenNotices:
        case Control_Btn_OpenGitHub:
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        // Output settings page
        case Control_Toggle_AutoOpenOutput:
            state.auto_open_output_window = !state.auto_open_output_window;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.auto_open_output_window ? 1 : 0);
            break;
        case Control_Toggle_StartFullscreen:
            state.output_start_fullscreen = !state.output_start_fullscreen;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.output_start_fullscreen ? 1 : 0);
            break;
        case Control_Set_PreferredMonitor:
            state.open_dropdown = (state.open_dropdown == hit) ? 0 : hit;
            break;
        case Control_Toggle_HideCursor:
            state.hide_cursor = !state.hide_cursor;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.hide_cursor ? 1 : 0);
            break;
        case Control_Toggle_RememberOutputPos:
            state.remember_output_pos = !state.remember_output_pos;
            if (m_on_setting_changed) m_on_setting_changed(hit, state.remember_output_pos ? 1 : 0);
            break;

        // Audio page
        case Control_Set_AudioDevice:
            state.open_dropdown = (state.open_dropdown == hit) ? 0 : hit;
            break;
        case Control_Toggle_AudioMute:
            state.audio_muted = !state.audio_muted;
            if (m_on_toggle_mute) m_on_toggle_mute();
            break;
        case Control_Btn_TestAudio:
            if (m_on_test_audio) m_on_test_audio();
            break;

        case Control_Set_Profile:
        case Control_Set_Receiver:
        case Control_Set_StreamingMode:
        case Control_Set_VideoFreshness:
        case Control_Set_VideoQueueFrames:
        case Control_Set_Output:
        case Control_Set_CaptureCanvas:
        case Control_Set_AspectMode:
        case Control_Set_Scaling:
        case Control_Set_PixelPerfect:
        case Control_Set_Renderer:
        case Control_Set_ColorPreset:
        case Control_Set_ColorRange:
        case Control_Set_ColorMatrix:
            state.open_dropdown = (state.open_dropdown == hit) ? 0 : hit;
            break;

        case Control_Set_CustomOutput:
            state.open_dropdown = 0;
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        case Control_Toggle_AdvancedColor:
            state.open_dropdown = 0;
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        case Control_Set_ResetColor:
            state.open_dropdown = 0;
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        case Control_Reset_Brightness:
        case Control_Reset_Contrast:
        case Control_Reset_Saturation:
        case Control_Reset_Hue:
        case Control_Reset_Sharpness:
            if (m_on_setting_changed) m_on_setting_changed(hit, 0);
            break;

        case Control_Set_Brightness:
        case Control_Set_Contrast:
        case Control_Set_Saturation:
        case Control_Set_Hue:
        case Control_Set_Sharpness:
            if (m_on_setting_changed) m_on_setting_changed(hit, ColorValueAt(hit, dip_x));
            break;

        default:
            break;
        }
        return true;
    }
    return pressed != Control_None;
}

bool MainWindowView::OnMouseLeave(UiState& state) noexcept {
    m_is_dragging_scrollbar = false;
    if (state.hovered_control != Control_None || state.pressed_control != Control_None) {
        state.hovered_control = Control_None;
        state.pressed_control = Control_None;
        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        return true;
    }
    return false;
}

bool MainWindowView::OnMouseWheel(int /*x*/, int /*y*/, int delta, UiState& state) noexcept {
    float max_scroll = std::max(0.0f, m_content_height - m_viewport_height);
    if (max_scroll <= 0.0f) return false;

    float cur_scroll = GetCurrentScrollY(state.active_tab);
    float scroll_delta = -static_cast<float>(delta) / 120.0f * 48.0f;
    float new_scroll = std::clamp(cur_scroll + scroll_delta, 0.0f, max_scroll);

    if (new_scroll != cur_scroll) {
        SetCurrentScrollY(state.active_tab, new_scroll);
        if (state.open_dropdown != 0) {
            state.open_dropdown = 0;
        }
        return true;
    }
    return false;
}

bool MainWindowView::OnKeyDown(WPARAM vk, UiState& state) noexcept {
    // Check for Wired Device Control shortcuts (Ctrl+H, Ctrl+L, Ctrl+[, Ctrl+], Ctrl+S)
    if (state.connection_mode == 1 &&
        (state.control_state == wired::WiredControlState::Ready ||
         state.control_state == wired::WiredControlState::Active)) {
        bool ctrl_down = (::GetKeyState(VK_CONTROL) < 0);
        if (ctrl_down) {
            switch (vk) {
            case 'H':
            case 'h':
                if (m_on_setting_changed) m_on_setting_changed(Control_Wired_Btn_Home, 0);
                return true;
            case 'L':
            case 'l':
                if (m_on_setting_changed) m_on_setting_changed(Control_Wired_Btn_Lock, 0);
                return true;
            case VK_OEM_4: // '['
                if (m_on_setting_changed) m_on_setting_changed(Control_Wired_Btn_VolDown, 0);
                return true;
            case VK_OEM_6: // ']'
                if (m_on_setting_changed) m_on_setting_changed(Control_Wired_Btn_VolUp, 0);
                return true;
            case 'S':
            case 's':
                if (m_on_setting_changed) m_on_setting_changed(Control_Wired_Btn_Siri, 0);
                return true;
            default:
                break;
            }
        }
    }

    float max_scroll = std::max(0.0f, m_content_height - m_viewport_height);
    if (max_scroll <= 0.0f) return false;

    float cur_scroll = GetCurrentScrollY(state.active_tab);
    float new_scroll = cur_scroll;

    switch (vk) {
    case VK_UP:
        new_scroll -= 40.0f;
        break;
    case VK_DOWN:
        new_scroll += 40.0f;
        break;
    case VK_PRIOR:
        new_scroll -= m_viewport_height * 0.85f;
        break;
    case VK_NEXT:
        new_scroll += m_viewport_height * 0.85f;
        break;
    case VK_HOME:
        new_scroll = 0.0f;
        break;
    case VK_END:
        new_scroll = max_scroll;
        break;
    default:
        return false;
    }

    new_scroll = std::clamp(new_scroll, 0.0f, max_scroll);
    if (new_scroll != cur_scroll) {
        SetCurrentScrollY(state.active_tab, new_scroll);
        if (state.open_dropdown != 0) {
            state.open_dropdown = 0;
        }
        return true;
    }
    return false;
}

void MainWindowView::Render(const UiState& state) noexcept {
    if (!m_renderer.BeginDraw()) return;

    m_clickables.clear();
    m_renderer.Clear(colors::Background);

    m_renderer.GetDpi(&m_dpi_x, &m_dpi_y);
    float total_w = m_renderer.WidthDip();
    float total_h = m_renderer.HeightDip();

    if (state.is_screen_only) {
        RenderScreenOnlyView(state, D2D1::RectF(0.0f, 0.0f, total_w, total_h));
        m_renderer.EndDraw();
        return;
    }

    m_tier = CalculateTier(total_w);

    float header_h = metrics::HeaderHeight;
    float status_h = metrics::StatusBarHeight;
    float body_y   = header_h;
    float body_h   = total_h - header_h - status_h;

    // 1. Header (strict clip to header rect)
    RenderHeader(state, total_w);

    // Responsive sidebar width based on tier
    float sidebar_w = 200.0f;
    if (m_tier == LayoutTier::Medium) {
        sidebar_w = 68.0f;
    } else if (m_tier == LayoutTier::Small) {
        sidebar_w = 60.0f;
    }

    // 2. Left Sidebar Navigation
    RenderSidebar(state, body_y, body_h);

    // 3. Main Content Area (Tab-dependent)
    D2D1_RECT_F viewport_rc = D2D1::RectF(sidebar_w, body_y, total_w, body_y + body_h);
    m_viewport_left   = viewport_rc.left;
    m_viewport_top    = viewport_rc.top;
    m_viewport_right  = viewport_rc.right;
    m_viewport_bottom = viewport_rc.bottom;
    m_viewport_height = body_h;

    float cur_scroll = GetCurrentScrollY(state.active_tab);

    float content_h = body_h;
    if (state.active_tab == NavTab::Video) {
        float content_w = total_w - sidebar_w - 40.0f;
        bool two_col = (content_w >= 700.0f);
        float delivery_h = state.streaming_mode == 3 ? 188.0f : 108.0f;
        float col1_h = 78.0f + (state.receiver_quality_pending ? 162.0f : 140.0f) + delivery_h + 216.0f + 140.0f;
        float col2_h = 364.0f + (state.advanced_color_expanded ? 186.0f : 40.0f);
        content_h = 62.0f + (two_col ? std::max(col1_h, col2_h) : (col1_h + col2_h)) + 32.0f;
    } else if (state.active_tab == NavTab::Audio) {
        content_h = std::max(body_h, 560.0f);
    } else if (state.active_tab == NavTab::Color) {
        content_h = std::max(body_h, 560.0f);
    } else if (state.active_tab == NavTab::Settings) {
        content_h = std::max(body_h, 520.0f);
    } else if (state.active_tab == NavTab::Diagnostics) {
        content_h = std::max(body_h, 560.0f);
    } else if (state.active_tab == NavTab::Mirror) {
        content_h = std::max(body_h, state.connection_mode == 1 ? 870.0f : 710.0f);
    }
    m_content_height = content_h;

    float max_scroll = std::max(0.0f, content_h - body_h);
    cur_scroll = std::clamp(cur_scroll, 0.0f, max_scroll);
    SetCurrentScrollY(state.active_tab, cur_scroll);

    m_renderer.PushClip(viewport_rc);

    if (cur_scroll > 0.0f) {
        m_renderer.Target()->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -cur_scroll));
    }

    float content_y = body_y;
    if (state.show_crash_banner) {
        float banner_h = 36.0f;
        D2D1_RECT_F banner_rc = D2D1::RectF(sidebar_w + 16.0f, content_y + 8.0f, total_w - 16.0f, content_y + 8.0f + banner_h);
        RenderCrashBanner(state, banner_rc);
        content_y += banner_h + 12.0f;
    }

    if (state.active_tab == NavTab::Mirror) {
        RenderConnectionModeSelector(state,
            D2D1::RectF(sidebar_w, content_y, total_w, content_y + 84.0f));
        const float mirror_y = content_y + 84.0f;
        const float mirror_h = content_h - 84.0f;
            if (state.connection_mode == 1) {
                RenderWiredMirrorView(state, D2D1::RectF(sidebar_w, mirror_y, total_w, mirror_y + mirror_h));
            } else if (m_tier == LayoutTier::Large) {
                float right_w   = 340.0f;
                float preview_w = total_w - sidebar_w - right_w;

                D2D1_RECT_F preview_rc = D2D1::RectF(sidebar_w, mirror_y, sidebar_w + preview_w, mirror_y + mirror_h);
                RenderDevicePreview(state, preview_rc);

                D2D1_RECT_F right_rc = D2D1::RectF(sidebar_w + preview_w, mirror_y, total_w, mirror_y + mirror_h);
                RenderRightStack(state, right_rc);
            } else if (m_tier == LayoutTier::Medium) {
                float right_w   = 310.0f;
                float preview_w = total_w - sidebar_w - right_w;

                D2D1_RECT_F preview_rc = D2D1::RectF(sidebar_w, mirror_y, sidebar_w + preview_w, mirror_y + mirror_h);
                RenderDevicePreview(state, preview_rc);

                D2D1_RECT_F right_rc = D2D1::RectF(sidebar_w + preview_w, mirror_y, total_w, mirror_y + mirror_h);
                RenderRightStack(state, right_rc);
            } else {
                // Small tier (< 900 DIP): 2-column or responsive stacked layout
                float content_w = total_w - sidebar_w;
                if (content_w >= 540.0f) {
                    float right_w = std::min(300.0f, content_w * 0.48f);
                    float preview_w = content_w - right_w;

                    D2D1_RECT_F preview_rc = D2D1::RectF(sidebar_w, mirror_y, sidebar_w + preview_w, mirror_y + mirror_h);
                    RenderDevicePreview(state, preview_rc);

                    D2D1_RECT_F right_rc = D2D1::RectF(sidebar_w + preview_w, mirror_y, total_w, mirror_y + mirror_h);
                    RenderRightStack(state, right_rc);
                } else {
                    // Stacked single column if extremely compact
                    D2D1_RECT_F preview_rc = D2D1::RectF(sidebar_w, mirror_y, total_w, mirror_y + mirror_h);
                    RenderDevicePreview(state, preview_rc);
                }
            }
    } else {
        D2D1_RECT_F full_content_rc = D2D1::RectF(sidebar_w, content_y, total_w, content_y + body_h);
        if (state.active_tab == NavTab::Video) {
            RenderVideoView(state, full_content_rc);
        } else if (state.active_tab == NavTab::Audio) {
            RenderAudioPage(state, full_content_rc);
        } else if (state.active_tab == NavTab::Color) {
            RenderColorView(state, full_content_rc);
        } else if (state.active_tab == NavTab::Settings) {
            RenderSettingsView(state, full_content_rc);
        } else if (state.active_tab == NavTab::Diagnostics) {
            RenderDiagnosticsView(state, full_content_rc);
        }
    }

    if (cur_scroll > 0.0f) {
        m_renderer.Target()->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    RenderScrollbar(cur_scroll, m_content_height, m_viewport_height, viewport_rc);

    m_renderer.PopClip();

    // 4. Bottom Status Bar
    RenderStatusBar(state, total_h - status_h, total_w);

    // 5. Floating Dropdown Overlay (rendered on top of all views)
    if (state.open_dropdown != 0) {
        RenderDropdownOverlay(state);
    }

    m_renderer.EndDraw();
}

void MainWindowView::RenderHeader(const UiState& state, float width) noexcept {
    float h = metrics::HeaderHeight;
    D2D1_RECT_F header_rc = D2D1::RectF(0.0f, 0.0f, width, h);
    m_renderer.PushClip(header_rc);

    // Header subtle divider line
    m_renderer.DrawLine(
        D2D1::Point2F(0.0f, h - 1.0f),
        D2D1::Point2F(width, h - 1.0f),
        m_renderer.BrushCardBorder(), 1.0f
    );

    // Embedded Brand Logo (WIC bitmap from the clean asset)
    D2D1_RECT_F logo_rc = D2D1::RectF(16.0f, 12.0f, 52.0f, 48.0f);
    m_renderer.DrawLogo(logo_rc);

    // Brand Title & Subtitle
    D2D1_RECT_F title_rc = D2D1::RectF(60.0f, 10.0f, 240.0f, 34.0f);
    m_renderer.DrawTextSimple(
        L"Duwn Mirror", m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    if (width >= 850.0f) {
        D2D1_RECT_F sub_rc = D2D1::RectF(61.0f, 33.0f, 360.0f, 50.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Header_Subtitle), m_renderer.FontSmall(), sub_rc,
            m_renderer.BrushTextAccent(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );
    }

    // Right side Header Badges
    float right_margin = width - 16.0f;

    // Version Badge (Clickable to About)
    D2D1_RECT_F ver_badge = D2D1::RectF(right_margin - 88.0f, 17.0f, right_margin, 43.0f);
    RegisterClickable(ver_badge, Control_Btn_HeaderAbout, loc::Get(loc::S::Header_AboutTooltip));
    std::wstring ver_tag = std::format(L"v{}", DUWN_VERSION_STRING_W);
    m_renderer.DrawBadge(
        ver_badge, ver_tag,
        D2D1::ColorF(0.231f, 0.510f, 0.965f, 0.12f),
        colors::BrandBlue, true
    );

    // Connection Status Pill (wider to fit full session state labels cleanly)
    D2D1_RECT_F status_pill = D2D1::RectF(right_margin - 250.0f, 17.0f, right_margin - 96.0f, 43.0f);
    D2D1_COLOR_F status_color = colors::StatusBlue;
    std::wstring_view status_text = loc::Get(loc::S::Status_Ready);
    const bool has_video_evidence = (state.total_frames_presented > 0 || state.render_fps > 0.0 || state.decoded_fps > 0.0 || state.width > 0);

    if (state.connection_mode == 1) {
        if (state.status == ConnectionStatus::Streaming || state.status == ConnectionStatus::Connected) {
            if (has_video_evidence) {
                status_color = colors::StatusGreen;
                status_text  = loc::Get(loc::S::Status_Streaming);
            } else {
                status_color = colors::StatusAmber;
                status_text  = loc::Get(loc::S::Status_ConnectedWaitingVideo);
            }
        } else if (state.status == ConnectionStatus::Connecting) {
            status_color = colors::StatusAmber;
            status_text  = loc::Get(loc::S::Status_Connecting);
        } else if (state.wired.network_up) {
            status_color = colors::StatusGreen;
            status_text  = loc::Get(loc::S::Mirror_Empty_Wired_Ready);
        } else if (state.wired.usb_interface_count > 0) {
            status_color = colors::StatusAmber;
            status_text  = loc::Get(loc::S::Mirror_Empty_Wired_Preparing);
        } else {
            status_color = colors::StatusBlue;
            status_text  = loc::Get(loc::S::Mirror_Empty_Wired_NoCable);
        }
    } else {
        switch (state.status) {
        case ConnectionStatus::Ready:
            status_color = colors::StatusBlue;
            status_text  = loc::Get(loc::S::Status_Ready);
            break;
        case ConnectionStatus::Connecting:
            status_color = colors::StatusAmber;
            status_text  = (state.device_name != L"—" && !state.device_name.empty())
                           ? loc::Get(loc::S::Status_Connecting)
                           : loc::Get(loc::S::Status_StartingAirPlay);
            break;
        case ConnectionStatus::Connected:
            status_color = colors::StatusAmber;
            status_text  = loc::Get(loc::S::Status_ConnectedWaitingVideo);
            break;
        case ConnectionStatus::Streaming:
            if (has_video_evidence) {
                status_color = colors::StatusGreen;
                status_text  = loc::Get(loc::S::Status_Streaming);
            } else {
                status_color = colors::StatusAmber;
                status_text  = loc::Get(loc::S::Status_ConnectedWaitingVideo);
            }
            break;
        case ConnectionStatus::Paused:
            status_color = colors::StatusAmber;
            status_text  = loc::Get(loc::S::Status_Paused);
            break;
        case ConnectionStatus::Reconnecting:
            status_color = colors::StatusAmber;
            status_text  = loc::Get(loc::S::Status_Reconnecting);
            break;
        case ConnectionStatus::Disconnected:
            status_color = colors::StatusBlue;
            status_text  = loc::Get(loc::S::Status_Disconnected);
            break;
        case ConnectionStatus::Error:
            status_color = colors::StatusRed;
            status_text  = loc::Get(loc::S::Status_AirPlayRuntimeError);
            break;
        }
    }

    D2D1_COLOR_F pill_bg = status_color;
    pill_bg.a = 0.14f;
    m_renderer.FillRoundedRect(status_pill, metrics::PillRadius, m_renderer.BrushCardBorder());
    m_renderer.DrawRoundedRect(status_pill, metrics::PillRadius, m_renderer.BrushCardBorder(), 1.0f);

    // Glowing status dot inside pill
    D2D1_POINT_2F dot_pt = D2D1::Point2F(status_pill.left + 14.0f, 30.0f);
    m_renderer.DrawStatusDot(dot_pt, 4.0f, status_color, true);

    D2D1_RECT_F pill_text_rc = D2D1::RectF(status_pill.left + 24.0f, status_pill.top, status_pill.right - 8.0f, status_pill.bottom);
    m_renderer.DrawTextSimple(
        status_text, m_renderer.FontSmallBold(), pill_text_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    m_renderer.PopClip();
}
void MainWindowView::RenderSidebar(const UiState& state, float top, float height) noexcept {
    float w = (m_tier == LayoutTier::Large) ? 200.0f : (m_tier == LayoutTier::Medium ? 68.0f : 60.0f);
    D2D1_RECT_F sidebar_rc = D2D1::RectF(0.0f, top, w, top + height);
    m_renderer.PushClip(sidebar_rc);

    // Darker sidebar background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> side_brush;
    m_renderer.Target()->CreateSolidColorBrush(colors::SidebarBg, side_brush.GetAddressOf());
    if (side_brush) {
        m_renderer.FillRect(sidebar_rc, side_brush.Get());
    }

    // Right border
    m_renderer.DrawLine(
        D2D1::Point2F(w - 1.0f, top),
        D2D1::Point2F(w - 1.0f, top + height),
        m_renderer.BrushCardBorder(), 1.0f
    );

    // Navigation Menu Items (Approved Workspace: Mirror, Video, Audio, Color, Settings; Diagnostics at bottom)
    struct NavItem {
        NavTab tab;
        ControlId ctrl_id;
        std::wstring_view label;
        IconType icon;
    };

    const NavItem top_nav_items[] = {
        { NavTab::Mirror,   Control_Nav_Mirror,   loc::Get(loc::S::Nav_Mirror),   IconType::Phone },
        { NavTab::Video,    Control_Nav_Video,    loc::Get(loc::S::Nav_Video),    IconType::Monitor },
        { NavTab::Audio,    Control_Nav_Audio,    loc::Get(loc::S::Nav_Audio),    IconType::Speaker },
        { NavTab::Color,    Control_Nav_Color,    loc::Get(loc::S::Nav_Color),    IconType::Palette },
        { NavTab::Settings, Control_Nav_Settings, loc::Get(loc::S::Nav_Settings), IconType::Settings },
    };

    float item_h = 40.0f;

    auto RenderItem = [&](const NavItem& item, float cur_y) {
        bool is_active = (state.active_tab == item.tab);
        bool is_hovered = (state.hovered_control == item.ctrl_id);

        if (m_tier == LayoutTier::Large) {
            float item_margin = 12.0f;
            D2D1_RECT_F item_rc = D2D1::RectF(item_margin, cur_y, w - item_margin, cur_y + item_h);
            RegisterClickable(item_rc, item.ctrl_id, item.label);

            if (is_active) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> active_brush;
                m_renderer.Target()->CreateSolidColorBrush(colors::NavActiveBg, active_brush.GetAddressOf());
                if (active_brush) {
                    m_renderer.FillRoundedRect(item_rc, metrics::ButtonRadius, active_brush.Get());
                    m_renderer.DrawRoundedRect(item_rc, metrics::ButtonRadius, m_renderer.BrushBrandBlue(), 1.2f);
                }

                D2D1_RECT_F indicator = D2D1::RectF(item_margin + 2.0f, cur_y + 8.0f, item_margin + 5.0f, cur_y + item_h - 8.0f);
                m_renderer.FillRoundedRect(indicator, 1.5f, m_renderer.BrushBrandBlue());
            } else if (is_hovered) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hover_brush;
                m_renderer.Target()->CreateSolidColorBrush(colors::NavHoverBg, hover_brush.GetAddressOf());
                if (hover_brush) {
                    m_renderer.FillRoundedRect(item_rc, metrics::ButtonRadius, hover_brush.Get());
                }
            }

            D2D1_RECT_F icon_rc = D2D1::RectF(item_margin + 12.0f, cur_y + 11.0f, item_margin + 30.0f, cur_y + item_h - 11.0f);
            D2D1_COLOR_F icon_color = is_active ? colors::BrandBlue : (is_hovered ? colors::TextPrimary : colors::TextSecondary);
            m_renderer.DrawIcon(item.icon, icon_rc, icon_color, 1.6f);

            D2D1_RECT_F text_rc = D2D1::RectF(item_margin + 40.0f, cur_y, w - item_margin, cur_y + item_h);
            IDWriteTextFormat* fmt = is_active ? m_renderer.FontBodyBold() : m_renderer.FontBody();
            ID2D1Brush* text_brush = is_active ? m_renderer.BrushTextPrimary() :
                                     (is_hovered ? m_renderer.BrushTextPrimary() : m_renderer.BrushTextSecondary());

            m_renderer.DrawTextSimple(item.label, fmt, text_rc, text_brush,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            float item_sz = 38.0f;
            float item_x = (w - item_sz) * 0.5f;
            D2D1_RECT_F item_rc = D2D1::RectF(item_x, cur_y, item_x + item_sz, cur_y + item_sz);
            RegisterClickable(item_rc, item.ctrl_id, item.label);

            if (is_active) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> active_brush;
                m_renderer.Target()->CreateSolidColorBrush(colors::NavActiveBg, active_brush.GetAddressOf());
                if (active_brush) {
                    m_renderer.FillRoundedRect(item_rc, metrics::ButtonRadius, active_brush.Get());
                    m_renderer.DrawRoundedRect(item_rc, metrics::ButtonRadius, m_renderer.BrushBrandBlue(), 1.2f);
                }

                D2D1_RECT_F indicator = D2D1::RectF(2.0f, cur_y + 8.0f, 5.0f, cur_y + item_sz - 8.0f);
                m_renderer.FillRoundedRect(indicator, 1.5f, m_renderer.BrushBrandBlue());
            } else if (is_hovered) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hover_brush;
                m_renderer.Target()->CreateSolidColorBrush(colors::NavHoverBg, hover_brush.GetAddressOf());
                if (hover_brush) {
                    m_renderer.FillRoundedRect(item_rc, metrics::ButtonRadius, hover_brush.Get());
                }
            }

            D2D1_RECT_F icon_rc = D2D1::RectF(item_x + 9.0f, cur_y + 9.0f, item_x + 29.0f, cur_y + 29.0f);
            D2D1_COLOR_F icon_color = is_active ? colors::BrandBlue : (is_hovered ? colors::TextPrimary : colors::TextSecondary);
            m_renderer.DrawIcon(item.icon, icon_rc, icon_color, 1.6f);
        }
    };

    float nav_y = top + 16.0f;

    for (const auto& item : top_nav_items) {
        RenderItem(item, nav_y);
        nav_y += item_h + 6.0f;
    }

    // Bottom section: Chẩn đoán (Diagnostics at bottom)
    float diag_y = top + height - item_h - 16.0f;
    if (diag_y > nav_y + 10.0f) {
        m_renderer.DrawLine(
            D2D1::Point2F(14.0f, diag_y - 8.0f),
            D2D1::Point2F(w - 14.0f, diag_y - 8.0f),
            m_renderer.BrushCardBorder(), 1.0f
        );
        const NavItem bottom_item = {
            NavTab::Diagnostics, Control_Nav_Diagnostics, loc::Get(loc::S::Nav_Diagnostics), IconType::Performance
        };
        RenderItem(bottom_item, diag_y);
    }

    m_renderer.PopClip();
}

void MainWindowView::RenderConnectionModeSelector(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const D2D1_RECT_F card = D2D1::RectF(area.left + 12.0f, area.top + 8.0f,
        area.right - 12.0f, area.bottom - 4.0f);
    m_renderer.DrawCard(card, false, metrics::CardRadius);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Mode_Title), m_renderer.FontSmallBold(),
        D2D1::RectF(card.left + 18.0f, card.top + 8.0f, card.right - 18.0f, card.top + 27.0f),
        m_renderer.BrushTextMuted());
    const float available = card.right - card.left;
    const float button_w = std::min(185.0f, (available - 42.0f) * 0.5f);
    const float start = card.right - 14.0f - button_w * 2.0f - 8.0f;
    const D2D1_RECT_F wireless = D2D1::RectF(start, card.top + 30.0f,
        start + button_w, card.bottom - 8.0f);
    const D2D1_RECT_F wired = D2D1::RectF(wireless.right + 8.0f, wireless.top,
        wireless.right + 8.0f + button_w, wireless.bottom);
    RegisterClickable(wireless, Control_Mode_Wireless, loc::Get(loc::S::Mode_Wireless), true);
    RegisterClickable(wired, Control_Mode_Wired, loc::Get(loc::S::Mode_Wired), true);
    m_renderer.DrawButton(wireless, loc::Get(loc::S::Mode_Wireless), state.connection_mode == 0,
        state.hovered_control == Control_Mode_Wireless, state.pressed_control == Control_Mode_Wireless,
        IconType::Wifi);
    m_renderer.DrawButton(wired, loc::Get(loc::S::Mode_Wired), state.connection_mode == 1,
        state.hovered_control == Control_Mode_Wired, state.pressed_control == Control_Mode_Wired,
        IconType::Phone);
}

void MainWindowView::RenderWiredMirrorView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float left = area.left + 24.0f;
    const float right = area.right - 24.0f;
    float cur_y = area.top + 12.0f;

    const auto& wired = state.wired;
    const bool is_connected = (wired.usb_interface_count > 0 || state.control_state == wired::WiredControlState::Ready || state.control_state == wired::WiredControlState::Active);
    const std::wstring dev_name = !state.control_device_name.empty() && state.control_device_name != L"—"
        ? state.control_device_name : !wired.device_name.empty() ? wired.device_name
        : (is_connected ? L"iPhone" : loc::Get(loc::S::Wired_Unknown));

    // -------------------------------------------------------------
    // CARD 1: WIRED DEVICE & HARDWARE STATUS
    // -------------------------------------------------------------
    D2D1_RECT_F status_card = D2D1::RectF(left, cur_y, right, cur_y + 190.0f);
    m_renderer.DrawCard(status_card, false, metrics::CardRadius);

    // Badges: Wired USB
    m_renderer.DrawBadge(D2D1::RectF(left + 18.0f, cur_y + 16.0f, left + 180.0f, cur_y + 42.0f),
        loc::Get(loc::S::Mode_Wired), D2D1::ColorF(0.486f, 0.227f, 0.929f, 0.15f), colors::BrandPurple, true);

#ifdef DUWN_ENABLE_DEVICE_CONTROL
    const wchar_t* ctrl_status_text = loc::Get(loc::S::Wired_ControlDisabled);
    D2D1_COLOR_F ctrl_bg = D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.15f);
    D2D1_COLOR_F ctrl_fg = colors::TextSecondary;

    switch (state.control_state) {
    case wired::WiredControlState::Disabled:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlDisabled);
        break;
    case wired::WiredControlState::Starting:
    case wired::WiredControlState::ConnectingRsd:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlStarting);
        ctrl_bg = D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.15f);
        ctrl_fg = colors::StatusAmber;
        break;
    case wired::WiredControlState::OpeningHid:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlOpeningHid);
        ctrl_bg = D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.15f);
        ctrl_fg = colors::StatusAmber;
        break;
    case wired::WiredControlState::Ready:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlReady);
        ctrl_bg = D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.15f);
        ctrl_fg = colors::StatusGreen;
        break;
    case wired::WiredControlState::Active:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlActive);
        ctrl_bg = D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.25f);
        ctrl_fg = colors::StatusGreen;
        break;
    case wired::WiredControlState::Error:
        ctrl_status_text = loc::Get(loc::S::Wired_ControlError);
        ctrl_bg = D2D1::ColorF(0.937f, 0.267f, 0.267f, 0.15f);
        ctrl_fg = colors::StatusRed;
        break;
    default:
        break;
    }

    m_renderer.DrawBadge(D2D1::RectF(left + 190.0f, cur_y + 16.0f, left + 430.0f, cur_y + 42.0f),
        ctrl_status_text, ctrl_bg, ctrl_fg, true);
#endif

    // Title / Description
    m_renderer.DrawTextSimple(loc::Get(loc::S::Wired_Description), m_renderer.FontBody(),
        D2D1::RectF(left + 18.0f, cur_y + 48.0f, right - 18.0f, cur_y + 70.0f),
        m_renderer.BrushTextSecondary());

    // Device and media/control status from live USB and AirPlay state.
    const std::wstring usb_val = is_connected ? loc::Get(loc::S::Wired_DeviceDetected) : loc::Get(loc::S::Wired_NoCable);
    const std::wstring network_val = wired.network_up ? L"Ready" : L"Preparing USB connection...";
    const std::wstring video_val = state.status == ConnectionStatus::Streaming ? L"Streaming"
        : state.status == ConnectionStatus::Connecting ? L"Connecting" : wired.network_up ? L"Ready" : L"Unavailable";
#ifdef DUWN_ENABLE_DEVICE_CONTROL
    const std::wstring control_val = state.control_state == wired::WiredControlState::Active ? L"Active"
        : state.control_state == wired::WiredControlState::Ready ? L"Ready"
        : state.device_control_enabled ? L"Connecting" : L"Off";
#endif
    const std::wstring ios_val = !state.control_ios_version.empty() && state.control_ios_version != L"—"
        ? state.control_ios_version : loc::Get(loc::S::Wired_Unknown);

    struct Field { const wchar_t* label; const std::wstring* value; };
#ifdef DUWN_ENABLE_DEVICE_CONTROL
    const Field fields[] = {
        {L"Device", &dev_name},
        {L"iOS Version", &ios_val},
        {L"USB", &usb_val},
        {L"USB Network", &network_val},
        {L"Screen Mirroring", &video_val},
        {L"Device Control", &control_val}
    };
    const int num_fields = 6;
#else
    const Field fields[] = {
        {L"Device", &dev_name},
        {L"iOS Version", &ios_val},
        {L"USB", &usb_val},
        {L"USB Network", &network_val},
        {L"Screen Mirroring", &video_val}
    };
    const int num_fields = 5;
#endif

    const float col_w = (right - left - 54.0f) / 3.0f;
    for (int i = 0; i < num_fields; ++i) {
        int col = i % 3;
        int row = i / 3;
        float fx = left + 18.0f + col * (col_w + 18.0f);
        float fy = cur_y + 80.0f + row * 45.0f;
        m_renderer.DrawTextSimple(fields[i].label, m_renderer.FontSmall(),
            D2D1::RectF(fx, fy, fx + col_w, fy + 18.0f), m_renderer.BrushTextMuted());
        m_renderer.DrawTextSimple(*fields[i].value, m_renderer.FontSmallBold(),
            D2D1::RectF(fx, fy + 18.0f, fx + col_w, fy + 38.0f), m_renderer.BrushTextPrimary());
    }

    cur_y += 202.0f;

#ifdef DUWN_ENABLE_DEVICE_CONTROL
    // -------------------------------------------------------------
    // "Control connected — video unavailable" Banner
    // -------------------------------------------------------------
    if (state.wired_needs_mirroring_reconnect ||
        ((state.control_state == wired::WiredControlState::Ready ||
         state.control_state == wired::WiredControlState::Active) &&
        state.status != ConnectionStatus::Streaming)) {
        D2D1_RECT_F no_video_rc = D2D1::RectF(left, cur_y, right, cur_y + 44.0f);
        m_renderer.DrawCard(no_video_rc, false, metrics::CardRadius);
        m_renderer.DrawBadge(D2D1::RectF(left + 12.0f, cur_y + 10.0f, left + 40.0f, cur_y + 34.0f),
            L"!", D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.25f), colors::StatusAmber, true);
        m_renderer.DrawTextSimple(loc::Get(state.wired_needs_mirroring_reconnect
            ? loc::S::Wired_ReconnectMirroring : loc::S::Wired_ControlConnectedNoVideo), m_renderer.FontBodyBold(),
            D2D1::RectF(left + 48.0f, cur_y + 12.0f, right - 18.0f, cur_y + 34.0f),
            m_renderer.BrushTextPrimary());
        cur_y += 52.0f;
    }

    // -------------------------------------------------------------
    // CARD 2: DEVICE CONTROL (HARDWARE BUTTON BAR & ACTIONS)
    // -------------------------------------------------------------
    D2D1_RECT_F ctrl_card = D2D1::RectF(left, cur_y, right, cur_y + 175.0f);
    m_renderer.DrawCard(ctrl_card, false, metrics::CardRadius);

    // Header & Enable/Disable Toggle
    m_renderer.DrawTextSimple(loc::Get(loc::S::Wired_DeviceControl), m_renderer.FontSmallBold(),
        D2D1::RectF(left + 18.0f, cur_y + 14.0f, left + 300.0f, cur_y + 38.0f),
        m_renderer.BrushBrandPurple());

    bool control_on = (state.control_state == wired::WiredControlState::Ready ||
                       state.control_state == wired::WiredControlState::Active ||
                       state.control_state == wired::WiredControlState::Starting ||
                       state.control_state == wired::WiredControlState::ConnectingRsd ||
                       state.control_state == wired::WiredControlState::OpeningHid);

    const wchar_t* toggle_label = control_on
        ? loc::Get(loc::S::Wired_DisableControl)
        : loc::Get(loc::S::Wired_EnableControl);

    D2D1_RECT_F toggle_btn_rc = D2D1::RectF(right - 180.0f, cur_y + 10.0f, right - 18.0f, cur_y + 42.0f);
    RegisterClickable(toggle_btn_rc, Control_Wired_ToggleControl, toggle_label, true);
    m_renderer.DrawButton(toggle_btn_rc, toggle_label, !control_on,
        state.hovered_control == Control_Wired_ToggleControl,
        state.pressed_control == Control_Wired_ToggleControl);

    // Hardware Button Bar: Home, Power, Vol-, Vol+, Mute, Siri
    float btn_bar_y = cur_y + 50.0f;
    const float btn_gap = 10.0f;
    const float btn_w = (right - left - 36.0f - btn_gap * 5.0f) / 6.0f;

    struct HwBtn { int id; loc::S text; };
    const HwBtn hw_buttons[] = {
        {Control_Wired_Btn_Home, loc::S::Wired_BtnHome},
        {Control_Wired_Btn_Lock, loc::S::Wired_BtnLock},
        {Control_Wired_Btn_VolDown, loc::S::Wired_BtnVolDown},
        {Control_Wired_Btn_VolUp, loc::S::Wired_BtnVolUp},
        {Control_Wired_Btn_Mute, loc::S::Wired_BtnMute},
        {Control_Wired_Btn_Siri, loc::S::Wired_BtnSiri}
    };

    for (int i = 0; i < 6; ++i) {
        float bx = left + 18.0f + i * (btn_w + btn_gap);
        D2D1_RECT_F brc = D2D1::RectF(bx, btn_bar_y, bx + btn_w, btn_bar_y + 40.0f);
        RegisterClickable(brc, hw_buttons[i].id, loc::Get(hw_buttons[i].text), true);
        m_renderer.DrawButton(brc, loc::Get(hw_buttons[i].text), false,
            state.hovered_control == hw_buttons[i].id,
            state.pressed_control == hw_buttons[i].id);
    }

    // Keyboard Shortcuts Hint Line
    m_renderer.DrawTextSimple(loc::Get(loc::S::Wired_ShortcutsHint), m_renderer.FontSmall(),
        D2D1::RectF(left + 18.0f, cur_y + 98.0f, right - 18.0f, cur_y + 120.0f),
        m_renderer.BrushTextMuted());

    // Latency Telemetry Line
    std::wstring tele_text = std::format(
        L"Events: {} | Latency P50: {:.1f}ms | P95: {:.1f}ms | Avg: {:.1f}ms",
        state.control_events_sent,
        state.control_latency_p50_ms,
        state.control_latency_p95_ms,
        state.control_latency_avg_ms);
    m_renderer.DrawTextSimple(tele_text, m_renderer.FontSmallBold(),
        D2D1::RectF(left + 18.0f, cur_y + 126.0f, right - 18.0f, cur_y + 152.0f),
        m_renderer.BrushBrandBlue());

    cur_y += 187.0f;
#endif

    // -------------------------------------------------------------
    // CARD 3: CONNECTION GUIDE (RELEASE) / INTERACTIVE SCREEN CANVAS (DEV)
    // -------------------------------------------------------------
#ifdef DUWN_ENABLE_DEVICE_CONTROL
    const float preview_h = 300.0f;
    const float preview_w = preview_h * (9.0f / 19.5f);
    float center_x = (left + right) * 0.5f;

    D2D1_RECT_F preview_card_rc = D2D1::RectF(left, cur_y, right, cur_y + preview_h + 30.0f);
    m_renderer.DrawCard(preview_card_rc, false, metrics::CardRadius);

    D2D1_RECT_F phone_screen = D2D1::RectF(
        center_x - preview_w * 0.5f,
        cur_y + 15.0f,
        center_x + preview_w * 0.5f,
        cur_y + 15.0f + preview_h
    );
    m_wired_preview_rect = phone_screen;

    // Draw Phone Frame (rounded bezel)
    m_renderer.DrawInset(phone_screen, 16.0f);

    // Phone notch / dynamic island bar at top
    D2D1_RECT_F island_rc = D2D1::RectF(center_x - 24.0f, phone_screen.top + 8.0f, center_x + 24.0f, phone_screen.top + 20.0f);
    m_renderer.DrawCard(island_rc, true, 6.0f);

    // Phone home bar indicator at bottom
    D2D1_RECT_F home_bar_rc = D2D1::RectF(center_x - 30.0f, phone_screen.bottom - 12.0f, center_x + 30.0f, phone_screen.bottom - 7.0f);
    m_renderer.DrawCard(home_bar_rc, true, 2.5f);

    // Text instructions inside screen
    m_renderer.DrawTextSimple(
        control_on ? L"Touch Screen\n(Click / Drag to control)" : L"Touch Screen\n(Enable Control above)",
        m_renderer.FontSmallBold(),
        D2D1::RectF(phone_screen.left + 8.0f, phone_screen.top + 90.0f, phone_screen.right - 8.0f, phone_screen.top + 160.0f),
        control_on ? m_renderer.BrushBrandPurple() : m_renderer.BrushTextMuted(),
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Side instructions / connection guide
    float guide_left = phone_screen.right + 24.0f;
    if (guide_left < right - 180.0f) {
        m_renderer.DrawTextSimple(loc::Get(loc::S::Wired_StepsTitle), m_renderer.FontSmallBold(),
            D2D1::RectF(guide_left, cur_y + 20.0f, right - 18.0f, cur_y + 42.0f),
            m_renderer.BrushBrandBlue());
        const loc::S steps[] = {loc::S::Wired_Step1, loc::S::Wired_Step2, loc::S::Wired_Step3, loc::S::Wired_Step4};
        for (int i = 0; i < 4; ++i) {
            m_renderer.DrawTextSimple(loc::Get(steps[i]), m_renderer.FontSmall(),
                D2D1::RectF(guide_left, cur_y + 46.0f + i * 26.0f, right - 18.0f, cur_y + 70.0f + i * 26.0f),
                m_renderer.BrushTextSecondary());
        }
    }

    cur_y += preview_h + 40.0f;
#else
    // RELEASE MODE: Clean 4-step Wired Connection Guide Card
    const float guide_card_h = 160.0f;
    D2D1_RECT_F guide_card_rc = D2D1::RectF(left, cur_y, right, cur_y + guide_card_h);
    m_renderer.DrawCard(guide_card_rc, false, metrics::CardRadius);

    m_renderer.DrawTextSimple(loc::Get(loc::S::Wired_StepsTitle), m_renderer.FontBodyBold(),
        D2D1::RectF(left + 18.0f, cur_y + 16.0f, right - 18.0f, cur_y + 38.0f),
        m_renderer.BrushBrandBlue());

    const loc::S steps[] = {loc::S::Wired_Step1, loc::S::Wired_Step2, loc::S::Wired_Step3, loc::S::Wired_Step4};
    for (int i = 0; i < 4; ++i) {
        float step_y = cur_y + 46.0f + i * 26.0f;
        m_renderer.DrawTextSimple(loc::Get(steps[i]), m_renderer.FontSmall(),
            D2D1::RectF(left + 18.0f, step_y, right - 18.0f, step_y + 24.0f),
            m_renderer.BrushTextSecondary());
    }

    cur_y += guide_card_h + 16.0f;
#endif

    // -------------------------------------------------------------
    // BOTTOM ACTION BUTTONS
    // -------------------------------------------------------------
    const float b_gap = 8.0f;
    const float b_w = (right - left - b_gap * 3.0f) / 4.0f;
    const struct { int id; loc::S text; bool primary; } actions[] = {
        {Control_Wired_Refresh, loc::S::Wired_Refresh, false},
        {Control_Wired_Start, loc::S::Wired_Start, true},
        {Control_Btn_OutputWindow, loc::S::Right_ShowOutputWindow, false},
        {Control_Wired_Troubleshoot, loc::S::Wired_Troubleshoot, false}
    };
    for (int i = 0; i < 4; ++i) {
        D2D1_RECT_F btn_rc = D2D1::RectF(left + i * (b_w + b_gap), cur_y,
            left + i * (b_w + b_gap) + b_w, cur_y + 42.0f);
        RegisterClickable(btn_rc, actions[i].id, loc::Get(actions[i].text), true);
        m_renderer.DrawButton(btn_rc, actions[i].id == Control_Btn_OutputWindow && state.output_window_visible
            ? loc::Get(loc::S::Right_OutputWindowActive) : loc::Get(actions[i].text), actions[i].primary,
            state.hovered_control == actions[i].id, state.pressed_control == actions[i].id);
    }
}

void MainWindowView::RenderDevicePreview(const UiState& state, const D2D1_RECT_F& area) noexcept {
    m_renderer.PushClip(area);

    // Center Session Visualization (Option B: Decoupled stream status card)
    float pad = 24.0f;
    D2D1_RECT_F card_rc = D2D1::RectF(area.left + pad, area.top + pad, area.right - pad, area.bottom - pad);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    float cx = (card_rc.left + card_rc.right) * 0.5f;

    bool is_streaming = (state.status == ConnectionStatus::Connected ||
                          state.session_state == airplay::AirPlaySessionState::Connected ||
                          state.session_state == airplay::AirPlaySessionState::Streaming ||
                          state.session_state == airplay::AirPlaySessionState::Paused);

    if (is_streaming) {
        // STREAMING / CONNECTED / PAUSED STATE
        float top_y = card_rc.top + 28.0f;

        // Top Status Badge
        bool is_paused = (state.session_state == airplay::AirPlaySessionState::Paused);
        const bool has_video_evidence = (state.total_frames_presented > 0 || state.render_fps > 0.0 || state.decoded_fps > 0.0 || state.width > 0);
        D2D1_RECT_F badge_rc = D2D1::RectF(cx - 120.0f, top_y, cx + 120.0f, top_y + 26.0f);
        if (is_paused) {
            m_renderer.DrawBadge(badge_rc, loc::Get(loc::S::Status_PausedStaticScreen),
                                 D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.15f), colors::StatusAmber, true);
        } else if (has_video_evidence) {
            m_renderer.DrawBadge(badge_rc, loc::Get(loc::S::Status_AirPlayStreamActive),
                                 D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.15f), colors::StatusGreen, true);
        } else {
            m_renderer.DrawBadge(badge_rc, loc::Get(loc::S::Status_ConnectedWaitingVideo),
                                 D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.15f), colors::StatusAmber, true);
        }

        // Logo Emblem
        D2D1_RECT_F logo_rc = D2D1::RectF(cx - 36.0f, top_y + 38.0f, cx + 36.0f, top_y + 110.0f);
        m_renderer.DrawLogo(logo_rc);

        // Device Name Title
        std::wstring dev_text = state.device_name.empty() || state.device_name == L"—" ? loc::Get(loc::S::Mirror_AppleDevice) : state.device_name;
        D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 118.0f, card_rc.right - 20.0f, top_y + 146.0f);
        m_renderer.DrawTextSimple(
            dev_text, m_renderer.FontTitle(), title_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        // Subtitle / Architecture note
        std::wstring sub_str;
        if (!state.model_name.empty() && state.model_name != L"—") {
            std::wstring model_disp = (state.model_db_match == L"Fallback" && !state.product_type.empty() && state.product_type != L"—")
                ? std::format(L"{} ({})", state.model_name, state.product_type)
                : state.model_name;
            sub_str = std::format(L"{} • {} • Local RTP/UDP", model_disp, state.os_version != L"—" ? state.os_version : L"iOS");
        } else {
            sub_str = loc::Get(loc::S::Mirror_HardwareD3D11Note);
        }
        D2D1_RECT_F sub_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 146.0f, card_rc.right - 20.0f, top_y + 168.0f);
        m_renderer.DrawTextSimple(
            sub_str,
            m_renderer.FontSmall(), sub_rc,
            m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        // Telemetry Metrics Grid (3x2 tiles inside card)
        float grid_w = std::min(480.0f, card_rc.right - card_rc.left - 40.0f);
        float grid_left = cx - grid_w * 0.5f;
        float grid_top = top_y + 180.0f;
        float tile_w = (grid_w - 16.0f) / 3.0f;
        float tile_h = 58.0f;

        struct MetricBox {
            std::wstring_view label;
            std::wstring value;
            D2D1_COLOR_F color;
        };

        std::wstring res_str = (state.width > 0 && state.height > 0) ?
            std::format(L"{}×{}", state.width, state.height) : L"—";
        std::wstring fps_str = state.has_fps_sample ?
            (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) :
                (is_paused ? std::wstring(loc::Get(loc::S::Mirror_Static)) : L"0.0 FPS")) :
            std::wstring(loc::Get(loc::S::Common_NoData));
        std::wstring audio_str;
        if (state.audio_muted) {
            audio_str = loc::Get(loc::S::Audio_Mute);
        } else if (state.status == ConnectionStatus::Idle) {
            audio_str = std::format(L"48kHz • {}", loc::Get(loc::S::Status_Idle));
        } else if (state.audio_active) {
            audio_str = std::format(L"48kHz • {}", loc::Get(loc::S::Perf_Active));
        } else if (state.audio_rtp_packets > 0) {
            audio_str = std::format(L"48kHz • {}", loc::Get(loc::S::Status_Paused));
        } else {
            audio_str = std::format(L"48kHz • {}", loc::Get(loc::S::Status_Idle));
        }

        int64_t up = state.session_uptime_sec;
        std::wstring uptime_str = std::format(L"{:02d}:{:02d}:{:02d}", up / 3600, (up % 3600) / 60, up % 60);

        std::wstring drops_str = std::format(L"{}", state.dropped_frames);
        std::wstring lat_str = state.has_latency_sample ?
            (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) :
                L"< 1.0 ms") :
            std::wstring(loc::Get(loc::S::Common_NoData));

        MetricBox boxes[6] = {
            { loc::Get(loc::S::Mirror_Metric_Resolution), res_str, colors::BrandBlue },
            { loc::Get(loc::S::Mirror_Metric_FrameRate), fps_str, is_paused ? colors::StatusAmber : colors::StatusGreen },
            { loc::Get(loc::S::Mirror_Metric_AudioEngine), audio_str, colors::BrandPurple },
            { loc::Get(loc::S::Mirror_Metric_SessionUptime), uptime_str, colors::TextPrimary },
            { loc::Get(loc::S::Mirror_Metric_FrameDrops), drops_str, state.dropped_frames == 0 ? colors::StatusGreen : colors::StatusAmber },
            { loc::Get(loc::S::Mirror_Metric_PipelineLag), lat_str, colors::BrandCyan }
        };

        for (int i = 0; i < 6; ++i) {
            float bx = grid_left + (i % 3) * (tile_w + 8.0f);
            float by = grid_top + (i / 3) * (tile_h + 8.0f);
            D2D1_RECT_F tile_rc = D2D1::RectF(bx, by, bx + tile_w, by + tile_h);

            m_renderer.DrawInset(tile_rc, 8.0f);

            D2D1_RECT_F lbl_rc = D2D1::RectF(bx + 8.0f, by + 6.0f, bx + tile_w - 8.0f, by + 22.0f);
            m_renderer.DrawTextSimple(
                boxes[i].label, m_renderer.FontSmall(), lbl_rc,
                m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            );

            D2D1_RECT_F val_rc = D2D1::RectF(bx + 8.0f, by + 24.0f, bx + tile_w - 8.0f, by + 50.0f);
            m_renderer.DrawTextSimple(
                boxes[i].value, m_renderer.FontBodyBold(), val_rc,
                m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            );
        }

        // Action Controls: Open/Hide Output, Disconnect, Toolbar Toggle
        float action_y = grid_top + tile_h * 2.0f + 20.0f;
        float action_w = std::min(440.0f, card_rc.right - card_rc.left - 40.0f);
        float action_h = 40.0f;
        float btn_half_w = (action_w - 12.0f) * 0.5f;

        // 1. Output Button (Hiện/Ẩn output)
        D2D1_RECT_F out_act_rc = D2D1::RectF(cx - action_w * 0.5f, action_y, cx - action_w * 0.5f + btn_half_w, action_y + action_h);
        RegisterClickable(out_act_rc, Control_Btn_ToggleOutput, loc::Get(loc::S::Mirror_Btn_ShowOutput));
        bool oh = (state.hovered_control == Control_Btn_ToggleOutput);
        bool op = (state.pressed_control == Control_Btn_ToggleOutput);
        std::wstring_view out_act_lbl = state.output_window_visible
            ? loc::Get(loc::S::Mirror_Btn_HideOutput)
            : loc::Get(loc::S::Mirror_Btn_ShowOutput);
        m_renderer.DrawButton(out_act_rc, out_act_lbl, state.output_window_visible, oh, op, IconType::Monitor);

        // 2. Disconnect Button (Ngắt kết nối)
        D2D1_RECT_F disc_act_rc = D2D1::RectF(cx - action_w * 0.5f + btn_half_w + 12.0f, action_y, cx + action_w * 0.5f, action_y + action_h);
        RegisterClickable(disc_act_rc, Control_Btn_Disconnect, loc::Get(loc::S::Mirror_Btn_DisconnectSession));
        bool dh = (state.hovered_control == Control_Btn_Disconnect);
        bool dp = (state.pressed_control == Control_Btn_Disconnect);
        m_renderer.DrawButton(disc_act_rc, loc::Get(loc::S::Mirror_Btn_DisconnectSession), false, dh, dp, IconType::Power);

        // 3. Toggle: Show toolbar on output window
        float toggle_y = action_y + action_h + 16.0f;
        D2D1_RECT_F toggle_rc = D2D1::RectF(cx - action_w * 0.5f, toggle_y, cx + action_w * 0.5f, toggle_y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Mirror_Toggle_OutputToolbar), m_renderer.FontBody(),
            D2D1::RectF(toggle_rc.left, toggle_rc.top, toggle_rc.right - 46.0f, toggle_rc.bottom),
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F pill = D2D1::RectF(toggle_rc.right - 36.0f, toggle_rc.top + 3.0f, toggle_rc.right, toggle_rc.bottom - 3.0f);
        m_renderer.FillRoundedRect(pill, 9.0f, state.show_output_toolbar ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        float thumb_x = state.show_output_toolbar ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 6.0f, m_renderer.BrushTextPrimary());
        RegisterClickable(toggle_rc, Control_Toggle_OutputToolbar, loc::Get(loc::S::Mirror_Toggle_OutputToolbar));

    } else if (state.status == ConnectionStatus::Connecting) {
        // CONNECTING / STARTING STATE
        float top_y = card_rc.top + 40.0f;

        bool is_startup = (state.device_name == L"—" || state.device_name.empty());
        const wchar_t* badge_str = is_startup ? loc::Get(loc::S::Status_StartingAirPlay) : loc::Get(loc::S::Status_Connecting);
        const wchar_t* title_str = is_startup ? loc::Get(loc::S::Status_StartingAirPlay) : loc::Get(loc::S::Status_Connecting);
        const wchar_t* sub_str   = is_startup ? loc::Get(loc::S::Status_Ready)
                                              : loc::Get(loc::S::Status_Connecting);

        D2D1_RECT_F badge_rc = D2D1::RectF(cx - 100.0f, top_y, cx + 100.0f, top_y + 26.0f);
        m_renderer.DrawBadge(badge_rc, badge_str,
                             D2D1::ColorF(0.961f, 0.624f, 0.043f, 0.15f), colors::StatusAmber, true);

        // Pulsing / Spinning Circle Indicator
        D2D1_POINT_2F spin_center = D2D1::Point2F(cx, top_y + 90.0f);
        m_renderer.DrawStatusDot(spin_center, 22.0f, colors::BrandBlue, true);

        D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 140.0f, card_rc.right - 20.0f, top_y + 168.0f);
        m_renderer.DrawTextSimple(
            title_str, m_renderer.FontTitle(), title_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        D2D1_RECT_F sub_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 172.0f, card_rc.right - 20.0f, top_y + 196.0f);
        m_renderer.DrawTextSimple(
            sub_str,
            m_renderer.FontBody(), sub_rc,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

    } else if (state.status == ConnectionStatus::Error) {
        // ERROR STATE
        float top_y = card_rc.top + 40.0f;

        D2D1_RECT_F badge_rc = D2D1::RectF(cx - 70.0f, top_y, cx + 70.0f, top_y + 26.0f);
        m_renderer.DrawBadge(badge_rc, loc::Get(loc::S::Status_Dot_Error),
                             D2D1::ColorF(0.937f, 0.267f, 0.267f, 0.15f), colors::StatusRed, true);

        D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 50.0f, card_rc.right - 20.0f, top_y + 80.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Status_AirPlayRuntimeError), m_renderer.FontTitle(), title_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        D2D1_RECT_F err_rc = D2D1::RectF(card_rc.left + 40.0f, top_y + 90.0f, card_rc.right - 40.0f, top_y + 130.0f);
        m_renderer.DrawInset(err_rc, 8.0f);
        m_renderer.DrawTextSimple(
            state.status_message, m_renderer.FontSmallBold(), err_rc,
            m_renderer.BrushStatusRed(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        D2D1_RECT_F hint_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 145.0f, card_rc.right - 20.0f, top_y + 175.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Perf_MissingError),
            m_renderer.FontSmall(), hint_rc,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

    } else {
        // IDLE (READY) / RECONNECTING STATE
        float top_y = card_rc.top + 30.0f;

        D2D1_RECT_F badge_rc = D2D1::RectF(cx - 90.0f, top_y, cx + 90.0f, top_y + 26.0f);
        m_renderer.DrawBadge(badge_rc, loc::Get(loc::S::Status_Dot_Ready),
                             D2D1::ColorF(0.231f, 0.510f, 0.965f, 0.12f), colors::BrandBlue, true);

        // Logo
        D2D1_RECT_F logo_rc = D2D1::RectF(cx - 36.0f, top_y + 40.0f, cx + 36.0f, top_y + 112.0f);
        m_renderer.DrawLogo(logo_rc);

        // Title
        D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 120.0f, card_rc.right - 20.0f, top_y + 148.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Mirror_Empty_Wireless_Title), m_renderer.FontTitle(), title_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        D2D1_RECT_F sub_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 150.0f, card_rc.right - 20.0f, top_y + 196.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Mirror_Empty_Wireless_Desc),
            m_renderer.FontBody(), sub_rc,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        // 3-Step Connection Guide Box
        float box_w = std::min(440.0f, card_rc.right - card_rc.left - 40.0f);
        float box_h = 100.0f;
        float box_x = cx - box_w * 0.5f;
        float box_y = top_y + 200.0f;
        D2D1_RECT_F guide_rc = D2D1::RectF(box_x, box_y, box_x + box_w, box_y + box_h);

        m_renderer.DrawInset(guide_rc, 10.0f);

        D2D1_RECT_F s1 = D2D1::RectF(box_x + 16.0f, box_y + 10.0f, box_x + box_w - 16.0f, box_y + 36.0f);
        D2D1_RECT_F s2 = D2D1::RectF(box_x + 16.0f, box_y + 36.0f, box_x + box_w - 16.0f, box_y + 62.0f);
        D2D1_RECT_F s3 = D2D1::RectF(box_x + 16.0f, box_y + 62.0f, box_x + box_w - 16.0f, box_y + 88.0f);

        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Mirror_Guide_Step1), m_renderer.FontSmallBold(), s1,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Mirror_Guide_Step2), m_renderer.FontSmallBold(), s2,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Mirror_Guide_Step3), m_renderer.FontSmallBold(), s3,
            m_renderer.BrushTextAccent(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        // Action controls: Open/Hide Output, Disconnect (disabled), Toolbar Toggle
        float action_y = box_y + box_h + 20.0f;
        float action_w = box_w;
        float action_h = 40.0f;
        float btn_half_w = (action_w - 12.0f) * 0.5f;

        // 1. Output Button (Hiện/Ẩn output)
        D2D1_RECT_F out_act_rc = D2D1::RectF(cx - action_w * 0.5f, action_y, cx - action_w * 0.5f + btn_half_w, action_y + action_h);
        RegisterClickable(out_act_rc, Control_Btn_ToggleOutput, loc::Get(loc::S::Mirror_Btn_ShowOutput));
        bool oh = (state.hovered_control == Control_Btn_ToggleOutput);
        bool op = (state.pressed_control == Control_Btn_ToggleOutput);
        std::wstring_view out_act_lbl = state.output_window_visible
            ? loc::Get(loc::S::Mirror_Btn_HideOutput)
            : loc::Get(loc::S::Mirror_Btn_ShowOutput);
        m_renderer.DrawButton(out_act_rc, out_act_lbl, state.output_window_visible, oh, op, IconType::Monitor);

        // 2. Disconnect Button (Disabled when not connected)
        D2D1_RECT_F disc_act_rc = D2D1::RectF(cx - action_w * 0.5f + btn_half_w + 12.0f, action_y, cx + action_w * 0.5f, action_y + action_h);
        RegisterClickable(disc_act_rc, Control_Btn_Disconnect, loc::Get(loc::S::Mirror_Btn_DisconnectDisabledTooltip));
        m_renderer.DrawButton(disc_act_rc, loc::Get(loc::S::Mirror_Btn_DisconnectSession), false, false, false, IconType::Power);

        // 3. Toggle: Show toolbar on output window
        float toggle_y = action_y + action_h + 16.0f;
        D2D1_RECT_F toggle_rc = D2D1::RectF(cx - action_w * 0.5f, toggle_y, cx + action_w * 0.5f, toggle_y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Mirror_Toggle_OutputToolbar), m_renderer.FontBody(),
            D2D1::RectF(toggle_rc.left, toggle_rc.top, toggle_rc.right - 46.0f, toggle_rc.bottom),
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F pill = D2D1::RectF(toggle_rc.right - 36.0f, toggle_rc.top + 3.0f, toggle_rc.right, toggle_rc.bottom - 3.0f);
        m_renderer.FillRoundedRect(pill, 9.0f, state.show_output_toolbar ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        float thumb_x = state.show_output_toolbar ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 6.0f, m_renderer.BrushTextPrimary());
        RegisterClickable(toggle_rc, Control_Toggle_OutputToolbar, loc::Get(loc::S::Mirror_Toggle_OutputToolbar));
    }

    m_renderer.PopClip();
}

void MainWindowView::RenderRightStack(const UiState& state, const D2D1_RECT_F& area) noexcept {
    m_renderer.PushClip(area);

    float pad = 12.0f;
    float card_w = (area.right - area.left) - pad * 2.0f;
    if (card_w < 100.0f) {
        m_renderer.PopClip();
        return;
    }
    float card_x = area.left + pad;
    float cur_y  = area.top + 12.0f;
    float avail_h = area.bottom - area.top;

    // Card 1: Connection Card (compact 105 DIP)
    float card1_h = 105.0f;
    D2D1_RECT_F card1_rc = D2D1::RectF(card_x, cur_y, card_x + card_w, cur_y + card1_h);
    RenderConnectionCard(state, card1_rc);
    cur_y += card1_h + 10.0f;

    // Card 2: Performance Card (compact 125 DIP)
    float card2_h = 125.0f;
    D2D1_RECT_F card2_rc = D2D1::RectF(card_x, cur_y, card_x + card_w, cur_y + card2_h);
    RenderPerformanceCard(state, card2_rc);
    cur_y += card2_h + 10.0f;

    // Card 3: Quick Controls Card (streamlined 145 DIP)
    float card3_h = 145.0f;
    D2D1_RECT_F card3_rc = D2D1::RectF(card_x, cur_y, card_x + card_w, cur_y + card3_h);
    RenderControlsCard(state, card3_rc);
    cur_y += card3_h + 10.0f;

    // Card 4: Creator Integration Card (Only rendered if height budget allows >= 520 DIP)
    if (avail_h >= 520.0f && (area.bottom - cur_y) >= 70.0f) {
        float card4_h = std::min(80.0f, area.bottom - cur_y - 12.0f);
        D2D1_RECT_F card4_rc = D2D1::RectF(card_x, cur_y, card_x + card_w, cur_y + card4_h);
        RenderCreatorTipsCard(state, card4_rc);
    }

    m_renderer.PopClip();
}

void MainWindowView::RenderConnectionCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept {
    m_renderer.PushClip(card_rc);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    // Header row
    D2D1_RECT_F header_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 8.0f, card_rc.right - 14.0f, card_rc.top + 26.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Right_DeviceConnection), m_renderer.FontSmallBold(), header_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Device miniature icon & Name
    D2D1_RECT_F dev_icon_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 32.0f, card_rc.left + 32.0f, card_rc.top + 54.0f);
    m_renderer.DrawIcon(IconType::Phone, dev_icon_rc, colors::BrandBlue, 1.8f);

    bool is_connected = (state.status == ConnectionStatus::Connected ||
                         state.session_state == airplay::AirPlaySessionState::Connected ||
                         state.session_state == airplay::AirPlaySessionState::Streaming ||
                         state.session_state == airplay::AirPlaySessionState::Paused);

    float btn_w = 72.0f;
    D2D1_RECT_F dev_name_rc = D2D1::RectF(card_rc.left + 38.0f, card_rc.top + 28.0f, card_rc.right - btn_w - 18.0f, card_rc.top + 48.0f);
    std::wstring dev_disp = is_connected ?
        (state.device_name.empty() || state.device_name == L"—" ? loc::Get(loc::S::Mirror_AppleDevice) : state.device_name) :
        loc::Get(loc::S::Mirror_NoDeviceConnected);
    m_renderer.DrawTextSimple(
        dev_disp, m_renderer.FontHeader(), dev_name_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    D2D1_RECT_F dev_ip_rc = D2D1::RectF(card_rc.left + 38.0f, card_rc.top + 46.0f, card_rc.right - btn_w - 18.0f, card_rc.top + 64.0f);
    std::wstring net_disp;
    if (is_connected) {
        if (!state.model_name.empty() && state.model_name != L"—") {
            std::wstring model_disp = (state.model_db_match == L"Fallback" && !state.product_type.empty() && state.product_type != L"—")
                ? std::format(L"{} ({})", state.model_name, state.product_type)
                : state.model_name;
            net_disp = std::format(L"{} • {}", model_disp, state.os_version != L"—" ? state.os_version : L"iOS");
        } else if (!state.client_ip.empty() && state.client_ip != L"—") {
            net_disp = std::format(L"IP: {} • Local RTP/UDP", state.client_ip);
        } else {
            net_disp = loc::Get(loc::S::Right_LocalRtpStream);
        }
    } else {
        net_disp = loc::Get(loc::S::Status_WaitingAirPlayConn);
    }

    m_renderer.DrawTextSimple(
        net_disp, m_renderer.FontSmall(), dev_ip_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Disconnect / Refresh Button on right
    D2D1_RECT_F disc_btn_rc = D2D1::RectF(card_rc.right - btn_w - 14.0f, card_rc.top + 30.0f, card_rc.right - 14.0f, card_rc.top + 58.0f);
    RegisterClickable(disc_btn_rc, Control_Btn_Disconnect);
    bool is_hovered = (state.hovered_control == Control_Btn_Disconnect);
    bool is_pressed = (state.pressed_control == Control_Btn_Disconnect);

    const wchar_t* btn_text = state.receiver_quality_pending
        ? loc::Get(loc::S::Right_Btn_Apply)
        : (is_connected ? loc::Get(loc::S::Right_Btn_Eject) : loc::Get(loc::S::Right_Btn_Refresh));

    m_renderer.DrawButton(
        disc_btn_rc,
        btn_text,
        false, is_hovered, is_pressed,
        IconType::Power
    );

    // Pills / tags row at bottom of card
    float pill_y = card_rc.top + 72.0f;
    float pill_w = 64.0f;
    float pill_h = 20.0f;
    float px = card_rc.left + 14.0f;

    std::wstring tag1 = (state.width > 0) ? std::format(L"{}p{:.0f}", state.height, state.nominal_fps > 0 ? state.nominal_fps : 60.0) : L"—";
    m_renderer.DrawBadge(D2D1::RectF(px, pill_y, px + pill_w, pill_y + pill_h),
                         tag1, D2D1::ColorF(0.231f, 0.510f, 0.965f, 0.15f), colors::BrandBlue);
    px += pill_w + 6.0f;

    m_renderer.DrawBadge(D2D1::RectF(px, pill_y, px + pill_w + 26.0f, pill_y + pill_h),
                         state.transport_type, D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.15f), colors::StatusGreen);
    px += pill_w + 32.0f;

    if (px + pill_w + 14.0f <= card_rc.right - 14.0f) {
        m_renderer.DrawBadge(D2D1::RectF(px, pill_y, px + pill_w + 14.0f, pill_y + pill_h),
                             std::format(L"{}: {}", loc::Get(loc::S::Right_Signal), state.signal_quality), D2D1::ColorF(0.486f, 0.227f, 0.929f, 0.15f), colors::BrandPurple);
    }

    m_renderer.PopClip();
}

void MainWindowView::RenderPerformanceCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept {
    m_renderer.PushClip(card_rc);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    // Header
    D2D1_RECT_F header_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 8.0f, card_rc.right - 14.0f, card_rc.top + 26.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Right_RealtimeTelemetry), m_renderer.FontSmallBold(), header_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Sub-badge in header
    D2D1_RECT_F sub_badge = D2D1::RectF(card_rc.right - 88.0f, card_rc.top + 7.0f, card_rc.right - 14.0f, card_rc.top + 25.0f);
    if (state.status == ConnectionStatus::Connected) {
        m_renderer.DrawBadge(sub_badge, loc::Get(loc::S::Status_Dot_Streaming), D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.12f), colors::StatusGreen);
    } else {
        m_renderer.DrawBadge(sub_badge, loc::Get(loc::S::Status_Dot_Idle), D2D1::ColorF(0.231f, 0.510f, 0.965f, 0.12f), colors::BrandBlue);
    }

    // 2x2 Metric Tiles Grid
    float grid_x = card_rc.left + 14.0f;
    float grid_y = card_rc.top + 30.0f;
    float tile_gap = 6.0f;
    float tile_w = ((card_rc.right - card_rc.left - 28.0f) - tile_gap) * 0.5f;
    float tile_h = 42.0f;

    struct MetricTile {
        std::wstring_view label;
        std::wstring val;
        IconType icon;
        D2D1_COLOR_F color;
    };

    std::wstring fps_disp = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    std::wstring lat_disp = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    std::wstring q_disp   = std::format(L"{} {}", state.queue_depth, state.queue_depth == 1 ? loc::Get(loc::S::Right_FrameSingular) : loc::Get(loc::S::Right_FramesPlural));

    MetricTile tiles[4] = {
        { loc::Get(loc::S::Right_Tile_Decoder), state.decoder_name, IconType::Chip, colors::BrandBlue },
        { loc::Get(loc::S::Right_Tile_RenderFps), fps_disp, IconType::Performance, colors::StatusGreen },
        { loc::Get(loc::S::Right_Tile_QueuePacing), q_disp, IconType::CheckCircle, colors::BrandPurple },
        { loc::Get(loc::S::Right_Tile_AvLag), lat_disp, IconType::Wifi, colors::BrandCyan },
    };

    for (int i = 0; i < 4; ++i) {
        float tx = grid_x + (i % 2) * (tile_w + tile_gap);
        float ty = grid_y + (i / 2) * (tile_h + tile_gap);
        D2D1_RECT_F tile_rc = D2D1::RectF(tx, ty, tx + tile_w, ty + tile_h);

        m_renderer.DrawInset(tile_rc, 6.0f);

        D2D1_RECT_F lbl_rc = D2D1::RectF(tx + 8.0f, ty + 4.0f, tx + tile_w - 6.0f, ty + 18.0f);
        m_renderer.DrawTextSimple(
            tiles[i].label, m_renderer.FontSmall(), lbl_rc,
            m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );

        D2D1_RECT_F val_rc = D2D1::RectF(tx + 8.0f, ty + 18.0f, tx + tile_w - 6.0f, ty + 38.0f);
        m_renderer.DrawTextSimple(
            tiles[i].val, m_renderer.FontBodyBold(), val_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        );
    }

    m_renderer.PopClip();
}

void MainWindowView::RenderControlsCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept {
    m_renderer.PushClip(card_rc);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    // Header
    D2D1_RECT_F header_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 7.0f, card_rc.right - 14.0f, card_rc.top + 23.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Mirror_WindowsTitle), m_renderer.FontSmallBold(), header_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Row 1: Primary Action Buttons (Screen Only / Fullscreen)
    float pri_top = card_rc.top + 25.0f;
    float pri_h = 32.0f;
    float btn_w = (card_rc.right - card_rc.left - 28.0f - 8.0f) * 0.5f;

    // 1. Hiện/Ẩn Output (Toggle Output Window) button
    D2D1_RECT_F out_btn_rc = D2D1::RectF(card_rc.left + 14.0f, pri_top, card_rc.left + 14.0f + btn_w, pri_top + pri_h);
    RegisterClickable(out_btn_rc, Control_Btn_ToggleOutput, loc::Get(loc::S::Mirror_Btn_ShowOutput));
    bool out_h = (state.hovered_control == Control_Btn_ToggleOutput);
    bool out_p = (state.pressed_control == Control_Btn_ToggleOutput);
    std::wstring_view out_lbl = state.output_window_visible
        ? loc::Get(loc::S::Mirror_Btn_HideOutput)
        : loc::Get(loc::S::Mirror_Btn_ShowOutput);
    m_renderer.DrawButton(out_btn_rc, out_lbl, state.output_window_visible, out_h, out_p, IconType::Monitor);

    // 2. Toàn màn hình (Fullscreen) button
    D2D1_RECT_F full_btn_rc = D2D1::RectF(card_rc.left + 14.0f + btn_w + 8.0f, pri_top, card_rc.right - 14.0f, pri_top + pri_h);
    RegisterClickable(full_btn_rc, Control_Btn_Fullscreen);
    bool full_h = (state.hovered_control == Control_Btn_Fullscreen);
    bool full_p = (state.pressed_control == Control_Btn_Fullscreen);
    std::wstring_view full_lbl = state.output_fullscreen ? L"Windowed" : loc::Get(loc::S::Right_Btn_Full);
    m_renderer.DrawButton(full_btn_rc, full_lbl, state.output_fullscreen, full_h, full_p, IconType::Maximize);

    // Row 2: Secondary Toggles (Always on Top, Aspect Ratio Lock)
    float row2_y = pri_top + pri_h + 6.0f;
    float row2_h = 28.0f;
    float row2_btn_w = (card_rc.right - card_rc.left - 28.0f - 8.0f) * 0.5f;

    D2D1_RECT_F top_btn_rc = D2D1::RectF(card_rc.left + 14.0f, row2_y, card_rc.left + 14.0f + row2_btn_w, row2_y + row2_h);
    RegisterClickable(top_btn_rc, Control_Btn_AlwaysOnTop, loc::Get(loc::S::Right_Btn_Pin));
    m_renderer.DrawButton(top_btn_rc, loc::Get(loc::S::Right_Btn_Pin), state.always_on_top,
        state.hovered_control == Control_Btn_AlwaysOnTop, state.pressed_control == Control_Btn_AlwaysOnTop, IconType::Pin);

    D2D1_RECT_F aspect_btn_rc = D2D1::RectF(card_rc.left + 14.0f + row2_btn_w + 8.0f, row2_y, card_rc.right - 14.0f, row2_y + row2_h);
    RegisterClickable(aspect_btn_rc, Control_Btn_AspectLock, loc::Get(loc::S::Output_Fit));
    m_renderer.DrawButton(aspect_btn_rc, loc::Get(loc::S::Output_Fit), false,
        state.hovered_control == Control_Btn_AspectLock, state.pressed_control == Control_Btn_AspectLock, IconType::Monitor);

    // Row 3: Quick Volume Bar (Shared state with Audio tab)
    float vol_y = row2_y + row2_h + 8.0f;
    float vol_h = 28.0f;
    float mute_btn_w = 60.0f;
    float pct_w = 40.0f;

    D2D1_RECT_F mute_rc = D2D1::RectF(card_rc.left + 14.0f, vol_y, card_rc.left + 14.0f + mute_btn_w, vol_y + vol_h);
    RegisterClickable(mute_rc, Control_Btn_Mute, loc::Get(loc::S::Audio_Mute));
    m_renderer.DrawButton(mute_rc, state.audio_muted ? loc::Get(loc::S::Right_Btn_Unmute) : loc::Get(loc::S::Right_Btn_Mute),
        state.audio_muted, state.hovered_control == Control_Btn_Mute, state.pressed_control == Control_Btn_Mute, IconType::Speaker);

    // Interactive slider track
    float track_x0 = card_rc.left + 14.0f + mute_btn_w + 8.0f;
    float track_x1 = card_rc.right - 14.0f - pct_w - 4.0f;
    D2D1_RECT_F slider_area_rc = D2D1::RectF(track_x0, vol_y, track_x1, vol_y + vol_h);
    RegisterClickable(slider_area_rc, Control_Slider_QuickVolume, loc::Get(loc::S::Common_QuickVolume));

    float track_cy = vol_y + vol_h * 0.5f;
    D2D1_RECT_F track_bg = D2D1::RectF(track_x0, track_cy - 3.0f, track_x1, track_cy + 3.0f);
    m_renderer.FillRoundedRect(track_bg, 3.0f, m_renderer.BrushCardBorder());

    float vol_frac = std::clamp(state.audio_volume, 0.0f, 1.0f);
    float fill_x1 = track_x0 + (track_x1 - track_x0) * vol_frac;
    if (fill_x1 > track_x0) {
        D2D1_RECT_F track_fill = D2D1::RectF(track_x0, track_cy - 3.0f, fill_x1, track_cy + 3.0f);
        m_renderer.FillRoundedRect(track_fill, 3.0f, m_renderer.BrushBrandBlue());
    }

    D2D1_ELLIPSE thumb = D2D1::Ellipse(D2D1::Point2F(fill_x1, track_cy), 6.0f, 6.0f);
    m_renderer.Target()->FillEllipse(thumb, m_renderer.BrushTextPrimary());

    D2D1_RECT_F pct_rc = D2D1::RectF(track_x1 + 4.0f, vol_y, card_rc.right - 14.0f, vol_y + vol_h);
    std::wstring pct_str = state.audio_muted ? L"0%" : std::format(L"{}%", static_cast<int>(std::round(vol_frac * 100.0f)));
    m_renderer.DrawTextSimple(pct_str, m_renderer.FontSmallBold(), pct_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_renderer.PopClip();
}

void MainWindowView::RenderCreatorTipsCard(const UiState& /*state*/, const D2D1_RECT_F& card_rc) noexcept {
    m_renderer.PushClip(card_rc);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    D2D1_RECT_F header_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 8.0f, card_rc.right - 14.0f, card_rc.top + 24.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Right_CreatorTipsHeader), m_renderer.FontSmallBold(), header_rc,
        m_renderer.BrushTextAccent(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    D2D1_RECT_F tip_rc = D2D1::RectF(card_rc.left + 14.0f, card_rc.top + 26.0f, card_rc.right - 14.0f, card_rc.bottom - 8.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Right_CreatorTipsDesc),
        m_renderer.FontSmall(), tip_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR
    );

    m_renderer.PopClip();
}

void MainWindowView::RenderStatusBar(const UiState& state, float bottom_y, float width) noexcept {
    float h = metrics::StatusBarHeight;
    D2D1_RECT_F bar_rc = D2D1::RectF(0.0f, bottom_y, width, bottom_y + h);
    m_renderer.PushClip(bar_rc);

    // Top border line
    m_renderer.DrawLine(
        D2D1::Point2F(0.0f, bottom_y),
        D2D1::Point2F(width, bottom_y),
        m_renderer.BrushCardBorder(), 1.0f
    );
    const bool has_video_evidence = (state.total_frames_presented > 0 || state.render_fps > 0.0 || state.decoded_fps > 0.0 || state.width > 0);
    if (state.connection_mode == 1) {
        const std::wstring phase = !state.wired.usb_interface_count ? loc::Get(loc::S::Wired_NoCable)
            : !state.wired.network_up ? loc::Get(loc::S::Wired_PreparingUsbNetwork)
            : std::format(L"{}: {}", loc::Get(loc::S::Wired_ScreenMirroring),
                loc::Get(state.status == ConnectionStatus::Streaming
                    ? (has_video_evidence ? loc::S::Status_Streaming : loc::S::Status_ConnectedWaitingVideo)
                    : loc::S::Status_Ready));
        m_renderer.DrawStatusDot(D2D1::Point2F(20.0f, bottom_y + h * 0.5f), 3.5f,
            state.wired.network_up ? colors::StatusGreen
                : state.wired.usb_interface_count ? colors::StatusAmber : colors::StatusBlue, false);
        m_renderer.DrawTextSimple(phase, m_renderer.FontSmall(),
            D2D1::RectF(32.0f, bottom_y, width * 0.68f, bottom_y + h),
            m_renderer.BrushTextSecondary());
        m_renderer.DrawTextSimple(loc::Get(loc::S::Mode_Wired), m_renderer.FontSmallBold(),
            D2D1::RectF(width * 0.70f, bottom_y, width - 20.0f, bottom_y + h),
            m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_renderer.PopClip();
        return;
    }

    // Status Dot
    D2D1_POINT_2F dot = D2D1::Point2F(20.0f, bottom_y + h * 0.5f);
    D2D1_COLOR_F dot_color = colors::StatusBlue;
    switch (state.session_state) {
    case airplay::AirPlaySessionState::Streaming:
    case airplay::AirPlaySessionState::Connected:
        dot_color = has_video_evidence ? colors::StatusGreen : colors::StatusAmber;
        break;
    case airplay::AirPlaySessionState::Paused:
    case airplay::AirPlaySessionState::Connecting:
    case airplay::AirPlaySessionState::Disconnecting:
        dot_color = colors::StatusAmber;
        break;
    case airplay::AirPlaySessionState::Error:
        dot_color = colors::StatusRed;
        break;
    case airplay::AirPlaySessionState::Idle:
    default:
        dot_color = colors::StatusBlue;
        break;
    }
    m_renderer.DrawStatusDot(dot, 3.5f, dot_color, false);

    // Status Message Text
    D2D1_RECT_F msg_rc = D2D1::RectF(32.0f, bottom_y, width * 0.50f, bottom_y + h);
    m_renderer.DrawTextSimple(
        state.status_message, m_renderer.FontSmall(), msg_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Right Telemetry Indicators
    bool is_conn = (state.status == ConnectionStatus::Connected ||
                    state.session_state == airplay::AirPlaySessionState::Connected ||
                    state.session_state == airplay::AirPlaySessionState::Streaming ||
                    state.session_state == airplay::AirPlaySessionState::Paused);

    std::wstring audio_status_str;
    if (state.audio_muted) {
        audio_status_str = loc::Get(loc::S::Audio_Mute);
    } else if (state.audio_active) {
        audio_status_str = std::format(L"48kHz ({})", loc::Get(loc::S::Perf_Active));
    } else if (state.audio_rtp_packets > 0) {
        audio_status_str = std::format(L"48kHz ({})", loc::Get(loc::S::Status_Paused));
    } else {
        audio_status_str = std::format(L"48kHz ({})", loc::Get(loc::S::Status_Idle));
    }

    std::wstring fps_footer = state.has_fps_sample ?
        std::format(L"Render: {:.1f} FPS", state.render_fps) :
        std::format(L"Render: {}", loc::Get(loc::S::Common_NoData));

    std::wstring right_status = is_conn ?
        std::format(L"{} • {} • Queue: {} • Audio: {}",
                    state.transport_type, fps_footer, state.queue_depth, audio_status_str) :
        loc::Get(loc::S::Status_EngineReadyPorts);

    D2D1_RECT_F rstat_rc = D2D1::RectF(width * 0.45f, bottom_y, width - 20.0f, bottom_y + h);
    m_renderer.DrawTextSimple(
        right_status, m_renderer.FontSmall(), rstat_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    m_renderer.PopClip();
}

void MainWindowView::RenderPerformanceView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    float pad = 24.0f;
    float content_w = area.right - area.left - pad * 2.0f;
    if (m_content_height > m_viewport_height) {
        content_w -= 12.0f;
    }
    float card_h = 510.0f;
    D2D1_RECT_F card_rc = D2D1::RectF(area.left + pad, area.top + pad, area.left + pad + content_w, area.top + pad + card_h);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);
    if (state.connection_mode == 1) {
        m_renderer.DrawTextSimple(loc::Get(loc::S::Mode_Wired), m_renderer.FontTitle(),
            D2D1::RectF(card_rc.left + 24.0f, card_rc.top + 20.0f,
                card_rc.right - 24.0f, card_rc.top + 55.0f), m_renderer.BrushTextPrimary());
        const std::wstring count = std::format(L"{}", state.wired.usb_interface_count);
        const std::wstring output = std::format(L"{}×{}", state.output_width, state.output_height);
        const std::wstring unknown = loc::Get(loc::S::Wired_Unknown);
        const std::wstring runtime = !state.wired.apple_runtime_present
            ? loc::Get(loc::S::Wired_RuntimeMissing)
            : state.wired.apple_service_running ? loc::Get(loc::S::Wired_RuntimeRunning)
            : loc::Get(loc::S::Wired_RuntimeStopped);
        const std::wstring device = state.wired.device_name.empty() ? unknown : state.wired.device_name;
        const std::wstring adapter = state.wired.network_up ? L"Apple Mobile Device Ethernet" : unknown;
        const std::wstring usb_val = state.wired.usb_interface_count ? L"USB connected" : L"USB disconnected";
        const std::wstring media_protocol = L"AirPlay (H.264 + audio)";
        const std::wstring control_protocol = L"CoreDevice HID / RSD";
        const std::wstring local_ip = state.wired.network_ipv4.empty() ? unknown : state.wired.network_ipv4;
        const std::wstring rtsp_local = state.wired.rtsp_local_endpoint.empty()
            ? unknown : state.wired.rtsp_local_endpoint;
        const std::wstring rtsp_peer = state.wired.rtsp_peer_endpoint.empty()
            ? unknown : state.wired.rtsp_peer_endpoint;
        const std::wstring network_id = state.wired.network_up
            ? std::format(L"ifIndex {} · LUID {} · /{}", state.wired.network_if_index,
                state.wired.network_luid, state.wired.network_prefix) : unknown;
        const std::wstring source = state.width && state.height
            ? std::format(L"{}×{} · {:.1f} fps", state.width, state.height, state.source_fps) : unknown;
        const std::wstring rates = std::format(L"Decoded {:.1f} · Presented {:.1f} fps",
            state.decoded_fps, state.render_fps);
        const struct { const wchar_t* label; const std::wstring* value; } rows[] = {
            {L"Physical transport", &usb_val},
            {L"Network transport", &adapter},
            {L"Local media IP", &local_ip},
            {L"Peer IP", &state.client_ip},
            {L"RTSP local", &rtsp_local},
            {L"RTSP peer", &rtsp_peer},
            {L"Network interface", &network_id},
            {L"Media protocol", &media_protocol},
            {L"Control protocol", &control_protocol},
            {L"Source", &source},
            {L"Frame rates", &rates},
            {L"Output", &output},
            {L"USB interfaces", &count},
            {L"Apple runtime", &runtime}
        };
        for (int i = 0; i < 14; ++i) {
            const float y = card_rc.top + 70.0f + i * 31.0f;
            m_renderer.DrawInset(D2D1::RectF(card_rc.left + 24.0f, y,
                card_rc.right - 24.0f, y + 27.0f), 7.0f);
            m_renderer.DrawTextSimple(rows[i].label, m_renderer.FontSmall(),
                D2D1::RectF(card_rc.left + 38.0f, y + 2.0f, card_rc.left + 230.0f, y + 25.0f),
                m_renderer.BrushTextMuted());
            m_renderer.DrawTextSimple(*rows[i].value, m_renderer.FontSmallBold(),
                D2D1::RectF(card_rc.left + 230.0f, y + 2.0f, card_rc.right - 38.0f, y + 25.0f),
                m_renderer.BrushTextPrimary());
        }
        return;
    }

    // Panel Header
    D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 24.0f, card_rc.top + 20.0f, card_rc.right - 200.0f, card_rc.top + 48.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Perf_DiagnosticsTitle), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    D2D1_RECT_F sub_rc = D2D1::RectF(card_rc.left + 24.0f, card_rc.top + 48.0f, card_rc.right - 200.0f, card_rc.top + 70.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Perf_DiagnosticsSub),
        m_renderer.FontSmall(), sub_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Relocated Flush Pipeline Action Button (Item 7)
    D2D1_RECT_F flush_rc = D2D1::RectF(card_rc.right - 160.0f, card_rc.top + 24.0f, card_rc.right - 24.0f, card_rc.top + 60.0f);
    RegisterClickable(flush_rc, Control_Btn_FlushPipeline, L"", true);
    bool h = (state.hovered_control == Control_Btn_FlushPipeline);
    bool p = (state.pressed_control == Control_Btn_FlushPipeline);
    m_renderer.DrawButton(flush_rc, loc::Get(loc::S::Perf_FlushPipeline), false, h, p, IconType::Refresh);

    // 4 Diagnostic Grid Cards: VIDEO, SOURCE, OUTPUT, AUDIO
    float grid_top = card_rc.top + 80.0f;
    float total_content_w = card_rc.right - card_rc.left - 48.0f;
    float col_gap = 16.0f;
    float col_w = (total_content_w - col_gap) * 0.5f;
    float row_h = 180.0f;

    // Card 1: Video Decode & Render
    D2D1_RECT_F c1_rc = D2D1::RectF(card_rc.left + 24.0f, grid_top, card_rc.left + 24.0f + col_w, grid_top + row_h);
    m_renderer.DrawInset(c1_rc, 10.0f);
    D2D1_RECT_F c1_title = D2D1::RectF(c1_rc.left + 14.0f, c1_rc.top + 10.0f, c1_rc.right - 14.0f, c1_rc.top + 28.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_VideoDecodeRender), m_renderer.FontSmallBold(), c1_title, m_renderer.BrushBrandBlue());

    // 4 Key Metrics: Source FPS, Decoded FPS, Presented FPS, Latency (prominent visual weight)
    float c1_half_w = (col_w - 28.0f - 12.0f) * 0.5f;
    float c1_left_col = c1_rc.left + 14.0f;
    float c1_right_col = c1_left_col + c1_half_w + 12.0f;

    // Row 1: Source FPS & Decoded FPS
    float m_r1_top = c1_rc.top + 32.0f;
    D2D1_RECT_F src_fps_lbl = D2D1::RectF(c1_left_col, m_r1_top, c1_left_col + c1_half_w, m_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_SourceFps), m_renderer.FontSmall(), src_fps_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F src_fps_val = D2D1::RectF(c1_left_col, m_r1_top + 14.0f, c1_left_col + c1_half_w, m_r1_top + 36.0f);
    m_renderer.DrawTextSimple(std::format(L"{:.1f} FPS", state.source_fps), m_renderer.FontSubheader(), src_fps_val, m_renderer.BrushTextPrimary());

    D2D1_RECT_F dec_fps_lbl = D2D1::RectF(c1_right_col, m_r1_top, c1_right_col + c1_half_w, m_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_DecodedPresented), m_renderer.FontSmall(), dec_fps_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F dec_fps_val = D2D1::RectF(c1_right_col, m_r1_top + 14.0f, c1_right_col + c1_half_w, m_r1_top + 36.0f);
    m_renderer.DrawTextSimple(std::format(L"{:.1f} FPS", state.decoded_fps), m_renderer.FontSubheader(), dec_fps_val, m_renderer.BrushTextPrimary());

    // Row 2: Presented FPS & Latency
    float m_r2_top = c1_rc.top + 74.0f;
    D2D1_RECT_F prs_fps_lbl = D2D1::RectF(c1_left_col, m_r2_top, c1_left_col + c1_half_w, m_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Right_Tile_RenderFps), m_renderer.FontSmall(), prs_fps_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F prs_fps_val = D2D1::RectF(c1_left_col, m_r2_top + 14.0f, c1_left_col + c1_half_w, m_r2_top + 36.0f);
    std::wstring prs_fps_val_text = state.has_fps_sample ?
        (state.render_fps > 0.0 ? std::format(L"{:.1f} FPS", state.render_fps) : L"0.0 FPS") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    m_renderer.DrawTextSimple(prs_fps_val_text, m_renderer.FontSubheader(), prs_fps_val, m_renderer.BrushTextPrimary());

    D2D1_RECT_F lat_lbl = D2D1::RectF(c1_right_col, m_r2_top, c1_right_col + c1_half_w, m_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Right_Tile_AvLag), m_renderer.FontSmall(), lat_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F lat_val = D2D1::RectF(c1_right_col, m_r2_top + 14.0f, c1_right_col + c1_half_w, m_r2_top + 36.0f);
    std::wstring lat_perf_str = state.has_latency_sample ?
        (state.pipeline_latency_ms >= 1.0 ? std::format(L"{:.1f} ms", state.pipeline_latency_ms) : L"< 1.0 ms") :
        std::wstring(loc::Get(loc::S::Common_NoData));
    m_renderer.DrawTextSimple(lat_perf_str, m_renderer.FontSubheader(), lat_val, m_renderer.BrushTextPrimary());

    // Secondary line: Decoder, Drops, Queue
    float m_r3_top = c1_rc.top + 118.0f;
    D2D1_RECT_F c1_sec = D2D1::RectF(c1_left_col, m_r3_top, c1_rc.right - 14.0f, c1_rc.bottom - 6.0f);
    std::wstring c1_sec_text = std::format(
        L"{}: {}  •  {}: {}  •  {}: {}",
        loc::Get(loc::S::Perf_Lbl_Decoder), state.decoder_name.empty() ? L"—" : state.decoder_name,
        loc::Get(loc::S::Mirror_Metric_FrameDrops), state.dropped_frames,
        loc::Get(loc::S::Right_Tile_QueuePacing), state.queue_depth);
    m_renderer.DrawTextSimple(c1_sec_text, m_renderer.FontSmall(), c1_sec, m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Card 2: Source
    D2D1_RECT_F c2_rc = D2D1::RectF(card_rc.left + 24.0f + col_w + col_gap, grid_top, card_rc.right - 24.0f, grid_top + row_h);
    m_renderer.DrawInset(c2_rc, 10.0f);
    D2D1_RECT_F c2_title = D2D1::RectF(c2_rc.left + 14.0f, c2_rc.top + 10.0f, c2_rc.right - 14.0f, c2_rc.top + 28.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_Source), m_renderer.FontSmallBold(), c2_title, m_renderer.BrushBrandCyan());

    float c2_half_w = (col_w - 28.0f - 12.0f) * 0.5f;
    float c2_left_col = c2_rc.left + 14.0f;
    float c2_right_col = c2_left_col + c2_half_w + 12.0f;

    // Row 1: Actual Source & Bitrate (prominent)
    D2D1_RECT_F c2_src_lbl = D2D1::RectF(c2_left_col, m_r1_top, c2_left_col + c2_half_w, m_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_ActualSource), m_renderer.FontSmall(), c2_src_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c2_src_val = D2D1::RectF(c2_left_col, m_r1_top + 14.0f, c2_left_col + c2_half_w, m_r1_top + 36.0f);
    std::wstring c2_act_src = state.width && state.height ? std::format(L"{}×{}", state.width, state.height) : L"—";
    m_renderer.DrawTextSimple(c2_act_src, m_renderer.FontSubheader(), c2_src_val, m_renderer.BrushTextPrimary());

    D2D1_RECT_F c2_bit_lbl = D2D1::RectF(c2_right_col, m_r1_top, c2_right_col + c2_half_w, m_r1_top + 14.0f);
    m_renderer.DrawTextSimple(L"Bitrate", m_renderer.FontSmall(), c2_bit_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c2_bit_val = D2D1::RectF(c2_right_col, m_r1_top + 14.0f, c2_right_col + c2_half_w, m_r1_top + 36.0f);
    m_renderer.DrawTextSimple(std::format(L"{:.2f} Mbps", state.video_bitrate_mbps), m_renderer.FontSubheader(), c2_bit_val, m_renderer.BrushTextPrimary());

    // Row 2: Requested Input & Transport Mode
    D2D1_RECT_F c2_req_lbl = D2D1::RectF(c2_left_col, m_r2_top, c2_left_col + c2_half_w, m_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_RequestedInput), m_renderer.FontSmall(), c2_req_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c2_req_val = D2D1::RectF(c2_left_col, m_r2_top + 14.0f, c2_left_col + c2_half_w, m_r2_top + 36.0f);
    m_renderer.DrawTextSimple(state.requested_quality_class.empty() || state.requested_quality_class == L"—" ? L"Auto" : state.requested_quality_class, m_renderer.FontBodyBold(), c2_req_val, m_renderer.BrushTextSecondary());

    D2D1_RECT_F c2_trn_lbl = D2D1::RectF(c2_right_col, m_r2_top, c2_right_col + c2_half_w, m_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_TransportMode), m_renderer.FontSmall(), c2_trn_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c2_trn_val = D2D1::RectF(c2_right_col, m_r2_top + 14.0f, c2_right_col + c2_half_w, m_r2_top + 36.0f);
    m_renderer.DrawTextSimple(state.transport_name.empty() ? L"—" : state.transport_name, m_renderer.FontBodyBold(), c2_trn_val, m_renderer.BrushTextSecondary());

    // Secondary line: Coded resolution and RTP packets
    D2D1_RECT_F c2_sec = D2D1::RectF(c2_left_col, m_r3_top, c2_rc.right - 14.0f, c2_rc.bottom - 6.0f);
    std::wstring c2_sec_text = std::format(
        L"Coded: {}×{}  •  {}: {}",
        state.coded_width, state.coded_height,
        loc::Get(loc::S::Perf_Lbl_VideoRtpPackets), state.video_rtp_packets);
    m_renderer.DrawTextSimple(c2_sec_text, m_renderer.FontSmall(), c2_sec, m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Card 3: Output & Presentation
    float grid_row2_top = grid_top + row_h + 16.0f;
    D2D1_RECT_F c3_rc = D2D1::RectF(card_rc.left + 24.0f, grid_row2_top, card_rc.left + 24.0f + col_w, grid_row2_top + row_h);
    m_renderer.DrawInset(c3_rc, 10.0f);
    D2D1_RECT_F c3_title = D2D1::RectF(c3_rc.left + 14.0f, c3_rc.top + 10.0f, c3_rc.right - 14.0f, c3_rc.top + 28.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_PresDxgiClock), m_renderer.FontSmallBold(), c3_title, m_renderer.BrushStatusGreen());

    const std::wstring_view out_quality_names[] = {
        loc::Get(loc::S::Opt_Out_Auto),
        loc::Get(loc::S::Opt_Out_HD),
        loc::Get(loc::S::Opt_Out_FullHD),
        loc::Get(loc::S::Opt_Out_2K),
        loc::Get(loc::S::Opt_Out_4K),
        loc::Get(loc::S::Opt_Out_Original),
        loc::Get(loc::S::Opt_Out_Custom)
    };
    std::wstring out_q_label = std::wstring(out_quality_names[std::clamp(state.output_quality, 0, 6)]);

    float c3_half_w = (col_w - 28.0f - 12.0f) * 0.5f;
    float c3_left_col = c3_rc.left + 14.0f;
    float c3_right_col = c3_left_col + c3_half_w + 12.0f;

    // Row 1: Resolved Output & Quality Preset (prominent)
    float c3_r1_top = c3_rc.top + 32.0f;
    D2D1_RECT_F c3_res_lbl = D2D1::RectF(c3_left_col, c3_r1_top, c3_left_col + c3_half_w, c3_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Resolved), m_renderer.FontSmall(), c3_res_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c3_res_val = D2D1::RectF(c3_left_col, c3_r1_top + 14.0f, c3_left_col + c3_half_w, c3_r1_top + 36.0f);
    std::wstring c3_res_str = state.output_width && state.output_height ? std::format(L"{}×{}", state.output_width, state.output_height) : L"—";
    m_renderer.DrawTextSimple(c3_res_str, m_renderer.FontSubheader(), c3_res_val, m_renderer.BrushTextPrimary());

    D2D1_RECT_F c3_q_lbl = D2D1::RectF(c3_right_col, c3_r1_top, c3_right_col + c3_half_w, c3_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Output_Quality), m_renderer.FontSmall(), c3_q_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c3_q_val = D2D1::RectF(c3_right_col, c3_r1_top + 14.0f, c3_right_col + c3_half_w, c3_r1_top + 36.0f);
    m_renderer.DrawTextSimple(out_q_label, m_renderer.FontSubheader(), c3_q_val, m_renderer.BrushTextPrimary());

    // Row 2: Renderer Mode & Surface
    float c3_r2_top = c3_rc.top + 74.0f;
    D2D1_RECT_F c3_rnd_lbl = D2D1::RectF(c3_left_col, c3_r2_top, c3_left_col + c3_half_w, c3_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_RendererMode), m_renderer.FontSmall(), c3_rnd_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c3_rnd_val = D2D1::RectF(c3_left_col, c3_r2_top + 14.0f, c3_left_col + c3_half_w, c3_r2_top + 36.0f);
    m_renderer.DrawTextSimple(state.renderer_name.empty() ? L"—" : state.renderer_name, m_renderer.FontBodyBold(), c3_rnd_val, m_renderer.BrushTextSecondary());

    D2D1_RECT_F c3_srf_lbl = D2D1::RectF(c3_right_col, c3_r2_top, c3_right_col + c3_half_w, c3_r2_top + 14.0f);
    m_renderer.DrawTextSimple(L"Surface", m_renderer.FontSmall(), c3_srf_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c3_srf_val = D2D1::RectF(c3_right_col, c3_r2_top + 14.0f, c3_right_col + c3_half_w, c3_r2_top + 36.0f);
    m_renderer.DrawTextSimple(state.width ? L"NV12 Zero-Copy" : L"—", m_renderer.FontBodyBold(), c3_srf_val, m_renderer.BrushTextSecondary());

    // Secondary line: Scaler & Capture Canvas
    float c3_r3_top = c3_rc.top + 118.0f;
    D2D1_RECT_F c3_sec = D2D1::RectF(c3_left_col, c3_r3_top, c3_rc.right - 14.0f, c3_rc.bottom - 6.0f);
    std::wstring c3_sec_text = std::format(
        L"{}: {}  •  {}: {}",
        loc::Get(loc::S::Output_ScalingQuality), state.scaler_name.empty() ? L"—" : state.scaler_name,
        loc::Get(loc::S::Perf_Lbl_CaptureCanvas), state.capture_width && state.capture_height ? std::format(L"{}×{}", state.capture_width, state.capture_height) : L"—");
    m_renderer.DrawTextSimple(c3_sec_text, m_renderer.FontSmall(), c3_sec, m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Card 4: Audio Subsystem & WASAPI
    D2D1_RECT_F c4_rc = D2D1::RectF(card_rc.left + 24.0f + col_w + col_gap, grid_row2_top, card_rc.right - 24.0f, grid_row2_top + row_h);
    m_renderer.DrawInset(c4_rc, 10.0f);
    D2D1_RECT_F c4_title = D2D1::RectF(c4_rc.left + 14.0f, c4_rc.top + 10.0f, c4_rc.right - 14.0f, c4_rc.top + 28.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_AudioWasapiClock), m_renderer.FontSmallBold(), c4_title, m_renderer.BrushBrandPurple());

    float c4_half_w = (col_w - 28.0f - 12.0f) * 0.5f;
    float c4_left_col = c4_rc.left + 14.0f;
    float c4_right_col = c4_left_col + c4_half_w + 12.0f;

    // Row 1: Status & Buffer (prominent)
    D2D1_RECT_F c4_st_lbl = D2D1::RectF(c4_left_col, c3_r1_top, c4_left_col + c4_half_w, c3_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_AudioStatus), m_renderer.FontSmall(), c4_st_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c4_st_val = D2D1::RectF(c4_left_col, c3_r1_top + 14.0f, c4_left_col + c4_half_w, c3_r1_top + 36.0f);
    std::wstring c4_audio_status = state.audio_muted ? std::wstring(loc::Get(loc::S::Audio_Mute))
        : (state.audio_active ? std::format(L"{} (48kHz)", loc::Get(loc::S::Perf_Active))
                              : std::format(L"{} (48kHz)", loc::Get(loc::S::Status_Idle)));
    m_renderer.DrawTextSimple(c4_audio_status, m_renderer.FontSubheader(), c4_st_val, state.audio_active ? m_renderer.BrushStatusGreen() : m_renderer.BrushTextPrimary());

    D2D1_RECT_F c4_buf_lbl = D2D1::RectF(c4_right_col, c3_r1_top, c4_right_col + c4_half_w, c3_r1_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_BufferMs), m_renderer.FontSmall(), c4_buf_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c4_buf_val = D2D1::RectF(c4_right_col, c3_r1_top + 14.0f, c4_right_col + c4_half_w, c3_r1_top + 36.0f);
    m_renderer.DrawTextSimple(std::format(L"{:.1f} ms", state.audio_buffer_ms), m_renderer.FontSubheader(), c4_buf_val, m_renderer.BrushTextPrimary());

    // Row 2: Underruns & Mute State
    D2D1_RECT_F c4_und_lbl = D2D1::RectF(c4_left_col, c3_r2_top, c4_left_col + c4_half_w, c3_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_RealUnderruns), m_renderer.FontSmall(), c4_und_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c4_und_val = D2D1::RectF(c4_left_col, c3_r2_top + 14.0f, c4_left_col + c4_half_w, c3_r2_top + 36.0f);
    m_renderer.DrawTextSimple(std::format(L"{}", state.audio_underruns), m_renderer.FontBodyBold(), c4_und_val, state.audio_underruns > 0 ? m_renderer.BrushStatusAmber() : m_renderer.BrushTextSecondary());

    D2D1_RECT_F c4_mut_lbl = D2D1::RectF(c4_right_col, c3_r2_top, c4_right_col + c4_half_w, c3_r2_top + 14.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Perf_Lbl_AudioMuteState), m_renderer.FontSmall(), c4_mut_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F c4_mut_val = D2D1::RectF(c4_right_col, c3_r2_top + 14.0f, c4_right_col + c4_half_w, c3_r2_top + 36.0f);
    m_renderer.DrawTextSimple(state.audio_muted ? loc::Get(loc::S::Perf_MutedSilentBuffer) : loc::Get(loc::S::Perf_UnmutedPlaying), m_renderer.FontBodyBold(), c4_mut_val, m_renderer.BrushTextSecondary());

    // Secondary line: Device & Format
    D2D1_RECT_F c4_sec = D2D1::RectF(c4_left_col, c3_r3_top, c4_rc.right - 14.0f, c4_rc.bottom - 6.0f);
    std::wstring c4_dev = state.resolved_audio_device_name.empty() || state.resolved_audio_device_name == L"—" ? state.audio_device_name : state.resolved_audio_device_name;
    std::wstring c4_sec_text = std::format(
        L"{}: {}  •  {}",
        loc::Get(loc::S::Audio_OutputDevice), c4_dev.empty() ? L"—" : c4_dev,
        loc::Get(loc::S::Perf_Lbl_EngineFormat));
    m_renderer.DrawTextSimple(c4_sec_text, m_renderer.FontSmall(), c4_sec, m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    if (state.width > state.output_width && state.output_width > 0) {
        D2D1_RECT_F warning = D2D1::RectF(card_rc.left + 24.0f, grid_row2_top + row_h + 8.0f,
                                          card_rc.right - 24.0f, grid_row2_top + row_h + 38.0f);
        m_renderer.DrawTextSimple(
            loc::Get(loc::S::Perf_ResolutionWarning),
            m_renderer.FontSmall(), warning, m_renderer.BrushBrandBlue());
    }
}

void MainWindowView::RenderDiagnosticsView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    RenderPerformanceView(state, area);
}

void MainWindowView::RenderScreenOnlyView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    m_renderer.Clear(colors::Background);

    float pill_w = 260.0f;
    float pill_h = 36.0f;
    float pill_x = area.right - pill_w - 24.0f;
    float pill_y = area.top + 24.0f;
    D2D1_RECT_F pill_rc = D2D1::RectF(pill_x, pill_y, pill_x + pill_w, pill_y + pill_h);

    m_renderer.DrawCard(pill_rc, false, 6.0f);

    float btn_w = 120.0f;
    D2D1_RECT_F back_btn = D2D1::RectF(pill_rc.left + 4.0f, pill_rc.top + 4.0f, pill_rc.left + 4.0f + btn_w, pill_rc.bottom - 4.0f);
    RegisterClickable(back_btn, Control_Btn_ToggleScreenOnly, loc::Get(loc::S::Common_BackToWorkspace));
    m_renderer.DrawButton(back_btn, loc::Get(loc::S::Common_BackToWorkspace), true,
        state.hovered_control == Control_Btn_ToggleScreenOnly, state.pressed_control == Control_Btn_ToggleScreenOnly, IconType::Monitor);

    D2D1_RECT_F full_btn = D2D1::RectF(back_btn.right + 6.0f, pill_rc.top + 4.0f, back_btn.right + 6.0f + 56.0f, pill_rc.bottom - 4.0f);
    RegisterClickable(full_btn, Control_Btn_Fullscreen);
    m_renderer.DrawButton(full_btn, state.output_fullscreen ? L"Windowed" : loc::Get(loc::S::Right_Btn_Full),
        state.output_fullscreen, state.hovered_control == Control_Btn_Fullscreen, state.pressed_control == Control_Btn_Fullscreen, IconType::Maximize);

    D2D1_RECT_F mute_btn = D2D1::RectF(full_btn.right + 6.0f, pill_rc.top + 4.0f, pill_rc.right - 4.0f, pill_rc.bottom - 4.0f);
    RegisterClickable(mute_btn, Control_Btn_Mute);
    m_renderer.DrawButton(mute_btn, state.audio_muted ? loc::Get(loc::S::Right_Btn_Unmute) : loc::Get(loc::S::Right_Btn_Mute),
        state.audio_muted, state.hovered_control == Control_Btn_Mute, state.pressed_control == Control_Btn_Mute, IconType::Speaker);
}

void MainWindowView::RenderSettingsView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    // Settings sub-tab navigation strip
    float content_top = 0.0f;
    RenderSettingsSubNav(state, area, content_top);

    const D2D1_RECT_F sub_area = D2D1::RectF(area.left, content_top, area.right, area.bottom);

    switch (state.active_settings_sub_tab) {
    case SettingsSubTab::General:  RenderGeneralPage(state, sub_area);  break;
    case SettingsSubTab::Output:   RenderOutputPage(state, sub_area);   break;
    case SettingsSubTab::Network:  RenderNetworkPage(state, sub_area);  break;
    case SettingsSubTab::Privacy:  RenderPrivacyPage(state, sub_area);  break;
    case SettingsSubTab::Advanced: RenderAdvancedPage(state, sub_area); break;
    }
    m_content_height += content_top - area.top;
}

void MainWindowView::RenderSettingsSubNav(const UiState& state, const D2D1_RECT_F& area, float& out_content_top) noexcept {
    const float pad = 20.0f;
    const float strip_h = 36.0f;
    const float strip_top = area.top + 4.0f;
    const float strip_bottom = strip_top + strip_h;

    // Sub-tab labels
    struct SubTabInfo { SettingsSubTab tab; loc::S loc_id; int ctrl; };
    static const SubTabInfo kTabs[] = {
        { SettingsSubTab::General,  loc::S::Settings_General,      Control_SubTab_General  },
        { SettingsSubTab::Output,   loc::S::Settings_Output,       Control_SubTab_Output   },
        { SettingsSubTab::Network,  loc::S::Settings_Network,      Control_SubTab_Network  },
        { SettingsSubTab::Privacy,  loc::S::Settings_Privacy,      Control_SubTab_Privacy  },
        { SettingsSubTab::Advanced, loc::S::Settings_Advanced,     Control_SubTab_Advanced },
    };

    float x = area.left + pad;
    for (const auto& t : kTabs) {
        const wchar_t* label = loc::Get(t.loc_id);
        // Measure text width for tab sizing
        float tab_w = 90.0f;
        if (t.tab == SettingsSubTab::Privacy) tab_w = 120.0f;

        const D2D1_RECT_F tab_rc = D2D1::RectF(x, strip_top, x + tab_w, strip_bottom);
        const bool is_active = (state.active_settings_sub_tab == t.tab);
        const bool is_hovered = (state.hovered_control == t.ctrl);

        // Active tab: accent underline + primary text; inactive: secondary text
        if (is_active) {
            // Underline indicator
            const D2D1_RECT_F line_rc = D2D1::RectF(x, strip_bottom - 2.0f, x + tab_w, strip_bottom);
            m_renderer.FillRect(line_rc, m_renderer.BrushBrandBlue());
            m_renderer.DrawTextSimple(label, m_renderer.FontBody(), tab_rc,
                m_renderer.BrushTextPrimary(),
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            if (is_hovered) {
                m_renderer.FillRoundedRect(tab_rc, 4.0f, m_renderer.BrushCardBorder());
            }
            m_renderer.DrawTextSimple(label, m_renderer.FontBody(), tab_rc,
                m_renderer.BrushTextSecondary(),
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        RegisterClickable(tab_rc, t.ctrl, L"", true);
        x += tab_w + 4.0f;
    }

    // Separator line below strip
    const D2D1_RECT_F sep_rc = D2D1::RectF(area.left + pad, strip_bottom + 1.0f, area.right - pad, strip_bottom + 2.0f);
    m_renderer.FillRect(sep_rc, m_renderer.BrushCardBorder());

    out_content_top = strip_bottom + 8.0f;
}

void MainWindowView::RenderVideoView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) {
        content_right -= 12.0f;
    }
    const float content_w = content_right - content_left;

    // Header Title & Description
    D2D1_RECT_F title_rc = D2D1::RectF(content_left, area.top + 14.0f, content_right, area.top + 38.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Video_Title), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    D2D1_RECT_F sub_rc = D2D1::RectF(content_left, area.top + 38.0f, content_right, area.top + 56.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::Video_Subtitle),
        m_renderer.FontSmall(), sub_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    const float gap = 16.0f;
    const bool two_col = (content_w >= 700.0f);
    const float col_w = two_col ? (content_w - gap) * 0.5f : content_w;
    const float col1_x = content_left;
    const float col2_x = two_col ? (col1_x + col_w + gap) : col1_x;
    float col1_y = area.top + 62.0f;
    float col2_y = two_col ? col1_y : 0.0f;

    auto draw_selector_row = [&](float x, float y, float w, int id, std::wstring_view name, std::wstring_view val, bool has_custom_btn = false) {
        float row_w = has_custom_btn ? w - 82.0f : w;
        D2D1_RECT_F rc = D2D1::RectF(x, y, x + row_w, y + 36.0f);
        m_renderer.DrawInset(rc, 7.0f);
        RegisterClickable(rc, id, L"", true);

        if (state.open_dropdown == id) {
            m_dropdown_anchor_rc = rc;
            m_renderer.DrawRoundedRect(rc, 7.0f, m_renderer.BrushBrandBlue(), 1.2f);
        } else if (state.hovered_control == id) {
            m_renderer.DrawRoundedRect(rc, 7.0f, m_renderer.BrushCardBorder(), 1.0f);
        }

        D2D1_RECT_F label_rc = D2D1::RectF(x + 10.0f, y + 2.0f, x + row_w * 0.44f, y + 34.0f);
        m_renderer.DrawTextSimple(name, m_renderer.FontSmallBold(), label_rc,
                                  m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        std::wstring disp = std::format(L"{}  ▼", val);
        D2D1_RECT_F val_rc = D2D1::RectF(x + row_w * 0.44f, y + 2.0f, x + row_w - 10.0f, y + 34.0f);
        m_renderer.DrawTextSimple(disp, m_renderer.FontSmall(), val_rc,
                                  m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    // -------------------------------------------------------------
    // Section 1: INPUT (AirPlay / Source Input)
    // -------------------------------------------------------------
    float in_card_h = state.receiver_quality_pending ? 154.0f : 132.0f;
    D2D1_RECT_F in_card = D2D1::RectF(col1_x, col1_y, col1_x + col_w, col1_y + in_card_h);
    m_renderer.DrawCard(in_card, false, metrics::CardRadius);

    D2D1_RECT_F in_header = D2D1::RectF(in_card.left + 14.0f, in_card.top + 7.0f, in_card.right - 14.0f, in_card.top + 22.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_AirPlayInput), m_renderer.FontSmallBold(), in_header, m_renderer.BrushBrandCyan());

    std::wstring rec_desc;
    if (state.receiver_quality == 0) {
        rec_desc = loc::Get(loc::S::Opt_Rec_Auto);
    } else {
        const std::wstring_view rec_names[] = {
            loc::Get(loc::S::Opt_Rec_Auto),
            loc::Get(loc::S::Opt_Rec_720p30),
            loc::Get(loc::S::Opt_Rec_720p60),
            loc::Get(loc::S::Opt_Rec_1080p30),
            loc::Get(loc::S::Opt_Rec_1080p60),
            loc::Get(loc::S::Opt_Rec_1440p60),
            loc::Get(loc::S::Opt_Rec_Original60)
        };
        rec_desc = rec_names[std::clamp(state.receiver_quality, 0, 6)];
    }
    draw_selector_row(in_card.left + 14.0f, in_card.top + 25.0f, col_w - 28.0f, Control_Set_Receiver,
                      loc::Get(loc::S::Video_ReceiverQuality), rec_desc);

    float row2_y = in_card.top + 65.0f;
    float half_w = (col_w - 36.0f) * 0.5f;

    // Requested Quality Class Box
    D2D1_RECT_F req_rc = D2D1::RectF(in_card.left + 14.0f, row2_y, in_card.left + 14.0f + half_w, row2_y + 36.0f);
    m_renderer.DrawInset(req_rc, 6.0f);
    D2D1_RECT_F req_lbl = D2D1::RectF(req_rc.left + 8.0f, req_rc.top + 2.0f, req_rc.right - 8.0f, req_rc.top + 18.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Requested), m_renderer.FontSmall(), req_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F req_val = D2D1::RectF(req_rc.left + 8.0f, req_rc.top + 16.0f, req_rc.right - 8.0f, req_rc.bottom - 2.0f);
    std::wstring req_str = state.requested_quality_class.empty() || state.requested_quality_class == L"—" ? rec_desc : state.requested_quality_class;
    m_renderer.DrawTextSimple(req_str, m_renderer.FontSmallBold(), req_val, m_renderer.BrushBrandCyan());

    // Actual Source Resolution & Framerate Box
    D2D1_RECT_F src_rc = D2D1::RectF(in_card.left + 14.0f + half_w + 8.0f, row2_y, in_card.right - 14.0f, row2_y + 36.0f);
    m_renderer.DrawInset(src_rc, 6.0f);
    D2D1_RECT_F src_lbl = D2D1::RectF(src_rc.left + 8.0f, src_rc.top + 2.0f, src_rc.right - 8.0f, src_rc.top + 18.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_ActualSource), m_renderer.FontSmall(), src_lbl, m_renderer.BrushTextMuted());
    D2D1_RECT_F src_val = D2D1::RectF(src_rc.left + 8.0f, src_rc.top + 16.0f, src_rc.right - 8.0f, src_rc.bottom - 2.0f);
    std::wstring src_str = (state.width > 0 && state.height > 0)
        ? std::format(L"{} × {} ({} FPS)", state.width, state.height, static_cast<int>(std::round(state.nominal_fps > 0.0 ? state.nominal_fps : (state.source_fps > 0.0 ? state.source_fps : 60.0))))
        : L"—";
    m_renderer.DrawTextSimple(src_str, m_renderer.FontSmallBold(), src_val, m_renderer.BrushTextPrimary());

    if (state.receiver_quality_pending) {
        D2D1_RECT_F ban_rc = D2D1::RectF(in_card.left + 14.0f, row2_y + 39.0f, in_card.right - 14.0f, row2_y + 57.0f);
        std::wstring ban_text = std::format(
            L"⚠ {}",
            loc::Get(loc::S::Video_PendingReconnect));
        m_renderer.DrawTextSimple(ban_text, m_renderer.FontSmallBold(), ban_rc, m_renderer.BrushStatusAmber());
    } else {
        D2D1_RECT_F hlp_rc = D2D1::RectF(in_card.left + 14.0f, row2_y + 39.0f, in_card.right - 14.0f, row2_y + 57.0f);
        std::wstring status_info;
        if (!state.quality_state_desc.empty() && state.quality_state_desc != L"—" && state.width > 0) {
            status_info = std::format(
                L"{} • {}",
                state.quality_state_desc,
                state.orientation_desc.empty() || state.orientation_desc == L"—" ? loc::Get(loc::S::Status_Ready) : state.orientation_desc);
        } else {
            status_info = std::format(
                L"{} • {}",
                loc::Get(loc::S::Video_SidecarActive),
                state.orientation_desc.empty() || state.orientation_desc == L"—" ? loc::Get(loc::S::Status_Ready) : state.orientation_desc);
        }
        ID2D1Brush* status_brush = (state.quality_effectiveness == 2)
            ? m_renderer.BrushStatusAmber()
            : (state.quality_effectiveness == 1)
                ? m_renderer.BrushStatusGreen()
                : m_renderer.BrushTextMuted();
        m_renderer.DrawTextSimple(status_info, m_renderer.FontSmall(), hlp_rc, status_brush);
    }

    col1_y += in_card_h + 8.0f;

    // Receiver delivery policy applies live and is independent of source quality.
    const bool custom_delivery = state.streaming_mode == 3;
    const float delivery_h = custom_delivery ? 180.0f : 100.0f;
    const D2D1_RECT_F delivery_card = D2D1::RectF(col1_x, col1_y, col1_x + col_w, col1_y + delivery_h);
    m_renderer.DrawCard(delivery_card, false, metrics::CardRadius);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Delivery), m_renderer.FontSmallBold(),
        D2D1::RectF(delivery_card.left + 14.0f, delivery_card.top + 7.0f, delivery_card.right - 14.0f, delivery_card.top + 22.0f),
        m_renderer.BrushBrandCyan());
    const std::wstring_view delivery_names[] = {
        loc::Get(loc::S::Opt_Delivery_Balanced), loc::Get(loc::S::Opt_Delivery_Fastest),
        loc::Get(loc::S::Opt_Delivery_Smooth), loc::Get(loc::S::Opt_Delivery_Custom)
    };
    draw_selector_row(delivery_card.left + 14.0f, delivery_card.top + 25.0f, col_w - 28.0f,
        Control_Set_StreamingMode, loc::Get(loc::S::Video_DeliveryMode),
        delivery_names[std::clamp(state.streaming_mode, 0, 3)]);
    if (custom_delivery) {
        draw_selector_row(delivery_card.left + 14.0f, delivery_card.top + 65.0f, col_w - 28.0f,
            Control_Set_VideoFreshness, loc::Get(loc::S::Video_Freshness),
            std::format(L"{} ms", state.custom_video_freshness_ms));
        draw_selector_row(delivery_card.left + 14.0f, delivery_card.top + 105.0f, col_w - 28.0f,
            Control_Set_VideoQueueFrames, loc::Get(loc::S::Video_QueueFrames),
            std::format(L"{}", state.custom_video_queue_frames));
    }
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_DeliveryHint), m_renderer.FontSmall(),
        D2D1::RectF(delivery_card.left + 14.0f, delivery_card.bottom - 32.0f,
                    delivery_card.right - 14.0f, delivery_card.bottom - 7.0f),
        m_renderer.BrushTextMuted());
    col1_y += delivery_h + 8.0f;

    // -------------------------------------------------------------
    // Section 2: OUTPUT
    // -------------------------------------------------------------
    float out_card_h = 247.0f;
    D2D1_RECT_F out_card = D2D1::RectF(col1_x, col1_y, col1_x + col_w, col1_y + out_card_h);
    m_renderer.DrawCard(out_card, false, metrics::CardRadius);

    D2D1_RECT_F out_header = D2D1::RectF(out_card.left + 14.0f, out_card.top + 7.0f, out_card.right - 14.0f, out_card.top + 22.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Output), m_renderer.FontSmallBold(), out_header, m_renderer.BrushBrandBlue());

    const std::wstring_view out_quality_names[] = {
        loc::Get(loc::S::Opt_Out_Auto),
        loc::Get(loc::S::Opt_Out_HD),
        loc::Get(loc::S::Opt_Out_FullHD),
        loc::Get(loc::S::Opt_Out_2K),
        loc::Get(loc::S::Opt_Out_4K),
        loc::Get(loc::S::Opt_Out_Original),
        loc::Get(loc::S::Opt_Out_Custom)
    };
    std::wstring out_q_str = std::wstring(out_quality_names[std::clamp(state.output_quality, 0, 6)]);
    draw_selector_row(out_card.left + 14.0f, out_card.top + 24.0f, col_w - 28.0f, Control_Set_Output,
                      loc::Get(loc::S::Output_Quality), out_q_str, true);

    D2D1_RECT_F custom_btn = D2D1::RectF(out_card.right - 14.0f - 76.0f, out_card.top + 24.0f, out_card.right - 14.0f, out_card.top + 60.0f);
    RegisterClickable(custom_btn, Control_Set_CustomOutput, L"", true);
    m_renderer.DrawButton(custom_btn, loc::Get(loc::S::Common_Custom), false,
                          state.hovered_control == Control_Set_CustomOutput,
                          state.pressed_control == Control_Set_CustomOutput);

    const std::wstring_view canvas_names[] = {
        loc::Get(loc::S::Opt_Canvas_FollowSource),
        loc::Get(loc::S::Opt_Canvas_16_9_HD),
        loc::Get(loc::S::Opt_Canvas_16_9_FullHD),
        loc::Get(loc::S::Opt_Canvas_16_9_2K),
        loc::Get(loc::S::Opt_Canvas_Custom)
    };
    draw_selector_row(out_card.left + 14.0f, out_card.top + 63.0f, col_w - 28.0f, Control_Set_CaptureCanvas,
                      loc::Get(loc::S::Output_CaptureCanvas), canvas_names[std::clamp(state.capture_canvas, 0, 4)]);

    std::wstring aspect_str;
    if (state.capture_canvas == 0) {
        aspect_str = loc::Get(loc::S::Opt_Aspect_Preserve);
    } else {
        const std::wstring_view fixed_aspect_names[] = {
            loc::Get(loc::S::Opt_Aspect_Fit),
            loc::Get(loc::S::Opt_Aspect_Fill),
            loc::Get(loc::S::Opt_Aspect_Stretch)
        };
        int fixed_idx = std::clamp(state.aspect_mode - 1, 0, 2);
        aspect_str = fixed_aspect_names[fixed_idx];
    }
    draw_selector_row(out_card.left + 14.0f, out_card.top + 102.0f, col_w - 28.0f, Control_Set_AspectMode,
                      loc::Get(loc::S::Output_AspectMode), aspect_str);

    const std::wstring_view scalers[] = {
        loc::Get(loc::S::Opt_Scale_Auto),
        loc::Get(loc::S::Opt_Scale_Fast),
        loc::Get(loc::S::Opt_Scale_Sharp),
        loc::Get(loc::S::Opt_Scale_HighQuality)
    };
    draw_selector_row(out_card.left + 14.0f, out_card.top + 141.0f, col_w - 28.0f, Control_Set_Scaling,
                      loc::Get(loc::S::Output_ScalingQuality), scalers[std::clamp(state.scaling_quality, 0, 3)]);

    const std::wstring_view pixel_perfect_names[] = {
        loc::Get(loc::S::Opt_Pixel_Auto),
        loc::Get(loc::S::Opt_Pixel_On),
        loc::Get(loc::S::Opt_Pixel_Off)
    };
    draw_selector_row(out_card.left + 14.0f, out_card.top + 180.0f, col_w - 28.0f, Control_Set_PixelPerfect,
                      loc::Get(loc::S::Output_PixelPerfect), pixel_perfect_names[std::clamp(state.pixel_perfect_mode, 0, 2)]);

    D2D1_RECT_F summary_rc = D2D1::RectF(out_card.left + 14.0f, out_card.top + 220.0f, out_card.right - 14.0f, out_card.top + 241.0f);
    m_renderer.DrawInset(summary_rc, 5.0f);
    std::wstring trans_str;
    if (state.width > 0 && state.height > 0) {
        const double ar = static_cast<double>(state.width) / static_cast<double>(state.height);
        if (state.capture_canvas == 0) {
            trans_str = std::format(L"{}: {}×{} ({:.2f}:1) → {}: {}×{} ({})",
                                    loc::Get(loc::S::Perf_Lbl_Source),
                                    state.width, state.height, ar,
                                    loc::Get(loc::S::Video_ActiveCanvas),
                                    state.output_width, state.output_height,
                                    out_q_str);
        } else {
            trans_str = std::format(L"{}: {}×{} ({:.2f}:1) → {}: {}×{} ({} · {})",
                                    loc::Get(loc::S::Perf_Lbl_Source),
                                    state.width, state.height, ar,
                                    loc::Get(loc::S::Video_ActiveCanvas),
                                    state.output_width, state.output_height,
                                    canvas_names[std::clamp(state.capture_canvas, 0, 4)],
                                    aspect_str);
        }
    } else {
        trans_str = loc::Get(loc::S::Video_CanvasMatchDesc);
    }
    D2D1_RECT_F sum_txt = D2D1::RectF(summary_rc.left + 8.0f, summary_rc.top, summary_rc.right - 8.0f, summary_rc.bottom);
    m_renderer.DrawTextSimple(trans_str, m_renderer.FontSmall(), sum_txt,
                              m_renderer.BrushBrandCyan(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    col1_y += out_card_h + 8.0f;

    // -------------------------------------------------------------
    // Section 3: PREVIEW & RENDERING
    // -------------------------------------------------------------
    float rnd_card_h = 132.0f;
    D2D1_RECT_F rnd_card = D2D1::RectF(col1_x, col1_y, col1_x + col_w, col1_y + rnd_card_h);
    m_renderer.DrawCard(rnd_card, false, metrics::CardRadius);

    D2D1_RECT_F rnd_header = D2D1::RectF(rnd_card.left + 14.0f, rnd_card.top + 7.0f, rnd_card.right - 14.0f, rnd_card.top + 22.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Rendering), m_renderer.FontSmallBold(), rnd_header, m_renderer.BrushBrandPurple());

    const std::wstring_view renderers[] = {
        loc::Get(loc::S::Opt_Rnd_Auto),
        loc::Get(loc::S::Opt_Rnd_D3D11),
        loc::Get(loc::S::Opt_Rnd_Compat),
        loc::Get(loc::S::Opt_Rnd_Warp)
    };
    draw_selector_row(rnd_card.left + 14.0f, rnd_card.top + 24.0f, col_w - 28.0f, Control_Set_Renderer,
                      loc::Get(loc::S::Video_RendererMode), renderers[std::clamp(state.renderer_mode, 0, 3)]);

    float diag_y1 = rnd_card.top + 64.0f;
    float diag_y2 = rnd_card.top + 95.0f;
    float diag_w = (col_w - 36.0f) * 0.5f;

    auto draw_diag_box = [&](float x, float y, std::wstring_view lbl, std::wstring_view val, bool is_active) {
        D2D1_RECT_F rc = D2D1::RectF(x, y, x + diag_w, y + 28.0f);
        m_renderer.DrawInset(rc, 5.0f);
        D2D1_RECT_F lrc = D2D1::RectF(x + 8.0f, y, x + diag_w * 0.45f, y + 28.0f);
        m_renderer.DrawTextSimple(lbl, m_renderer.FontSmall(), lrc, m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        D2D1_RECT_F vrc = D2D1::RectF(x + diag_w * 0.45f, y, x + diag_w - 8.0f, y + 28.0f);
        m_renderer.DrawTextSimple(val, m_renderer.FontSmallBold(), vrc, is_active ? m_renderer.BrushBrandBlue() : m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    draw_diag_box(rnd_card.left + 14.0f, diag_y1, loc::Get(loc::S::Video_Resolved), state.renderer_name.empty() ? L"—" : state.renderer_name, true);
    draw_diag_box(rnd_card.left + 14.0f + diag_w + 8.0f, diag_y1, loc::Get(loc::S::Right_Tile_Decoder), state.decoder_name.empty() ? L"—" : state.decoder_name, true);
    draw_diag_box(rnd_card.left + 14.0f, diag_y2, loc::Get(loc::S::Sidebar_ZeroCopyTitle), state.zero_copy ? loc::Get(loc::S::Video_ZeroCopyActive) : loc::Get(loc::S::Video_ZeroCopyDisabled), state.zero_copy);
    draw_diag_box(rnd_card.left + 14.0f + diag_w + 8.0f, diag_y2, L"GPU", state.gpu_name.empty() ? L"—" : state.gpu_name, false);

    col1_y += rnd_card_h + 8.0f;

    // -------------------------------------------------------------
    // Right Column (or stacked below in single-column)
    // -------------------------------------------------------------
    if (!two_col) col2_y = col1_y;

    // -------------------------------------------------------------
    // Section 4: COLOR GRADING
    // -------------------------------------------------------------
    float clr_card_h = 356.0f;
    D2D1_RECT_F clr_card = D2D1::RectF(col2_x, col2_y, col2_x + col_w, col2_y + clr_card_h);
    m_renderer.DrawCard(clr_card, false, metrics::CardRadius);

    D2D1_RECT_F clr_header = D2D1::RectF(clr_card.left + 14.0f, clr_card.top + 7.0f, clr_card.left + 74.0f, clr_card.top + 24.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Color), m_renderer.FontSmallBold(), clr_header, m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F live_badge = D2D1::RectF(clr_card.left + 76.0f, clr_card.top + 6.0f, clr_card.left + 120.0f, clr_card.top + 24.0f);
    m_renderer.DrawBadge(live_badge, loc::Get(loc::S::Video_Live), D2D1::ColorF(0.133f, 0.773f, 0.369f, 0.16f), colors::StatusGreen, true);

    const std::wstring_view preset_names[] = {
        loc::Get(loc::S::Opt_Color_Neutral),
        loc::Get(loc::S::Opt_Color_Vivid),
        loc::Get(loc::S::Opt_Color_Soft),
        loc::Get(loc::S::Opt_Color_Custom)
    };
    float preset_btn_w = 110.0f;
    D2D1_RECT_F preset_rc = D2D1::RectF(clr_card.right - 14.0f - 66.0f - 8.0f - preset_btn_w, clr_card.top + 5.0f,
                                       clr_card.right - 14.0f - 66.0f - 8.0f, clr_card.top + 27.0f);
    m_renderer.DrawInset(preset_rc, 5.0f);
    RegisterClickable(preset_rc, Control_Set_ColorPreset, L"", true);
    if (state.open_dropdown == Control_Set_ColorPreset) {
        m_dropdown_anchor_rc = preset_rc;
        m_renderer.DrawRoundedRect(preset_rc, 5.0f, m_renderer.BrushBrandBlue(), 1.2f);
    }
    std::wstring preset_str = std::format(L"{}  ▼", preset_names[std::clamp(state.color_preset, 0, 3)]);
    m_renderer.DrawTextSimple(preset_str, m_renderer.FontSmall(), preset_rc,
                              m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F reset_rc = D2D1::RectF(clr_card.right - 14.0f - 66.0f, clr_card.top + 5.0f, clr_card.right - 14.0f, clr_card.top + 27.0f);
    RegisterClickable(reset_rc, Control_Set_ResetColor, loc::Get(loc::S::Video_ResetTooltip), true);
    m_renderer.DrawButton(reset_rc, loc::Get(loc::S::Common_Reset), false,
                          state.hovered_control == Control_Set_ResetColor,
                          state.pressed_control == Control_Set_ResetColor);

    const std::wstring_view slider_names[5] = {
        loc::Get(loc::S::Output_Brightness),
        loc::Get(loc::S::Output_Contrast),
        loc::Get(loc::S::Output_Saturation),
        loc::Get(loc::S::Output_Hue),
        loc::Get(loc::S::Output_Sharpness)
    };
    const int values[5] = { state.brightness, state.contrast, state.saturation, state.hue, state.sharpness };

    float sy = clr_card.top + 34.0f;
    const float srow_h = 42.0f;
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F row_rc = D2D1::RectF(clr_card.left + 14.0f, sy, clr_card.right - 14.0f, sy + srow_h);
        m_renderer.DrawInset(row_rc, 7.0f);

        const int id = Control_Set_Brightness + i;
        if (state.filter_supported[i]) {
            RegisterClickable(row_rc, id, loc::Get(loc::S::Video_ResetToZeroTooltip), true);
        }

        D2D1_RECT_F name_rc = D2D1::RectF(row_rc.left + 10.0f, sy + 4.0f, row_rc.left + 90.0f, sy + srow_h - 4.0f);
        m_renderer.DrawTextSimple(slider_names[i], m_renderer.FontSmallBold(), name_rc,
                                  state.filter_supported[i] ? m_renderer.BrushTextPrimary() : m_renderer.BrushTextMuted(),
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (state.filter_supported[i]) {
            const float x0 = row_rc.left + 95.0f;
            const float x1 = row_rc.right - 95.0f;
            const float y_mid = sy + srow_h * 0.5f;

            m_renderer.DrawLine(D2D1::Point2F(x0, y_mid), D2D1::Point2F(x1, y_mid), m_renderer.BrushCardBorder(), 3.0f);

            if (i == 4) {
                m_renderer.DrawLine(D2D1::Point2F(x0, y_mid - 6.0f), D2D1::Point2F(x0, y_mid + 6.0f), m_renderer.BrushTextMuted(), 2.0f);
            } else {
                const float x_center = (x0 + x1) * 0.5f;
                m_renderer.DrawLine(D2D1::Point2F(x_center, y_mid - 6.0f), D2D1::Point2F(x_center, y_mid + 6.0f), m_renderer.BrushTextMuted(), 2.0f);
            }

            float knob_x = 0.0f;
            if (i == 4) {
                knob_x = x0 + (x1 - x0) * (std::clamp(values[i], 0, 100) / 100.0f);
            } else {
                knob_x = x0 + (x1 - x0) * ((std::clamp(values[i], -100, 100) + 100) / 200.0f);
            }
            m_renderer.DrawStatusDot(D2D1::Point2F(knob_x, y_mid), 5.5f, colors::BrandBlue, true);

            D2D1_RECT_F num_rc = D2D1::RectF(row_rc.right - 88.0f, y_mid - 12.0f, row_rc.right - 36.0f, y_mid + 12.0f);
            m_renderer.DrawInset(num_rc, 4.0f);
            std::wstring num_str = (i != 4 && values[i] > 0) ? std::format(L"+{}", values[i]) : std::format(L"{}", values[i]);
            m_renderer.DrawTextSimple(num_str, m_renderer.FontSmallBold(), num_rc,
                                      values[i] != 0 ? m_renderer.BrushBrandBlue() : m_renderer.BrushTextMuted(),
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            D2D1_RECT_F rst_rc = D2D1::RectF(row_rc.right - 30.0f, y_mid - 12.0f, row_rc.right - 6.0f, y_mid + 12.0f);
            const int reset_id = Control_Reset_Brightness + i;
            RegisterClickable(rst_rc, reset_id, loc::Get(loc::S::Video_ResetTooltip), true);
            m_renderer.DrawButton(rst_rc, L"↺", false,
                                  state.hovered_control == reset_id,
                                  state.pressed_control == reset_id);
        } else {
            D2D1_RECT_F unsupp_rc = D2D1::RectF(row_rc.left + 100.0f, sy, row_rc.right - 10.0f, sy + srow_h);
            m_renderer.DrawTextSimple(loc::Get(loc::S::Video_FilterNotSupported), m_renderer.FontSmall(), unsupp_rc,
                                      m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        sy += srow_h + 6.0f;
    }

    col2_y += clr_card_h + 8.0f;

    // -------------------------------------------------------------
    // Section 5: ADVANCED (Performance Profile, Color Range, Matrix & Pipeline)
    // -------------------------------------------------------------
    float adv_card_h = state.advanced_color_expanded ? 226.0f : 40.0f;
    D2D1_RECT_F adv_card = D2D1::RectF(col2_x, col2_y, col2_x + col_w, col2_y + adv_card_h);
    m_renderer.DrawCard(adv_card, false, metrics::CardRadius);

    D2D1_RECT_F adv_hdr_btn = D2D1::RectF(adv_card.left + 14.0f, adv_card.top + 4.0f, adv_card.right - 14.0f, adv_card.top + 34.0f);
    RegisterClickable(adv_hdr_btn, Control_Toggle_AdvancedColor, L"", true);

    std::wstring adv_title = std::format(L"{}  {}", state.advanced_color_expanded ? L"▼" : L"▶", loc::Get(loc::S::Video_AdvColorOverview));
    m_renderer.DrawTextSimple(adv_title, m_renderer.FontSmallBold(), adv_hdr_btn,
                              m_renderer.BrushBrandPurple(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    std::wstring_view toggle_lbl = state.advanced_color_expanded ? loc::Get(loc::S::Video_Collapse) : loc::Get(loc::S::Video_Expand);
    m_renderer.DrawTextSimple(toggle_lbl, m_renderer.FontSmall(), adv_hdr_btn,
                              m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (state.advanced_color_expanded) {
        const std::wstring_view profile_names[] = {
            loc::Get(loc::S::Opt_Profile_Auto),
            loc::Get(loc::S::Opt_Profile_HighQuality),
            loc::Get(loc::S::Opt_Profile_Balanced),
            loc::Get(loc::S::Opt_Profile_LowSpec),
            loc::Get(loc::S::Opt_Profile_Custom)
        };
        draw_selector_row(adv_card.left + 14.0f, adv_card.top + 38.0f, col_w - 28.0f, Control_Set_Profile,
                          loc::Get(loc::S::Output_Profile), profile_names[std::clamp(state.performance_profile, 0, 4)]);

        const std::wstring_view ranges[] = {
            loc::Get(loc::S::Opt_Range_Auto),
            loc::Get(loc::S::Opt_Range_Limited),
            loc::Get(loc::S::Opt_Range_Full)
        };
        draw_selector_row(adv_card.left + 14.0f, adv_card.top + 78.0f, col_w - 28.0f, Control_Set_ColorRange,
                          loc::Get(loc::S::Output_ColorRange), ranges[std::clamp(state.color_range, 0, 2)]);

        const std::wstring_view matrices[] = {
            loc::Get(loc::S::Opt_Matrix_Auto),
            loc::Get(loc::S::Opt_Matrix_601),
            loc::Get(loc::S::Opt_Matrix_709),
            loc::Get(loc::S::Opt_Matrix_2020)
        };
        draw_selector_row(adv_card.left + 14.0f, adv_card.top + 118.0f, col_w - 28.0f, Control_Set_ColorMatrix,
                          loc::Get(loc::S::Output_ColorMatrix), matrices[std::clamp(state.color_matrix, 0, 3)]);

        D2D1_RECT_F ov_rc = D2D1::RectF(adv_card.left + 14.0f, adv_card.top + 158.0f, adv_card.right - 14.0f, adv_card.top + 214.0f);
        m_renderer.DrawInset(ov_rc, 6.0f);

        std::wstring ov_txt = std::format(
            L"{}: {}×{} ({})\n"
            L"{}: {}\n"
            L"{}: {}, {}",
            loc::Get(loc::S::Video_ActiveCanvas), state.output_width, state.output_height, state.match_source ? loc::Get(loc::S::Video_AutoMatchSource) : loc::Get(loc::S::Video_Fixed),
            loc::Get(loc::S::Video_AspectBehavior), state.capture_canvas == 0 ? loc::Get(loc::S::Opt_Aspect_Preserve) : (state.aspect_mode == 1 ? loc::Get(loc::S::Opt_Aspect_Fit) : (state.aspect_mode == 2 ? loc::Get(loc::S::Opt_Aspect_Fill) : loc::Get(loc::S::Opt_Aspect_Stretch))),
            loc::Get(loc::S::Video_ColorPipeline),
            state.color_range == 0 ? loc::Get(loc::S::Video_AutoRange) : (state.color_range == 1 ? loc::Get(loc::S::Opt_Range_Limited) : loc::Get(loc::S::Opt_Range_Full)),
            state.color_matrix == 0 ? loc::Get(loc::S::Video_AutoMatrix) : (state.color_matrix == 1 ? loc::Get(loc::S::Opt_Matrix_601) : (state.color_matrix == 2 ? loc::Get(loc::S::Opt_Matrix_709) : loc::Get(loc::S::Opt_Matrix_2020)))
        );
        D2D1_RECT_F ov_txt_rc = D2D1::RectF(ov_rc.left + 10.0f, ov_rc.top + 4.0f, ov_rc.right - 10.0f, ov_rc.bottom - 4.0f);
        m_renderer.DrawTextSimple(ov_txt, m_renderer.FontSmall(), ov_txt_rc,
                                  m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void MainWindowView::RenderDropdownOverlay(const UiState& state) noexcept {
    if (state.open_dropdown == 0) return;

    float cur_scroll = GetCurrentScrollY(state.active_tab);
    float visual_anchor_top = m_dropdown_anchor_rc.top - cur_scroll;
    float visual_anchor_bottom = m_dropdown_anchor_rc.bottom - cur_scroll;

    if (visual_anchor_bottom < m_viewport_top || visual_anchor_top > m_viewport_bottom) {
        return;
    }

    struct DropdownItem {
        std::wstring_view label;
        std::wstring_view desc;
    };

    std::vector<DropdownItem> items;
    int selected_idx = 0;

    switch (state.open_dropdown) {
    case Control_Set_StreamingMode:
        items = {
            { loc::Get(loc::S::Opt_Delivery_Balanced), loc::Get(loc::S::Opt_Delivery_Balanced_Desc) },
            { loc::Get(loc::S::Opt_Delivery_Fastest), loc::Get(loc::S::Opt_Delivery_Fastest_Desc) },
            { loc::Get(loc::S::Opt_Delivery_Smooth), loc::Get(loc::S::Opt_Delivery_Smooth_Desc) },
            { loc::Get(loc::S::Opt_Delivery_Custom), loc::Get(loc::S::Opt_Delivery_Custom_Desc) }
        };
        selected_idx = std::clamp(state.streaming_mode, 0, 3);
        break;
    case Control_Set_VideoFreshness: {
        items = {{L"5 ms", L""}, {L"10 ms", L""}, {L"16 ms", L""},
                 {L"25 ms", L""}, {L"40 ms", L""}, {L"60 ms", L""}, {L"100 ms", L""}};
        uint32_t nearest_distance = 1000;
        for (size_t i = 0; i < std::size(kCustomFreshnessChoicesMs); ++i) {
            const uint32_t choice = kCustomFreshnessChoicesMs[i];
            const uint32_t distance = choice > state.custom_video_freshness_ms
                ? choice - state.custom_video_freshness_ms : state.custom_video_freshness_ms - choice;
            if (distance < nearest_distance) {
                nearest_distance = distance;
                selected_idx = static_cast<int>(i);
            }
        }
        break;
    }
    case Control_Set_VideoQueueFrames:
        items = {{L"1", loc::Get(loc::S::Opt_Delivery_Fastest_Desc)}, {L"2", L""}, {L"3", L""}};
        selected_idx = static_cast<int>(std::clamp(state.custom_video_queue_frames, 1u, 3u)) - 1;
        break;
    case Control_Set_Profile:
        items = {
            { loc::Get(loc::S::Opt_Profile_Auto),        loc::Get(loc::S::Opt_Profile_Auto_Desc) },
            { loc::Get(loc::S::Opt_Profile_HighQuality), loc::Get(loc::S::Opt_Profile_HighQuality_Desc) },
            { loc::Get(loc::S::Opt_Profile_Balanced),    loc::Get(loc::S::Opt_Profile_Balanced_Desc) },
            { loc::Get(loc::S::Opt_Profile_LowSpec),     loc::Get(loc::S::Opt_Profile_LowSpec_Desc) },
            { loc::Get(loc::S::Opt_Profile_Custom),      loc::Get(loc::S::Opt_Profile_Custom_Desc) }
        };
        selected_idx = std::clamp(state.performance_profile, 0, 4);
        break;
    case Control_Set_Receiver:
        items = {
            { loc::Get(loc::S::Opt_Rec_Auto),        loc::Get(loc::S::Opt_Rec_Auto_Desc) },
            { loc::Get(loc::S::Opt_Rec_720p30),      loc::Get(loc::S::Opt_Rec_720p30_Desc) },
            { loc::Get(loc::S::Opt_Rec_720p60),      loc::Get(loc::S::Opt_Rec_720p60_Desc) },
            { loc::Get(loc::S::Opt_Rec_1080p30),     loc::Get(loc::S::Opt_Rec_1080p30_Desc) },
            { loc::Get(loc::S::Opt_Rec_1080p60),     loc::Get(loc::S::Opt_Rec_1080p60_Desc) },
            { loc::Get(loc::S::Opt_Rec_1440p60),     loc::Get(loc::S::Opt_Rec_1440p60_Desc) },
            { loc::Get(loc::S::Opt_Rec_Original60),  loc::Get(loc::S::Opt_Rec_Original60_Desc) }
        };
        selected_idx = std::clamp(state.receiver_quality, 0, 6);
        break;
    case Control_Set_Output:
        items = {
            { loc::Get(loc::S::Opt_Out_Auto),     loc::Get(loc::S::Opt_Out_Auto_Desc) },
            { loc::Get(loc::S::Opt_Out_HD),       loc::Get(loc::S::Opt_Out_HD_Desc) },
            { loc::Get(loc::S::Opt_Out_FullHD),   loc::Get(loc::S::Opt_Out_FullHD_Desc) },
            { loc::Get(loc::S::Opt_Out_2K),       loc::Get(loc::S::Opt_Out_2K_Desc) },
            { loc::Get(loc::S::Opt_Out_4K),       loc::Get(loc::S::Opt_Out_4K_Desc) },
            { loc::Get(loc::S::Opt_Out_Original), loc::Get(loc::S::Opt_Out_Original_Desc) },
            { loc::Get(loc::S::Opt_Out_Custom),   loc::Get(loc::S::Opt_Out_Custom_Desc) }
        };
        selected_idx = std::clamp(state.output_quality, 0, 6);
        break;
    case Control_Set_CaptureCanvas:
        items = {
            { loc::Get(loc::S::Opt_Canvas_FollowSource), loc::Get(loc::S::Opt_Canvas_FollowSource_Desc) },
            { loc::Get(loc::S::Opt_Canvas_16_9_HD),      loc::Get(loc::S::Opt_Canvas_16_9_HD_Desc) },
            { loc::Get(loc::S::Opt_Canvas_16_9_FullHD),  loc::Get(loc::S::Opt_Canvas_16_9_FullHD_Desc) },
            { loc::Get(loc::S::Opt_Canvas_16_9_2K),      loc::Get(loc::S::Opt_Canvas_16_9_2K_Desc) },
            { loc::Get(loc::S::Opt_Canvas_Custom),       loc::Get(loc::S::Opt_Canvas_Custom_Desc) }
        };
        selected_idx = std::clamp(state.capture_canvas, 0, 4);
        break;
    case Control_Set_AspectMode:
        if (state.capture_canvas == 0) {
            items = {
                { loc::Get(loc::S::Opt_Aspect_Preserve), loc::Get(loc::S::Opt_Aspect_Preserve_Desc) }
            };
            selected_idx = 0;
        } else {
            items = {
                { loc::Get(loc::S::Opt_Aspect_Fit),     loc::Get(loc::S::Opt_Aspect_Fit_Desc) },
                { loc::Get(loc::S::Opt_Aspect_Fill),    loc::Get(loc::S::Opt_Aspect_Fill_Desc) },
                { loc::Get(loc::S::Opt_Aspect_Stretch), loc::Get(loc::S::Opt_Aspect_Stretch_Desc) }
            };
            selected_idx = std::clamp(state.aspect_mode - 1, 0, 2);
        }
        break;
    case Control_Set_Scaling:
        items = {
            { loc::Get(loc::S::Opt_Scale_Auto),        loc::Get(loc::S::Opt_Scale_Auto_Desc) },
            { loc::Get(loc::S::Opt_Scale_Fast),        loc::Get(loc::S::Opt_Scale_Fast_Desc) },
            { loc::Get(loc::S::Opt_Scale_Sharp),       loc::Get(loc::S::Opt_Scale_Sharp_Desc) },
            { loc::Get(loc::S::Opt_Scale_HighQuality), loc::Get(loc::S::Opt_Scale_HighQuality_Desc) }
        };
        selected_idx = std::clamp(state.scaling_quality, 0, 3);
        break;
    case Control_Set_PixelPerfect:
        items = {
            { loc::Get(loc::S::Opt_Pixel_Auto), loc::Get(loc::S::Opt_Pixel_Auto_Desc) },
            { loc::Get(loc::S::Opt_Pixel_On),   loc::Get(loc::S::Opt_Pixel_On_Desc) },
            { loc::Get(loc::S::Opt_Pixel_Off),  loc::Get(loc::S::Opt_Pixel_Off_Desc) }
        };
        selected_idx = std::clamp(state.pixel_perfect_mode, 0, 2);
        break;
    case Control_Set_Renderer:
        items = {
            { loc::Get(loc::S::Opt_Rnd_Auto),    loc::Get(loc::S::Opt_Rnd_Auto_Desc) },
            { loc::Get(loc::S::Opt_Rnd_D3D11),   loc::Get(loc::S::Opt_Rnd_D3D11_Desc) },
            { loc::Get(loc::S::Opt_Rnd_Compat),  loc::Get(loc::S::Opt_Rnd_Compat_Desc) },
            { loc::Get(loc::S::Opt_Rnd_Warp),    loc::Get(loc::S::Opt_Rnd_Warp_Desc) }
        };
        selected_idx = std::clamp(state.renderer_mode, 0, 3);
        break;
    case Control_Set_ColorPreset:
        items = {
            { loc::Get(loc::S::Opt_Color_Neutral), loc::Get(loc::S::Opt_Color_Neutral_Desc) },
            { loc::Get(loc::S::Opt_Color_Vivid),   loc::Get(loc::S::Opt_Color_Vivid_Desc) },
            { loc::Get(loc::S::Opt_Color_Soft),    loc::Get(loc::S::Opt_Color_Soft_Desc) },
            { loc::Get(loc::S::Opt_Color_Custom),  loc::Get(loc::S::Opt_Color_Custom_Desc) }
        };
        selected_idx = std::clamp(state.color_preset, 0, 3);
        break;
    case Control_Set_ColorRange:
        items = {
            { loc::Get(loc::S::Opt_Range_Auto),    loc::Get(loc::S::Opt_Range_Auto_Desc) },
            { loc::Get(loc::S::Opt_Range_Limited), loc::Get(loc::S::Opt_Range_Limited_Desc) },
            { loc::Get(loc::S::Opt_Range_Full),    loc::Get(loc::S::Opt_Range_Full_Desc) }
        };
        selected_idx = std::clamp(state.color_range, 0, 2);
        break;
    case Control_Set_ColorMatrix:
        items = {
            { loc::Get(loc::S::Opt_Matrix_Auto), loc::Get(loc::S::Opt_Matrix_Auto_Desc) },
            { loc::Get(loc::S::Opt_Matrix_601),  loc::Get(loc::S::Opt_Matrix_601_Desc) },
            { loc::Get(loc::S::Opt_Matrix_709),  loc::Get(loc::S::Opt_Matrix_709_Desc) },
            { loc::Get(loc::S::Opt_Matrix_2020), loc::Get(loc::S::Opt_Matrix_2020_Desc) }
        };
        selected_idx = std::clamp(state.color_matrix, 0, 3);
        break;
    case Control_Set_Language:
        items = {
            { loc::Get(loc::S::Opt_Lang_System), loc::Get(loc::S::Opt_Lang_System_Desc) },
            { loc::Get(loc::S::Opt_Lang_En),     loc::Get(loc::S::Opt_Lang_En_Desc) },
            { loc::Get(loc::S::Opt_Lang_Vi),     loc::Get(loc::S::Opt_Lang_Vi_Desc) }
        };
        selected_idx = (state.language == L"en-US") ? 1 : ((state.language == L"vi-VN") ? 2 : 0);
        break;
    case Control_Set_AudioDevice:
        items.push_back({ loc::Get(loc::S::Opt_Audio_SystemDefault), loc::Get(loc::S::Opt_Audio_SystemDefault_Desc) });
        selected_idx = 0;
        for (size_t i = 0; i < state.available_audio_devices.size(); ++i) {
            const auto& dev = state.available_audio_devices[i];
            items.push_back({ dev.name, loc::Get(loc::S::Opt_Audio_DirectEndpoint_Desc) });
            if (dev.id == state.audio_device_id) {
                selected_idx = static_cast<int>(i + 1);
            }
        }
        break;
    case Control_Set_PreferredMonitor:
        items = {
            { loc::Get(loc::S::Opt_Mon_Primary),   loc::Get(loc::S::Opt_Mon_Primary_Desc) },
            { loc::Get(loc::S::Opt_Mon_Secondary), loc::Get(loc::S::Opt_Mon_Secondary_Desc) },
            { loc::Get(loc::S::Opt_Mon_Current),   loc::Get(loc::S::Opt_Mon_Current_Desc) }
        };
        selected_idx = std::clamp(state.preferred_monitor, 0, 2);
        break;
    default:
        return;
    }

    float total_w = m_renderer.WidthDip();
    float total_h = m_renderer.HeightDip();

    // 1. Full-screen backdrop to dismiss on outside click
    D2D1_RECT_F backdrop = D2D1::RectF(0.0f, 0.0f, total_w, total_h);
    RegisterClickable(backdrop, Control_None);

    // 2. Position popup relative to m_dropdown_anchor_rc
    float anchor_w = m_dropdown_anchor_rc.right - m_dropdown_anchor_rc.left;
    float popup_w = std::clamp(std::max(anchor_w, 280.0f), 280.0f, 380.0f);
    float item_h = 42.0f;
    float popup_h = items.size() * item_h + 12.0f;

    float popup_x = m_dropdown_anchor_rc.left;
    if (popup_x + popup_w > total_w - 16.0f) {
        popup_x = total_w - 16.0f - popup_w;
    }
    if (popup_x < 16.0f) popup_x = 16.0f;

    float popup_y = visual_anchor_bottom + 4.0f;
    if (popup_y + popup_h > total_h - metrics::StatusBarHeight - 8.0f) {
        popup_y = visual_anchor_top - 4.0f - popup_h;
    }

    D2D1_RECT_F popup_rc = D2D1::RectF(popup_x, popup_y, popup_x + popup_w, popup_y + popup_h);

    // Draw elevated popup card
    m_renderer.FillRoundedRect(popup_rc, 8.0f, m_renderer.BrushCardBorder());
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pop_bg;
    m_renderer.Target()->CreateSolidColorBrush(colors::CardSurfaceHover, pop_bg.GetAddressOf());
    if (pop_bg) m_renderer.FillRoundedRect(popup_rc, 8.0f, pop_bg.Get());
    m_renderer.DrawRoundedRect(popup_rc, 8.0f, m_renderer.BrushBrandBlue(), 1.4f);

    // 3. Render items
    float cur_y = popup_y + 6.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        D2D1_RECT_F item_rc = D2D1::RectF(popup_x + 6.0f, cur_y, popup_x + popup_w - 6.0f, cur_y + item_h);
        int item_ctrl_id = Control_Dropdown_Item_Base + static_cast<int>(i);
        RegisterClickable(item_rc, item_ctrl_id);

        bool is_hovered = (state.hovered_control == item_ctrl_id);
        bool is_selected = (static_cast<int>(i) == selected_idx);

        if (is_selected) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sel_bg;
            m_renderer.Target()->CreateSolidColorBrush(colors::NavActiveBg, sel_bg.GetAddressOf());
            if (sel_bg) m_renderer.FillRoundedRect(item_rc, 6.0f, sel_bg.Get());
            m_renderer.DrawRoundedRect(item_rc, 6.0f, m_renderer.BrushBrandBlue(), 1.0f);
        } else if (is_hovered) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hov_bg;
            m_renderer.Target()->CreateSolidColorBrush(colors::NavHoverBg, hov_bg.GetAddressOf());
            if (hov_bg) m_renderer.FillRoundedRect(item_rc, 6.0f, hov_bg.Get());
        }

        if (is_selected) {
            D2D1_POINT_2F dot_pt = D2D1::Point2F(item_rc.left + 14.0f, (item_rc.top + item_rc.bottom) * 0.5f);
            m_renderer.DrawStatusDot(dot_pt, 3.5f, colors::BrandBlue, true);
        }

        float text_left = item_rc.left + (is_selected ? 26.0f : 14.0f);
        if (!items[i].desc.empty()) {
            D2D1_RECT_F label_rc = D2D1::RectF(text_left, item_rc.top + 3.0f, item_rc.right - 8.0f, item_rc.top + 21.0f);
            m_renderer.DrawTextSimple(
                items[i].label, m_renderer.FontSmallBold(), label_rc,
                is_selected ? m_renderer.BrushBrandBlue() : (is_hovered ? m_renderer.BrushTextPrimary() : m_renderer.BrushTextPrimary()),
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            );

            D2D1_RECT_F desc_rc = D2D1::RectF(text_left, item_rc.top + 21.0f, item_rc.right - 8.0f, item_rc.bottom - 2.0f);
            m_renderer.DrawTextSimple(
                items[i].desc, m_renderer.FontSmall(), desc_rc,
                m_renderer.BrushTextMuted(),
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            );
        } else {
            D2D1_RECT_F label_rc = D2D1::RectF(text_left, item_rc.top, item_rc.right - 8.0f, item_rc.bottom);
            m_renderer.DrawTextSimple(
                items[i].label, m_renderer.FontSmallBold(), label_rc,
                is_selected ? m_renderer.BrushBrandBlue() : (is_hovered ? m_renderer.BrushTextPrimary() : m_renderer.BrushTextPrimary()),
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            );
        }

        cur_y += item_h;
    }
}

void MainWindowView::RenderCrashBanner(const UiState& state, const D2D1_RECT_F& area) noexcept {
    if (!state.show_crash_banner) return;

    m_renderer.DrawCard(area, false, 6.0f);
    m_renderer.DrawRoundedRect(area, 6.0f, m_renderer.BrushStatusAmber(), 1.0f);

    D2D1_RECT_F icon_rc = D2D1::RectF(area.left + 12.0f, area.top, area.left + 32.0f, area.bottom);
    m_renderer.DrawTextSimple(L"⚠", m_renderer.FontSubheader(), icon_rc, m_renderer.BrushStatusAmber(),
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F text_rc = D2D1::RectF(area.left + 36.0f, area.top, area.right - 210.0f, area.bottom);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Crash_Banner_Text), m_renderer.FontBody(), text_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float btn_w = 90.0f;
    float btn_h = 24.0f;
    float btn_y = area.top + (area.bottom - area.top - btn_h) * 0.5f;

    float b1_x = area.right - 194.0f;
    D2D1_RECT_F b1_rc = D2D1::RectF(b1_x, btn_y, b1_x + btn_w, btn_y + btn_h);
    bool b1_hov = (state.hovered_control == Control_Btn_CrashOpenLogs);
    m_renderer.DrawButton(b1_rc, loc::Get(loc::S::Crash_Banner_OpenLogs), false, b1_hov, false);
    RegisterClickable(b1_rc, Control_Btn_CrashOpenLogs, loc::Get(loc::S::Crash_Banner_OpenLogs), true);

    float b2_x = area.right - 98.0f;
    D2D1_RECT_F b2_rc = D2D1::RectF(b2_x, btn_y, b2_x + btn_w, btn_y + btn_h);
    bool b2_hov = (state.hovered_control == Control_Btn_CrashDismiss);
    m_renderer.DrawButton(b2_rc, loc::Get(loc::S::Crash_Banner_Dismiss), false, b2_hov, false);
    RegisterClickable(b2_rc, Control_Btn_CrashDismiss, loc::Get(loc::S::Crash_Banner_Dismiss), true);
}

void MainWindowView::RenderFirstRunWelcome(const UiState& state, const D2D1_RECT_F& area) noexcept {
    float pad = 24.0f;
    float w = area.right - area.left - 2 * pad;
    float card_h = 360.0f;
    D2D1_RECT_F card_rc = D2D1::RectF(area.left + pad, area.top + pad, area.left + pad + w, area.top + pad + card_h);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    float cx = (card_rc.left + card_rc.right) * 0.5f;
    float y = card_rc.top + 24.0f;

    D2D1_RECT_F t_rc = D2D1::RectF(card_rc.left + 20.0f, y, card_rc.right - 20.0f, y + 28.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_Title), m_renderer.FontTitle(), t_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 34.0f;

    D2D1_RECT_F st_rc = D2D1::RectF(card_rc.left + 20.0f, y, card_rc.right - 20.0f, y + 20.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_Desc), m_renderer.FontBody(), st_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 36.0f;

    float col_w = (w - 60.0f) * 0.5f;
    float col_h = 150.0f;

    // Col 1: Wireless AirPlay
    D2D1_RECT_F c1_rc = D2D1::RectF(card_rc.left + 24.0f, y, card_rc.left + 24.0f + col_w, y + col_h);
    m_renderer.DrawInset(c1_rc, 8.0f);
    D2D1_RECT_F c1_t_rc = D2D1::RectF(c1_rc.left + 16.0f, c1_rc.top + 16.0f, c1_rc.right - 16.0f, c1_rc.top + 36.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_AirPlay_Title), m_renderer.FontSubheader(), c1_t_rc,
        m_renderer.BrushBrandBlue());
    D2D1_RECT_F c1_d_rc = D2D1::RectF(c1_rc.left + 16.0f, c1_rc.top + 42.0f, c1_rc.right - 16.0f, c1_rc.bottom - 16.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_AirPlay_Desc), m_renderer.FontSmall(), c1_d_rc,
        m_renderer.BrushTextSecondary());

    // Col 2: Wired USB
    D2D1_RECT_F c2_rc = D2D1::RectF(card_rc.left + 24.0f + col_w + 12.0f, y, card_rc.right - 24.0f, y + col_h);
    m_renderer.DrawInset(c2_rc, 8.0f);
    D2D1_RECT_F c2_t_rc = D2D1::RectF(c2_rc.left + 16.0f, c2_rc.top + 16.0f, c2_rc.right - 16.0f, c2_rc.top + 36.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_Wired_Title), m_renderer.FontSubheader(), c2_t_rc,
        m_renderer.BrushBrandBlue());
    D2D1_RECT_F c2_d_rc = D2D1::RectF(c2_rc.left + 16.0f, c2_rc.top + 42.0f, c2_rc.right - 16.0f, c2_rc.bottom - 16.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::FirstRun_Wired_Desc), m_renderer.FontSmall(), c2_d_rc,
        m_renderer.BrushTextSecondary());
    y += col_h + 24.0f;

    // "Get Started" Button
    float btn_w = 160.0f;
    float btn_h = 36.0f;
    D2D1_RECT_F btn_rc = D2D1::RectF(cx - btn_w * 0.5f, y, cx + btn_w * 0.5f, y + btn_h);
    bool hov = (state.hovered_control == Control_Btn_FirstRunContinue);
    m_renderer.DrawButton(btn_rc, loc::Get(loc::S::FirstRun_Btn_Continue), true, hov, false);
    RegisterClickable(btn_rc, Control_Btn_FirstRunContinue, L"", true);
}

void MainWindowView::RenderAboutView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    float pad = 24.0f;
    float content_right = area.right - pad - 12.0f;
    float card_h = 490.0f;
    D2D1_RECT_F card_rc = D2D1::RectF(area.left + pad, area.top + pad, content_right, area.top + pad + card_h);
    m_renderer.DrawCard(card_rc, false, metrics::CardRadius);

    float cx = (card_rc.left + card_rc.right) * 0.5f;
    float top_y = card_rc.top + 28.0f;

    // Brand Logo (80x80)
    D2D1_RECT_F logo_rc = D2D1::RectF(cx - 40.0f, top_y, cx + 40.0f, top_y + 80.0f);
    m_renderer.DrawLogo(logo_rc);

    // Product Title: Duwn Mirror
    D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 90.0f, card_rc.right - 20.0f, top_y + 116.0f);
    m_renderer.DrawTextSimple(
        DUWN_PRODUCT_NAME_W, m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Version Badge: Version 0.2.0 (Windows x64)
    std::wstring ver_str = std::format(L"Version {} (Windows {})", DUWN_VERSION_STRING_W, DUWN_BUILD_PLATFORM_W);
    D2D1_RECT_F ver_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 118.0f, card_rc.right - 20.0f, top_y + 138.0f);
    m_renderer.DrawTextSimple(
        ver_str, m_renderer.FontSmallBold(), ver_rc,
        m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Build ID (Git commit & timestamp)
    std::string commit_s = DUWN_GIT_COMMIT_SHORT;
    std::wstring commit_w(commit_s.begin(), commit_s.end());
    std::wstring build_id = std::format(L"Build {} • {}", commit_w, DUWN_BUILD_TIMESTAMP_W);
    D2D1_RECT_F build_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 138.0f, card_rc.right - 20.0f, top_y + 154.0f);
    m_renderer.DrawTextSimple(
        build_id, m_renderer.FontSmall(), build_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Description
    D2D1_RECT_F desc_rc = D2D1::RectF(card_rc.left + 20.0f, top_y + 158.0f, card_rc.right - 20.0f, top_y + 176.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::About_Desc), m_renderer.FontBody(), desc_rc,
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Open Source summary box
    float box_w = std::min(620.0f, card_rc.right - card_rc.left - 48.0f);
    float box_x = cx - box_w * 0.5f;
    float box_y = top_y + 188.0f;
    float box_h = 70.0f;
    D2D1_RECT_F box_rc = D2D1::RectF(box_x, box_y, box_x + box_w, box_y + box_h);
    m_renderer.DrawInset(box_rc, 8.0f);

    D2D1_RECT_F os_text_rc = D2D1::RectF(box_x + 14.0f, box_y + 10.0f, box_x + box_w - 14.0f, box_y + box_h - 10.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::About_OpenSourceSummary), m_renderer.FontSmall(), os_text_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    // Action Buttons: Copy Diagnostics, Open Logs, Third-Party Notices, GitHub
    float btn_w = 140.0f;
    float btn_gap = 12.0f;
    float total_btns_w = 4 * btn_w + 3 * btn_gap;
    float btn_start_x = cx - total_btns_w * 0.5f;
    float btn_y = box_y + box_h + 20.0f;
    float btn_h = 32.0f;

    struct ActionBtn {
        int id;
        loc::S label;
    };
    ActionBtn btns[] = {
        { Control_Btn_CopyDiagnostics, loc::S::About_Btn_CopyDiag },
        { Control_Btn_OpenLogs,        loc::S::About_Btn_OpenLogs },
        { Control_Btn_OpenNotices,     loc::S::About_Btn_Notices },
        { Control_Btn_OpenGitHub,      loc::S::About_Btn_GitHub },
    };

    for (int i = 0; i < 4; ++i) {
        float bx = btn_start_x + i * (btn_w + btn_gap);
        D2D1_RECT_F b_rc = D2D1::RectF(bx, btn_y, bx + btn_w, btn_y + btn_h);
        bool hov = (state.hovered_control == btns[i].id);
        m_renderer.DrawButton(b_rc, loc::Get(btns[i].label), false, hov, false);
        RegisterClickable(b_rc, btns[i].id, L"", true);
    }

    // Copyright footer
    D2D1_RECT_F foot_rc = D2D1::RectF(card_rc.left + 20.0f, btn_y + btn_h + 24.0f, card_rc.right - 20.0f, btn_y + btn_h + 44.0f);
    m_renderer.DrawTextSimple(
        loc::Get(loc::S::About_Copyright),
        m_renderer.FontSmall(), foot_rc,
        m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );
}

// ---------------------------------------------------------------------------
// Settings sub-pages
// ---------------------------------------------------------------------------

void MainWindowView::RenderGeneralPage(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    // Title
    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_General), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    // Section: Language
    {
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_left + 180.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::General_Language), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const wchar_t* lang_label =
            (state.language == L"en-US") ? loc::Get(loc::S::General_Language_En) :
            (state.language == L"vi-VN") ? loc::Get(loc::S::General_Language_Vi) : loc::Get(loc::S::General_Language_System);

        D2D1_RECT_F dd_rc = D2D1::RectF(content_left + 190.0f, y, content_left + 340.0f, y + 24.0f);
        const bool hov = (state.hovered_control == Control_Set_Language);
        m_renderer.DrawInset(dd_rc, 4.0f);
        if (state.open_dropdown == Control_Set_Language) {
            m_dropdown_anchor_rc = dd_rc;
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushBrandBlue(), 1.2f);
        } else if (hov) {
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushCardBorder(), 1.0f);
        }
        D2D1_RECT_F dd_txt = D2D1::RectF(dd_rc.left + 8.0f, dd_rc.top, dd_rc.right - 20.0f, dd_rc.bottom);
        m_renderer.DrawTextSimple(lang_label, m_renderer.FontBody(), dd_txt,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_renderer.DrawTextSimple(L"▾", m_renderer.FontBody(),
            D2D1::RectF(dd_rc.right - 18.0f, dd_rc.top, dd_rc.right - 2.0f, dd_rc.bottom),
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        RegisterClickable(dd_rc, Control_Set_Language, loc::Get(loc::S::General_Language), true);
        y += 32.0f;
    }

    // Section: Startup & Window behavior
    {
        auto DrawToggle = [&](const wchar_t* label, bool on, int ctrl_id, float row_y) {
            D2D1_RECT_F row = D2D1::RectF(content_left, row_y, content_right, row_y + 24.0f);
            m_renderer.DrawTextSimple(label, m_renderer.FontBody(), row,
                m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            // Toggle pill
            D2D1_RECT_F pill = D2D1::RectF(content_right - 42.0f, row_y + 4.0f, content_right - 2.0f, row_y + 20.0f);
            m_renderer.FillRoundedRect(pill, 8.0f, on ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
            // Thumb
            float thumb_x = on ? (pill.right - 14.0f) : (pill.left + 2.0f);
            D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
            m_renderer.FillRoundedRect(thumb, 5.0f, m_renderer.BrushTextPrimary());
            RegisterClickable(row, ctrl_id, L"", true);
        };
        DrawToggle(loc::Get(loc::S::General_RememberMode), state.remember_selected_mode,
            Control_Toggle_RememberMode, y);
        y += 32.0f;
        D2D1_RECT_F default_row = D2D1::RectF(content_left, y, content_right, y + 26.0f);
        RegisterClickable(default_row, Control_Set_DefaultMode, loc::Get(loc::S::General_DefaultMode), true);
        m_renderer.DrawTextSimple(loc::Get(loc::S::General_DefaultMode), m_renderer.FontBody(),
            D2D1::RectF(content_left, y, content_right - 160.0f, y + 26.0f), m_renderer.BrushTextPrimary());
        m_renderer.DrawTextSimple(loc::Get(state.default_connection_mode == 0
            ? loc::S::Mode_Wireless : loc::S::Mode_Wired), m_renderer.FontBodyBold(),
            D2D1::RectF(content_right - 185.0f, y, content_right, y + 26.0f),
            m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += 36.0f;
        DrawToggle(loc::Get(loc::S::General_StartWithWindows), state.start_on_boot, Control_Toggle_StartOnBoot, y);
        y += 32.0f;
        DrawToggle(loc::Get(loc::S::General_StartMinimized), state.start_minimized, Control_Toggle_StartMinimized, y);
        y += 32.0f;
        DrawToggle(loc::Get(loc::S::General_MinimizeToTray), state.minimize_to_tray, Control_Toggle_MinimizeToTray, y);
        y += 32.0f;
        DrawToggle(loc::Get(loc::S::General_RememberWindowPos), state.remember_window_pos, Control_Toggle_RememberWindowPos, y);
        y += 32.0f;
    }

    m_content_height = y - area.top;
}

void MainWindowView::RenderNetworkPage(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    // Title
    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_Network), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    auto DrawToggle = [&](const wchar_t* label, bool on, int ctrl_id, float row_y) {
        D2D1_RECT_F row = D2D1::RectF(content_left, row_y, content_right, row_y + 24.0f);
        m_renderer.DrawTextSimple(label, m_renderer.FontBody(), row,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // Toggle pill
        D2D1_RECT_F pill = D2D1::RectF(content_right - 42.0f, row_y + 4.0f, content_right - 2.0f, row_y + 20.0f);
        m_renderer.FillRoundedRect(pill, 8.0f, on ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        // Thumb
        float thumb_x = on ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 5.0f, m_renderer.BrushTextPrimary());
        RegisterClickable(row, ctrl_id, L"", true);
    };

    DrawToggle(loc::Get(loc::S::General_AllowPublicNetworks), state.allow_public_networks, Control_Toggle_AllowPublicNetworks, y);
    y += 36.0f;

    // Network Status Card
    float stat_h = 74.0f;
    D2D1_RECT_F stat_card = D2D1::RectF(content_left, y, content_right, y + stat_h);
    m_renderer.DrawCard(stat_card, false, 8.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Network_PublicSecurityTitle), m_renderer.FontSmallBold(),
        D2D1::RectF(stat_card.left + 14.0f, stat_card.top + 8.0f, stat_card.right - 14.0f, stat_card.top + 24.0f),
        m_renderer.BrushBrandBlue());

    std::wstring net_info = std::format(
        L"Type: {}   •   AirPlay Ports: 5000, 7000, 7100 (TCP/UDP)\nStatus: {}",
        state.is_public_network ? L"Public Network" : L"Private / Domain Network",
        state.is_public_network && !state.allow_public_networks ? L"Blocked on Public Network" : L"Listening & Discoverable"
    );
    m_renderer.DrawTextSimple(net_info, m_renderer.FontSmall(),
        D2D1::RectF(stat_card.left + 14.0f, stat_card.top + 28.0f, stat_card.right - 14.0f, stat_card.bottom - 8.0f),
        m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += stat_h + 12.0f;

    // Warnings and remediation cards
    if (state.public_rules_missing) {
        float warn_h = 76.0f;
        D2D1_RECT_F warn_card = D2D1::RectF(content_left, y, content_right, y + warn_h);
        m_renderer.DrawCard(warn_card, false, 8.0f);
        m_renderer.DrawRoundedRect(warn_card, 8.0f, m_renderer.BrushStatusAmber(), 1.0f);

        D2D1_RECT_F warn_txt_rc = D2D1::RectF(warn_card.left + 14.0f, warn_card.top + 6.0f, warn_card.right - 14.0f, warn_card.top + 40.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Network_PublicRulesMissingWarning), m_renderer.FontSmall(), warn_txt_rc,
            m_renderer.BrushStatusAmber(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        float btn_y = warn_card.top + 44.0f;
        float btn_h = 24.0f;
        D2D1_RECT_F btn_rc = D2D1::RectF(warn_card.left + 14.0f, btn_y, warn_card.left + 200.0f, btn_y + btn_h);
        m_renderer.DrawButton(btn_rc, loc::Get(loc::S::Network_RepairPublicRules), true,
            state.hovered_control == Control_Btn_RepairPublicRules, state.pressed_control == Control_Btn_RepairPublicRules);
        RegisterClickable(btn_rc, Control_Btn_RepairPublicRules, loc::Get(loc::S::Network_RepairPublicRules), true);
        y += warn_h + 8.0f;
    } else if (state.public_rules_unexpected) {
        float warn_h = 76.0f;
        D2D1_RECT_F warn_card = D2D1::RectF(content_left, y, content_right, y + warn_h);
        m_renderer.DrawCard(warn_card, false, 8.0f);
        m_renderer.DrawRoundedRect(warn_card, 8.0f, m_renderer.BrushStatusAmber(), 1.0f);

        D2D1_RECT_F warn_txt_rc = D2D1::RectF(warn_card.left + 14.0f, warn_card.top + 6.0f, warn_card.right - 14.0f, warn_card.top + 40.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Network_PublicMismatchWarning), m_renderer.FontSmall(), warn_txt_rc,
            m_renderer.BrushStatusAmber(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        float btn_y = warn_card.top + 44.0f;
        float btn_h = 24.0f;
        D2D1_RECT_F btn_rc = D2D1::RectF(warn_card.left + 14.0f, btn_y, warn_card.left + 200.0f, btn_y + btn_h);
        m_renderer.DrawButton(btn_rc, loc::Get(loc::S::Network_DisablePublicRules), false,
            state.hovered_control == Control_Btn_DisablePublicRules, state.pressed_control == Control_Btn_DisablePublicRules);
        RegisterClickable(btn_rc, Control_Btn_DisablePublicRules, loc::Get(loc::S::Network_DisablePublicRules), true);
        y += warn_h + 8.0f;
    }

    if (state.is_public_network && !state.allow_public_networks) {
        float warn_h = 92.0f;
        D2D1_RECT_F warn_card = D2D1::RectF(content_left, y, content_right, y + warn_h);
        m_renderer.DrawCard(warn_card, false, 8.0f);
        m_renderer.DrawRoundedRect(warn_card, 8.0f, m_renderer.BrushStatusAmber(), 1.0f);

        D2D1_RECT_F warn_txt_rc = D2D1::RectF(warn_card.left + 14.0f, warn_card.top + 8.0f, warn_card.right - 14.0f, warn_card.top + 48.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Network_PublicWarning), m_renderer.FontSmallBold(), warn_txt_rc,
            m_renderer.BrushStatusAmber(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        float btn_y = warn_card.top + 52.0f;
        float btn_h = 28.0f;
        D2D1_RECT_F btn1_rc = D2D1::RectF(warn_card.left + 14.0f, btn_y, warn_card.left + 220.0f, btn_y + btn_h);
        m_renderer.DrawButton(btn1_rc, loc::Get(loc::S::Network_OpenNetworkSettings), false,
            state.hovered_control == Control_Btn_OpenNetworkSettings, state.pressed_control == Control_Btn_OpenNetworkSettings);
        RegisterClickable(btn1_rc, Control_Btn_OpenNetworkSettings, loc::Get(loc::S::Network_OpenNetworkSettings), true);

        D2D1_RECT_F btn2_rc = D2D1::RectF(warn_card.left + 230.0f, btn_y, warn_card.left + 440.0f, btn_y + btn_h);
        m_renderer.DrawButton(btn2_rc, loc::Get(loc::S::Network_AllowOnThisNetwork), true,
            state.hovered_control == Control_Btn_AllowOnThisNetwork, state.pressed_control == Control_Btn_AllowOnThisNetwork);
        RegisterClickable(btn2_rc, Control_Btn_AllowOnThisNetwork, loc::Get(loc::S::Network_AllowOnThisNetwork), true);

        y += warn_h + 12.0f;
    }

    m_content_height = y - area.top;
}

void MainWindowView::RenderOutputPage(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    // Title
    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_Output), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    auto DrawToggle = [&](const wchar_t* label, bool on, int ctrl_id, float row_y) {
        D2D1_RECT_F row = D2D1::RectF(content_left, row_y, content_right, row_y + 24.0f);
        m_renderer.DrawTextSimple(label, m_renderer.FontBody(), row,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // Toggle pill
        D2D1_RECT_F pill = D2D1::RectF(content_right - 42.0f, row_y + 4.0f, content_right - 2.0f, row_y + 20.0f);
        m_renderer.FillRoundedRect(pill, 8.0f, on ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        // Thumb
        float thumb_x = on ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 5.0f, m_renderer.BrushTextPrimary());
        RegisterClickable(row, ctrl_id, L"", true);
    };

    DrawToggle(loc::Get(loc::S::Output_AutoOpenOutputWindow), state.auto_open_output_window, Control_Toggle_AutoOpenOutput, y);
    y += 32.0f;
    DrawToggle(loc::Get(loc::S::General_AlwaysOnTop), state.always_on_top, Control_Btn_AlwaysOnTop, y);
    y += 32.0f;
    DrawToggle(loc::Get(loc::S::Output_StartFullscreen), state.output_start_fullscreen, Control_Toggle_StartFullscreen, y);
    y += 32.0f;

    // Preferred Monitor Dropdown
    {
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_left + 180.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Output_PreferredMonitor), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const wchar_t* monitor_label =
            (state.preferred_monitor == 1) ? loc::Get(loc::S::Opt_Mon_Secondary) :
            (state.preferred_monitor == 2) ? loc::Get(loc::S::Opt_Mon_Current) : loc::Get(loc::S::Opt_Mon_Primary);

        D2D1_RECT_F dd_rc = D2D1::RectF(content_left + 190.0f, y, content_left + 360.0f, y + 24.0f);
        const bool hov = (state.hovered_control == Control_Set_PreferredMonitor);
        m_renderer.DrawInset(dd_rc, 4.0f);
        if (state.open_dropdown == Control_Set_PreferredMonitor) {
            m_dropdown_anchor_rc = dd_rc;
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushBrandBlue(), 1.2f);
        } else if (hov) {
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushCardBorder(), 1.0f);
        }
        D2D1_RECT_F dd_txt = D2D1::RectF(dd_rc.left + 8.0f, dd_rc.top, dd_rc.right - 20.0f, dd_rc.bottom);
        m_renderer.DrawTextSimple(monitor_label, m_renderer.FontBody(), dd_txt,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_renderer.DrawTextSimple(L"▾", m_renderer.FontBody(),
            D2D1::RectF(dd_rc.right - 18.0f, dd_rc.top, dd_rc.right - 2.0f, dd_rc.bottom),
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        RegisterClickable(dd_rc, Control_Set_PreferredMonitor, loc::Get(loc::S::Output_PreferredMonitor), true);
        y += 32.0f;
    }

    DrawToggle(loc::Get(loc::S::Output_HideCursor), state.hide_cursor, Control_Toggle_HideCursor, y);
    y += 32.0f;
    DrawToggle(loc::Get(loc::S::Output_RememberOutputPos), state.remember_output_pos, Control_Toggle_RememberOutputPos, y);
    y += 32.0f;

    m_content_height = y - area.top;
}

void MainWindowView::RenderAudioPage(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    // Title
    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_Audio), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;
    m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_PlaybackSection), m_renderer.FontSmallBold(),
        D2D1::RectF(content_left, y, content_right, y + 22.0f), m_renderer.BrushBrandBlue());
    y += 25.0f;

    // Output device dropdown
    {
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_left + 140.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_OutputDevice), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const std::wstring& dev_label = state.audio_device_name.empty() ? loc::Get(loc::S::Audio_SystemDefault) : state.audio_device_name;
        D2D1_RECT_F dd_rc = D2D1::RectF(content_left + 150.0f, y, content_right - 4.0f, y + 24.0f);
        const bool hov = (state.hovered_control == Control_Set_AudioDevice);
        m_renderer.DrawInset(dd_rc, 4.0f);
        if (state.open_dropdown == Control_Set_AudioDevice) {
            m_dropdown_anchor_rc = dd_rc;
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushBrandBlue(), 1.2f);
        } else if (hov) {
            m_renderer.DrawRoundedRect(dd_rc, 4.0f, m_renderer.BrushCardBorder(), 1.0f);
        }
        D2D1_RECT_F dd_txt = D2D1::RectF(dd_rc.left + 8.0f, dd_rc.top, dd_rc.right - 20.0f, dd_rc.bottom);
        m_renderer.DrawTextSimple(dev_label.c_str(), m_renderer.FontBody(), dd_txt,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_renderer.DrawTextSimple(L"▾", m_renderer.FontBody(),
            D2D1::RectF(dd_rc.right - 18.0f, dd_rc.top, dd_rc.right - 2.0f, dd_rc.bottom),
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        RegisterClickable(dd_rc, Control_Set_AudioDevice, loc::Get(loc::S::Audio_OutputDevice), true);
        y += 32.0f;

        if (state.audio_fallback_active) {
            std::wstring fallback_note = std::format(L"Đã chọn: {}  •  Đang phát tạm: {}",
                state.audio_device_name, state.resolved_audio_device_name);
            D2D1_RECT_F note_rc = D2D1::RectF(content_left + 150.0f, y, content_right, y + 20.0f);
            m_renderer.DrawTextSimple(fallback_note, m_renderer.FontSmall(), note_rc,
                m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            y += 24.0f;
        }
    }

    // Volume Slider (shared state with quick volume bar)
    {
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_left + 140.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_Volume), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        float track_x0 = content_left + 150.0f;
        float track_x1 = content_right - 60.0f;
        D2D1_RECT_F slider_rc = D2D1::RectF(track_x0, y, track_x1, y + 24.0f);
        RegisterClickable(slider_rc, Control_Slider_AudioVolume, loc::Get(loc::S::Audio_Volume), true);

        float track_cy = y + 12.0f;
        D2D1_RECT_F track_bg = D2D1::RectF(track_x0, track_cy - 3.0f, track_x1, track_cy + 3.0f);
        m_renderer.FillRoundedRect(track_bg, 3.0f, m_renderer.BrushCardBorder());

        float vol_frac = std::clamp(state.audio_volume, 0.0f, 1.0f);
        float fill_x1 = track_x0 + (track_x1 - track_x0) * vol_frac;
        if (fill_x1 > track_x0) {
            D2D1_RECT_F track_fill = D2D1::RectF(track_x0, track_cy - 3.0f, fill_x1, track_cy + 3.0f);
            m_renderer.FillRoundedRect(track_fill, 3.0f, m_renderer.BrushBrandBlue());
        }

        D2D1_ELLIPSE thumb = D2D1::Ellipse(D2D1::Point2F(fill_x1, track_cy), 6.5f, 6.5f);
        m_renderer.Target()->FillEllipse(thumb, m_renderer.BrushTextPrimary());

        D2D1_RECT_F pct_rc = D2D1::RectF(track_x1 + 6.0f, y, content_right, y + 24.0f);
        std::wstring pct_str = state.audio_muted ? L"0%" : std::format(L"{}%", static_cast<int>(std::round(vol_frac * 100.0f)));
        m_renderer.DrawTextSimple(pct_str, m_renderer.FontSmallBold(), pct_rc,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        y += 32.0f;
    }

    // Mute toggle
    {
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_StatusSection), m_renderer.FontSmallBold(),
            D2D1::RectF(content_left, y, content_right, y + 22.0f), m_renderer.BrushBrandBlue());
        y += 25.0f;
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_right - 50.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_Mute), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        D2D1_RECT_F pill = D2D1::RectF(content_right - 42.0f, y + 4.0f, content_right - 2.0f, y + 20.0f);
        m_renderer.FillRoundedRect(pill, 8.0f, state.audio_muted ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        float thumb_x = state.audio_muted ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 5.0f, m_renderer.BrushTextPrimary());
        D2D1_RECT_F row = D2D1::RectF(content_left, y, content_right, y + 24.0f);
        RegisterClickable(row, Control_Toggle_AudioMute, loc::Get(loc::S::Audio_Mute), true);
        y += 32.0f;
    }

    // Test Audio button
    {
        const bool hov = (state.hovered_control == Control_Btn_TestAudio);
        D2D1_RECT_F btn_rc = D2D1::RectF(content_left, y, content_left + 120.0f, y + 28.0f);
        m_renderer.DrawInset(btn_rc, 5.0f);
        if (hov) m_renderer.DrawRoundedRect(btn_rc, 5.0f, m_renderer.BrushCardBorder(), 1.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_TestButton), m_renderer.FontBody(), btn_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        RegisterClickable(btn_rc, Control_Btn_TestAudio, loc::Get(loc::S::Audio_PlayTestTone), true);
        y += 40.0f;
    }

    // Diagnostics section
    {
        D2D1_RECT_F sec_rc = D2D1::RectF(content_left, y, content_right, y + 20.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_Diagnostics), m_renderer.FontSmall(), sec_rc,
            m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y += 24.0f;

        auto DrawDiagRow = [&](const wchar_t* label, const std::wstring& value, float row_y) {
            D2D1_RECT_F lbl = D2D1::RectF(content_left, row_y, content_left + 160.0f, row_y + 20.0f);
            D2D1_RECT_F val = D2D1::RectF(content_left + 164.0f, row_y, content_right, row_y + 20.0f);
            m_renderer.DrawTextSimple(label, m_renderer.FontSmall(), lbl,
                m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_renderer.DrawTextSimple(value.c_str(), m_renderer.FontSmall(), val,
                m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        };

        auto buf_str = std::format(L"{:.1f} ms", state.audio_buffer_ms);
        auto ur_str  = std::format(L"{}", state.audio_underrun_count);
        DrawDiagRow(loc::Get(loc::S::Audio_SessionNameLbl), state.audio_session_display_name, y);  y += 22.0f;
        DrawDiagRow(loc::Get(loc::S::Audio_FormatLbl),      loc::Get(loc::S::Audio_FormatVal), y); y += 22.0f;
        DrawDiagRow(loc::Get(loc::S::Audio_BufferLbl),      buf_str,                          y);  y += 22.0f;
        DrawDiagRow(loc::Get(loc::S::Audio_UnderrunsLbl),   ur_str,                           y);  y += 22.0f;
        DrawDiagRow(loc::Get(loc::S::Audio_CurrentEndpoint), state.resolved_audio_device_name, y); y += 22.0f;
    }
    y += 12.0f;
    m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_TroubleshootSection), m_renderer.FontSmallBold(),
        D2D1::RectF(content_left, y, content_right, y + 22.0f), m_renderer.BrushBrandBlue());
    y += 26.0f;
    m_renderer.DrawTextSimple(loc::Get(loc::S::Audio_VolumeNote), m_renderer.FontSmall(),
        D2D1::RectF(content_left, y, content_right, y + 54.0f), m_renderer.BrushTextSecondary());
    y += 60.0f;

    m_content_height = y - area.top;
}

void MainWindowView::RenderPrivacyPage(const UiState& /*state*/, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_Privacy), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    const wchar_t* lines[] = {
        loc::Get(loc::S::Privacy_NoTelemetry),
        loc::Get(loc::S::Privacy_NoDataExternal),
        L"",
        loc::Get(loc::S::Privacy_OpenSourceDesc),
        L"https://github.com/duwn/duwn-mirror",
        L"",
        loc::Get(loc::S::Privacy_ThirdPartyComponents),
        loc::Get(loc::S::Privacy_ThirdPartyMore),
        loc::Get(loc::S::Privacy_SeeNotices),
    };
    for (const auto* line : lines) {
        D2D1_RECT_F lr = D2D1::RectF(content_left, y, content_right, y + 18.0f);
        if (*line) {
            m_renderer.DrawTextSimple(line, m_renderer.FontSmall(), lr,
                m_renderer.BrushTextSecondary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        y += 20.0f;
    }

    m_content_height = y - area.top;
}

void MainWindowView::RenderAdvancedPage(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Settings_Advanced), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    // Debug logging toggle
    {
        D2D1_RECT_F lbl_rc = D2D1::RectF(content_left, y, content_right - 50.0f, y + 24.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Adv_DebugLog), m_renderer.FontBody(), lbl_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        D2D1_RECT_F pill = D2D1::RectF(content_right - 42.0f, y + 4.0f, content_right - 2.0f, y + 20.0f);
        const bool on = state.debug_log;
        m_renderer.FillRoundedRect(pill, 8.0f, on ? m_renderer.BrushBrandBlue() : m_renderer.BrushCardBorder());
        float thumb_x = on ? (pill.right - 14.0f) : (pill.left + 2.0f);
        D2D1_RECT_F thumb = D2D1::RectF(thumb_x, pill.top + 2.0f, thumb_x + 12.0f, pill.bottom - 2.0f);
        m_renderer.FillRoundedRect(thumb, 5.0f, m_renderer.BrushTextPrimary());
        RegisterClickable(D2D1::RectF(content_left, y, content_right, y + 24.0f),
            Control_Toggle_DebugLog, loc::Get(loc::S::Adv_DebugLog), true);
        y += 32.0f;
    }

    // Flush pipeline button
    {
        const bool hov = (state.hovered_control == Control_Btn_FlushPipeline);
        D2D1_RECT_F btn_rc = D2D1::RectF(content_left, y, content_left + 150.0f, y + 28.0f);
        m_renderer.DrawInset(btn_rc, 5.0f);
        if (hov) m_renderer.DrawRoundedRect(btn_rc, 5.0f, m_renderer.BrushCardBorder(), 1.0f);
        m_renderer.DrawTextSimple(loc::Get(loc::S::Adv_FlushPipeline), m_renderer.FontBody(), btn_rc,
            m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        RegisterClickable(btn_rc, Control_Btn_FlushPipeline, loc::Get(loc::S::Adv_FlushPipeline), true);
        y += 40.0f;
    }
    const struct { int id; loc::S label; } actions[] = {
        {Control_Btn_OpenLogs, loc::S::Adv_OpenLogs},
        {Control_Btn_OpenSettingsFile, loc::S::Adv_OpenSettingsFile}
    };
    for (const auto& action : actions) {
        D2D1_RECT_F button = D2D1::RectF(content_left, y, content_left + 210.0f, y + 30.0f);
        RegisterClickable(button, action.id, loc::Get(action.label), true);
        m_renderer.DrawButton(button, loc::Get(action.label), false,
            state.hovered_control == action.id, state.pressed_control == action.id);
        y += 38.0f;
    }

    m_content_height = y - area.top;
}

void MainWindowView::RenderColorView(const UiState& state, const D2D1_RECT_F& area) noexcept {
    const float pad = 20.0f;
    const float content_left = area.left + pad;
    float content_right = area.right - pad;
    if (m_content_height > m_viewport_height) content_right -= 12.0f;
    float y = area.top + 14.0f;

    D2D1_RECT_F title_rc = D2D1::RectF(content_left, y, content_right, y + 26.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Nav_Color), m_renderer.FontTitle(), title_rc,
        m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += 38.0f;

    // Card 1: COLOR PRESETS & SLIDERS
    float clr_card_h = 356.0f;
    D2D1_RECT_F clr_card = D2D1::RectF(content_left, y, content_right, y + clr_card_h);
    m_renderer.DrawCard(clr_card, false, metrics::CardRadius);

    D2D1_RECT_F clr_header = D2D1::RectF(clr_card.left + 14.0f, clr_card.top + 7.0f, clr_card.left + 120.0f, clr_card.top + 24.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_Color), m_renderer.FontSmallBold(), clr_header, m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const std::wstring_view preset_names[] = {
        loc::Get(loc::S::Opt_Color_Neutral),
        loc::Get(loc::S::Opt_Color_Vivid),
        loc::Get(loc::S::Opt_Color_Soft),
        loc::Get(loc::S::Opt_Color_Custom)
    };
    float preset_btn_w = 110.0f;
    D2D1_RECT_F preset_rc = D2D1::RectF(clr_card.right - 14.0f - 66.0f - 8.0f - preset_btn_w, clr_card.top + 5.0f,
                                       clr_card.right - 14.0f - 66.0f - 8.0f, clr_card.top + 27.0f);
    m_renderer.DrawInset(preset_rc, 5.0f);
    RegisterClickable(preset_rc, Control_Set_ColorPreset, L"", true);
    if (state.open_dropdown == Control_Set_ColorPreset) {
        m_dropdown_anchor_rc = preset_rc;
        m_renderer.DrawRoundedRect(preset_rc, 5.0f, m_renderer.BrushBrandBlue(), 1.2f);
    }
    std::wstring preset_str = std::format(L"{}  ▼", preset_names[std::clamp(state.color_preset, 0, 3)]);
    m_renderer.DrawTextSimple(preset_str, m_renderer.FontSmall(), preset_rc,
                              m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F reset_rc = D2D1::RectF(clr_card.right - 14.0f - 66.0f, clr_card.top + 5.0f, clr_card.right - 14.0f, clr_card.top + 27.0f);
    RegisterClickable(reset_rc, Control_Set_ResetColor, loc::Get(loc::S::Video_ResetTooltip), true);
    m_renderer.DrawButton(reset_rc, loc::Get(loc::S::Common_Reset), false,
                          state.hovered_control == Control_Set_ResetColor,
                          state.pressed_control == Control_Set_ResetColor);

    const std::wstring_view slider_names[5] = {
        loc::Get(loc::S::Output_Brightness),
        loc::Get(loc::S::Output_Contrast),
        loc::Get(loc::S::Output_Saturation),
        loc::Get(loc::S::Output_Hue),
        loc::Get(loc::S::Output_Sharpness)
    };
    const int values[5] = { state.brightness, state.contrast, state.saturation, state.hue, state.sharpness };

    float sy = clr_card.top + 34.0f;
    const float srow_h = 42.0f;
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F row_rc = D2D1::RectF(clr_card.left + 14.0f, sy, clr_card.right - 14.0f, sy + srow_h);
        m_renderer.DrawInset(row_rc, 7.0f);

        const int id = Control_Set_Brightness + i;
        if (state.filter_supported[i]) {
            RegisterClickable(row_rc, id, loc::Get(loc::S::Video_ResetToZeroTooltip), true);
        }

        D2D1_RECT_F name_rc = D2D1::RectF(row_rc.left + 10.0f, sy + 4.0f, row_rc.left + 90.0f, sy + srow_h - 4.0f);
        m_renderer.DrawTextSimple(slider_names[i], m_renderer.FontSmallBold(), name_rc,
                                  state.filter_supported[i] ? m_renderer.BrushTextPrimary() : m_renderer.BrushTextMuted(),
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (state.filter_supported[i]) {
            const float x0 = row_rc.left + 95.0f;
            const float x1 = row_rc.right - 95.0f;
            const float y_mid = sy + srow_h * 0.5f;

            m_renderer.DrawLine(D2D1::Point2F(x0, y_mid), D2D1::Point2F(x1, y_mid), m_renderer.BrushCardBorder(), 3.0f);

            if (i == 4) {
                m_renderer.DrawLine(D2D1::Point2F(x0, y_mid - 6.0f), D2D1::Point2F(x0, y_mid + 6.0f), m_renderer.BrushTextMuted(), 2.0f);
            } else {
                const float x_center = (x0 + x1) * 0.5f;
                m_renderer.DrawLine(D2D1::Point2F(x_center, y_mid - 6.0f), D2D1::Point2F(x_center, y_mid + 6.0f), m_renderer.BrushTextMuted(), 2.0f);
            }

            float knob_x = 0.0f;
            if (i == 4) {
                knob_x = x0 + (x1 - x0) * (std::clamp(values[i], 0, 100) / 100.0f);
            } else {
                knob_x = x0 + (x1 - x0) * ((std::clamp(values[i], -100, 100) + 100) / 200.0f);
            }
            m_renderer.DrawStatusDot(D2D1::Point2F(knob_x, y_mid), 5.5f, colors::BrandBlue, true);

            D2D1_RECT_F num_rc = D2D1::RectF(row_rc.right - 88.0f, y_mid - 12.0f, row_rc.right - 36.0f, y_mid + 12.0f);
            m_renderer.DrawInset(num_rc, 4.0f);
            std::wstring num_str = (i != 4 && values[i] > 0) ? std::format(L"+{}", values[i]) : std::format(L"{}", values[i]);
            m_renderer.DrawTextSimple(num_str, m_renderer.FontSmallBold(), num_rc,
                                      values[i] != 0 ? m_renderer.BrushBrandBlue() : m_renderer.BrushTextMuted(),
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            D2D1_RECT_F rst_rc = D2D1::RectF(row_rc.right - 30.0f, y_mid - 12.0f, row_rc.right - 6.0f, y_mid + 12.0f);
            const int reset_id = Control_Reset_Brightness + i;
            RegisterClickable(rst_rc, reset_id, loc::Get(loc::S::Video_ResetTooltip), true);
            m_renderer.DrawButton(rst_rc, L"↺", false,
                                  state.hovered_control == reset_id,
                                  state.pressed_control == reset_id);
        } else {
            D2D1_RECT_F unsupp_rc = D2D1::RectF(row_rc.left + 100.0f, sy, row_rc.right - 10.0f, sy + srow_h);
            m_renderer.DrawTextSimple(loc::Get(loc::S::Video_FilterNotSupported), m_renderer.FontSmall(), unsupp_rc,
                                      m_renderer.BrushTextMuted(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        sy += srow_h + 4.0f;
    }

    y += clr_card_h + 16.0f;
    // Card 2: COLOR RANGE & MATRIX
    float cs_h = 104.0f;
    D2D1_RECT_F cs_card = D2D1::RectF(content_left, y, content_right, y + cs_h);
    m_renderer.DrawCard(cs_card, false, metrics::CardRadius);

    D2D1_RECT_F cs_header = D2D1::RectF(cs_card.left + 14.0f, cs_card.top + 8.0f, cs_card.right - 14.0f, cs_card.top + 24.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Video_AdvColorOverview), m_renderer.FontSmallBold(), cs_header, m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float row_y = cs_card.top + 32.0f;
    D2D1_RECT_F cr_lbl = D2D1::RectF(cs_card.left + 14.0f, row_y, cs_card.left + 150.0f, row_y + 24.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Output_ColorRange), m_renderer.FontBody(), cr_lbl, m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const std::wstring_view cr_names[] = { loc::Get(loc::S::Opt_Range_Auto), loc::Get(loc::S::Opt_Range_Limited), loc::Get(loc::S::Opt_Range_Full) };
    D2D1_RECT_F cr_dd = D2D1::RectF(cs_card.right - 180.0f, row_y, cs_card.right - 14.0f, row_y + 24.0f);
    m_renderer.DrawInset(cr_dd, 4.0f);
    RegisterClickable(cr_dd, Control_Set_ColorRange, L"", true);
    if (state.open_dropdown == Control_Set_ColorRange) {
        m_dropdown_anchor_rc = cr_dd;
        m_renderer.DrawRoundedRect(cr_dd, 4.0f, m_renderer.BrushBrandBlue(), 1.2f);
    }
    m_renderer.DrawTextSimple(std::format(L"{}  ▼", cr_names[std::clamp(state.color_range, 0, 2)]), m_renderer.FontSmall(), cr_dd,
                              m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    row_y += 32.0f;
    D2D1_RECT_F cm_lbl = D2D1::RectF(cs_card.left + 14.0f, row_y, cs_card.left + 150.0f, row_y + 24.0f);
    m_renderer.DrawTextSimple(loc::Get(loc::S::Output_ColorMatrix), m_renderer.FontBody(), cm_lbl, m_renderer.BrushTextPrimary(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const std::wstring_view cm_names[] = { loc::Get(loc::S::Opt_Matrix_Auto), loc::Get(loc::S::Opt_Matrix_601), loc::Get(loc::S::Opt_Matrix_709), loc::Get(loc::S::Opt_Matrix_2020) };
    D2D1_RECT_F cm_dd = D2D1::RectF(cs_card.right - 180.0f, row_y, cs_card.right - 14.0f, row_y + 24.0f);
    m_renderer.DrawInset(cm_dd, 4.0f);
    RegisterClickable(cm_dd, Control_Set_ColorMatrix, L"", true);
    if (state.open_dropdown == Control_Set_ColorMatrix) {
        m_dropdown_anchor_rc = cm_dd;
        m_renderer.DrawRoundedRect(cm_dd, 4.0f, m_renderer.BrushBrandBlue(), 1.2f);
    }
    m_renderer.DrawTextSimple(std::format(L"{}  ▼", cm_names[std::clamp(state.color_matrix, 0, 3)]), m_renderer.FontSmall(), cm_dd,
                              m_renderer.BrushBrandBlue(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    y += cs_h + 16.0f;
    m_content_height = y - area.top;
}

} // namespace duwn::ui
