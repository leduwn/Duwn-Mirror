#include "MainWindow.h"
#include "AppIcon.h"
#include "video/VideoGeometry.h"
#include "wired/WiredControlClient.h"
#include "common/logging/Logger.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <format>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace duwn::app {

static constexpr wchar_t kWindowClass[] = L"DUWNMirrorMainWindow";
static constexpr wchar_t kVideoChildClass[] = L"DuwnMirrorVideoChild";
static constexpr wchar_t kToolbarClass[] = L"DuwnMirrorScreenOnlyToolbar";
static constexpr UINT_PTR kTimerId = 1001;

LRESULT CALLBACK MainWindow::VideoChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hwnd, &ps);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

LRESULT CALLBACK MainWindow::ScreenOnlyToolbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    MainWindow* self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hwnd, &ps);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        HBRUSH bg_brush = ::CreateSolidBrush(RGB(18, 26, 48));
        HPEN border_pen = ::CreatePen(PS_SOLID, 1, RGB(46, 64, 105));
        HBRUSH old_brush = static_cast<HBRUSH>(::SelectObject(hdc, bg_brush));
        HPEN old_pen = static_cast<HPEN>(::SelectObject(hdc, border_pen));
        ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);

        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, RGB(243, 246, 255));
        HFONT font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        HFONT old_font = static_cast<HFONT>(::SelectObject(hdc, font));

        int w = rc.right - rc.left;
        RECT b1 = { 8, 4, 110, rc.bottom - 4 };
        RECT b2 = { 114, 4, 184, rc.bottom - 4 };
        RECT b3 = { 188, 4, w - 8, rc.bottom - 4 };

        ::DrawTextW(hdc, L"Workspace", -1, &b1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::DrawTextW(hdc, (self && self->m_fullscreen) ? L"Restore" : L"Full", -1, &b2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        bool muted = self ? self->m_state.audio_muted : false;
        ::DrawTextW(hdc, muted ? L"Unmute" : L"Mute", -1, &b3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        ::SelectObject(hdc, old_font);
        ::SelectObject(hdc, old_pen);
        ::SelectObject(hdc, old_brush);
        ::DeleteObject(border_pen);
        ::DeleteObject(bg_brush);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp);
        if (self) {
            if (x < 112) {
                self->m_state.is_screen_only = false;
                self->LayoutVideoSurface();
                if (self->m_hwnd) ::InvalidateRect(self->m_hwnd, nullptr, FALSE);
            } else if (x < 186) {
                self->ToggleFullscreen();
                ::InvalidateRect(hwnd, nullptr, TRUE);
            } else {
                if (self->m_hwnd) ::PostMessageW(self->m_hwnd, WM_APP + 0x010, 0, 0);
            }
        }
        return 0;
    }
    default:
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

MainWindow::MainWindow(std::function<void()> on_close) noexcept
    : m_on_close(std::move(on_close)) {}

MainWindow::~MainWindow() {
    if (m_screen_only_toolbar_hwnd) {
        ::DestroyWindow(m_screen_only_toolbar_hwnd);
        m_screen_only_toolbar_hwnd = nullptr;
    }
    if (m_video_surface_hwnd) {
        ::DestroyWindow(m_video_surface_hwnd);
        m_video_surface_hwnd = nullptr;
    }
    if (m_hwnd) {
        ::KillTimer(m_hwnd, kTimerId);
        m_view.Shutdown();
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void MainWindow::ApplyImmersiveDarkMode() noexcept {
    if (!m_hwnd) return;

    // Enable Windows 11/10 immersive dark mode
    BOOL dark_mode = TRUE;
    ::DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));

    // Custom dark titlebar palette (#0B1020 navy matching theme)
    // COLORREF is 0x00BBGGRR
    COLORREF caption_color = RGB(11, 16, 32);  // #0B1020
    COLORREF text_color    = RGB(243, 246, 255); // #F3F6FF
    COLORREF border_color  = RGB(30, 43, 77);   // #1E2B4D

    ::DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));
    ::DwmSetWindowAttribute(m_hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(text_color));
    ::DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &border_color, sizeof(border_color));
}

bool MainWindow::Create(const Settings& settings) noexcept {
    HINSTANCE hinstance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hinstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadAppIcon(nullptr, false);
    wc.hIconSm       = LoadAppIcon(nullptr, true);
    wc.hbrBackground = nullptr; // Null brush prevents flickering; Direct2D paints client area
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);

    // Initial desktop window size & coordinates from settings.json
    int x = (settings.window_preferences.x > -10000 && settings.window_preferences.x < 30000) ?
            settings.window_preferences.x : CW_USEDEFAULT;
    int y = (settings.window_preferences.y > -10000 && settings.window_preferences.y < 30000) ?
            settings.window_preferences.y : CW_USEDEFAULT;
    int w = settings.window_preferences.width >= 960 ? settings.window_preferences.width : 1280;
    int h = settings.window_preferences.height >= 560 ? settings.window_preferences.height : 740;

    m_hwnd = ::CreateWindowExW(
        0,
        kWindowClass,
        L"Duwn Mirror",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y,
        w, h,
        nullptr, nullptr,
        hinstance,
        this
    );

    if (!m_hwnd) {
        DUWN_LOG_ERROR("MainWindow", "CreateWindowExW failed");
        return false;
    }

    SetAppWindowIcons(m_hwnd);

    WNDCLASSEXW child_wc{};
    child_wc.cbSize        = sizeof(child_wc);
    child_wc.style         = CS_HREDRAW | CS_VREDRAW;
    child_wc.lpfnWndProc   = VideoChildWndProc;
    child_wc.hInstance     = hinstance;
    child_wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    child_wc.hbrBackground = nullptr;
    child_wc.lpszClassName = kVideoChildClass;
    ::RegisterClassExW(&child_wc);

    WNDCLASSEXW tb_wc{};
    tb_wc.cbSize        = sizeof(tb_wc);
    tb_wc.style         = CS_HREDRAW | CS_VREDRAW;
    tb_wc.lpfnWndProc   = ScreenOnlyToolbarWndProc;
    tb_wc.hInstance     = hinstance;
    tb_wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    tb_wc.hbrBackground = nullptr;
    tb_wc.lpszClassName = kToolbarClass;
    ::RegisterClassExW(&tb_wc);

    m_video_surface_hwnd = ::CreateWindowExW(
        0,
        kVideoChildClass,
        L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        m_hwnd,
        nullptr,
        hinstance,
        nullptr
    );

    m_screen_only_toolbar_hwnd = ::CreateWindowExW(
        0,
        kToolbarClass,
        L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        m_hwnd,
        nullptr,
        hinstance,
        this
    );

    m_state.aspect_locked = settings.aspect_ratio_locked;
    m_state.always_on_top = settings.always_on_top;
    m_state.audio_muted   = settings.audio_muted;

    ApplyImmersiveDarkMode();

    if (!m_view.Init(m_hwnd)) {
        DUWN_LOG_ERROR("MainWindow", "Direct2D MainWindowView init failed");
        return false;
    }

    m_view.SetOnTabChanged([this](ui::NavTab /*tab*/) {
        LayoutVideoSurface();
        if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
    });

    LayoutVideoSurface();

    // Set 4Hz (250ms) throttled UI telemetry refresh timer (Item 11)
    ::SetTimer(m_hwnd, kTimerId, 250, nullptr);

    if (settings.window_preferences.maximized) {
        ::ShowWindow(m_hwnd, SW_MAXIMIZE);
    } else {
        ::ShowWindow(m_hwnd, SW_SHOW);
    }
    ::UpdateWindow(m_hwnd);
    DUWN_LOG_INFO("MainWindow", "Direct2D Modern Control Window created");
    return true;
}

void MainWindow::SetTitle(std::wstring_view title) noexcept {
    if (m_hwnd) ::SetWindowTextW(m_hwnd, title.data());
}

void MainWindow::SetStatusText(std::wstring_view status) noexcept {
    m_status = status;
    m_state.status_message = m_status;
    SetTitle(L"Duwn Mirror");
    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::UpdateSessionState(airplay::AirPlaySessionState state) noexcept {
    m_state.session_state = state;
    using S = airplay::AirPlaySessionState;
    switch (state) {
    case S::Idle:
        m_state.status = ui::ConnectionStatus::Ready;
        m_state.status_message = L"Ready to connect";
        break;
    case S::Connecting:
        m_state.status = ui::ConnectionStatus::Connecting;
        m_state.status_message = (m_state.device_name != L"—" && !m_state.device_name.empty())
                                 ? L"Connecting to iPhone…"
                                 : L"Starting AirPlay…";
        break;
    case S::Connected:
        m_state.status = ui::ConnectionStatus::Streaming;
        m_state.status_message = L"Connected";
        break;
    case S::Streaming:
        m_state.status = ui::ConnectionStatus::Streaming;
        m_state.status_message = L"Streaming";
        break;
    case S::Paused:
        m_state.status = ui::ConnectionStatus::Paused;
        m_state.status_message = L"Paused (Static Screen)";
        break;
    case S::Disconnecting:
        m_state.status = ui::ConnectionStatus::Disconnected;
        m_state.status_message = L"Disconnecting…";
        break;
    case S::Error:
        m_state.status = ui::ConnectionStatus::Error;
        m_state.status_message = L"AirPlay Runtime Error";
        break;
    }
    LayoutVideoSurface();
    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::UpdateClientInfo(const airplay::AirPlayClientInfo& info) noexcept {
    m_state.device_name  = info.device_name.empty() ? L"Apple Device" : info.device_name;
    m_state.product_type = info.model.empty() ? L"—" : info.model;
    m_state.model_name   = info.model_marketing_name.empty() ?
        (info.model.empty() ? L"—" : info.model) : info.model_marketing_name;
    m_state.model_db_match = info.model.empty() ? L"—" : (info.is_exact_model_match ? L"Exact" : L"Fallback");

    if (!info.os_version.empty()) {
        m_state.os_version = std::format(L"{} {}", info.os_name, info.os_version);
    } else {
        m_state.os_version = info.os_name.empty() ? L"iOS" : info.os_name;
    }

    m_state.client_ip      = info.peer_address.empty() ? L"—" : info.peer_address;
    m_state.transport_type = L"Local RTP/UDP";
    m_state.signal_quality = L"N/A";

    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::UpdateSessionPhase(airplay::SessionPhase phase) noexcept {
    using P = airplay::SessionPhase;
    switch (phase) {
    case P::Advertising:
        m_state.status = ui::ConnectionStatus::Ready;
        m_state.status_message = L"Ready to connect";
        break;
    case P::Connecting:
        m_state.status = ui::ConnectionStatus::Connecting;
        m_state.status_message = (m_state.device_name != L"—" && !m_state.device_name.empty())
                                 ? L"Connecting to iPhone…"
                                 : L"Starting AirPlay…";
        break;
    case P::Streaming:
        m_state.status = ui::ConnectionStatus::Streaming;
        m_state.status_message = L"Streaming";
        break;
    case P::Reconnecting:
        m_state.status = ui::ConnectionStatus::Reconnecting;
        m_state.status_message = L"Reconnecting to iPhone…";
        break;
    case P::SidecarMissing:
        m_state.status = ui::ConnectionStatus::Error;
        m_state.status_message = L"AirPlay Runtime Error: uxplay.exe missing";
        break;
    case P::AdvertisingFailed:
        m_state.status = ui::ConnectionStatus::Error;
        m_state.status_message = L"AirPlay Runtime Error";
        break;
    }
    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::UpdateStreamMetadata(const airplay::StreamMetadata& meta) noexcept {
    m_state.width = meta.video_width;
    m_state.height = meta.video_height;
    if (meta.video_fps > 0.0) {
        m_state.nominal_fps = meta.video_fps;
    }
    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::UpdateTelemetry(double render_fps, double latency_ms,
                                  uint32_t queue_depth, uint64_t drops,
                                  int64_t uptime_sec) noexcept {
    m_state.render_fps = render_fps;
    m_state.pipeline_latency_ms = latency_ms;
    m_state.queue_depth = queue_depth;
    m_state.dropped_frames = drops;
    m_state.session_uptime_sec = uptime_sec;
    // Repaint triggered by timer or on demand
}

void MainWindow::UpdateExtendedTelemetry(uint64_t video_rtp, uint64_t audio_rtp,
                                         uint64_t audio_underruns, bool audio_active,
                                         bool audio_muted, std::wstring_view client_ip,
                                         std::wstring_view device_name) noexcept {
    m_state.video_rtp_packets = video_rtp;
    m_state.audio_rtp_packets = audio_rtp;
    m_state.audio_underruns   = audio_underruns;
    m_state.audio_active      = audio_active;
    m_state.audio_muted       = audio_muted;
    if (!client_ip.empty()) {
        m_state.client_ip = client_ip;
    }
    if (!device_name.empty()) {
        m_state.device_name = device_name;
    }
}

void MainWindow::SetOutputControlsState(bool visible, bool fullscreen, bool aspect_locked, bool always_on_top) noexcept {
    m_state.output_window_visible = visible;
    m_state.output_fullscreen     = fullscreen;
    m_state.aspect_locked         = aspect_locked;
    m_state.always_on_top         = always_on_top;
    if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SetOnToggleOutputWindow(std::function<void()> cb) noexcept {
    m_view.SetOnToggleOutputWindow(std::move(cb));
}

void MainWindow::SetOnToggleFullscreen(std::function<void()> cb) noexcept {
    m_view.SetOnToggleFullscreen(std::move(cb));
}

void MainWindow::SetOnToggleAspectLock(std::function<void()> cb) noexcept {
    m_view.SetOnToggleAspectLock(std::move(cb));
}

void MainWindow::SetOnToggleAlwaysOnTop(std::function<void()> cb) noexcept {
    m_view.SetOnToggleAlwaysOnTop(std::move(cb));
}

void MainWindow::SetOnToggleScreenOnly(std::function<void()> cb) noexcept {
    m_view.SetOnToggleScreenOnly([this, callback = std::move(cb)] {
        m_state.is_screen_only = !m_state.is_screen_only;
        LayoutVideoSurface();
        if (callback) callback();
        if (m_hwnd) ::InvalidateRect(m_hwnd, nullptr, FALSE);
    });
}

void MainWindow::SetOnTogglePreview(std::function<void()> cb) noexcept {
    m_view.SetOnTogglePreview(std::move(cb));
}

void MainWindow::SetOnFullscreenPreview(std::function<void()> cb) noexcept {
    m_view.SetOnFullscreenPreview(std::move(cb));
}

void MainWindow::SetOnTogglePreviewAlwaysOnTop(std::function<void()> cb) noexcept {
    m_view.SetOnTogglePreviewAlwaysOnTop(std::move(cb));
}

void MainWindow::SetOnToggleMute(std::function<void()> cb) noexcept {
    m_on_toggle_mute = cb;
    m_view.SetOnToggleMute(std::move(cb));
}

void MainWindow::SetOnVolumeChanged(std::function<void(float)> cb) noexcept {
    m_view.SetOnVolumeChanged(std::move(cb));
}

void MainWindow::SetOnDisconnect(std::function<void()> cb) noexcept {
    m_view.SetOnDisconnect(std::move(cb));
}

void MainWindow::SetOnFlushPipeline(std::function<void()> cb) noexcept {
    m_view.SetOnFlushPipeline(std::move(cb));
}

void MainWindow::SetOnSettingChanged(ui::MainWindowView::SettingCallback cb) noexcept {
    m_view.SetOnSettingChanged(std::move(cb));
}

void MainWindow::SetOnTestAudio(ui::MainWindowView::ActionCallback cb) noexcept {
    m_view.SetOnTestAudio(std::move(cb));
}

void MainWindow::SetOnAudioDeviceChanged(ui::MainWindowView::StringCallback cb) noexcept {
    m_view.SetOnAudioDeviceChanged(std::move(cb));
}

void MainWindow::SetOnLanguageChanged(ui::MainWindowView::StringCallback cb) noexcept {
    m_view.SetOnLanguageChanged(std::move(cb));
}

void MainWindow::SetOnTouchTap(ui::MainWindowView::TouchTapCallback cb) noexcept {
    m_on_touch_tap = cb;
    m_view.SetOnTouchTap(std::move(cb));
}

void MainWindow::SetOnTouchDrag(ui::MainWindowView::TouchDragCallback cb) noexcept {
    m_on_touch_drag = cb;
    m_view.SetOnTouchDrag(std::move(cb));
}

void MainWindow::LayoutVideoSurface() noexcept {
    if (!m_hwnd || !m_video_surface_hwnd) return;

    RECT rc{};
    ::GetClientRect(m_hwnd, &rc);
    int client_w = rc.right - rc.left;
    int client_h = rc.bottom - rc.top;
    if (client_w <= 0 || client_h <= 0) return;

    if (m_state.is_screen_only) {
        ::SetWindowPos(m_video_surface_hwnd, HWND_BOTTOM, 0, 0, client_w, client_h, SWP_SHOWWINDOW);
        if (m_on_video_surface_resize) {
            m_on_video_surface_resize(static_cast<uint32_t>(client_w), static_cast<uint32_t>(client_h));
        }

        if (m_screen_only_toolbar_hwnd) {
            int tb_w = 280;
            int tb_h = 38;
            int tb_x = client_w - tb_w - 24;
            int tb_y = 20;
            ::SetWindowPos(m_screen_only_toolbar_hwnd, HWND_TOP, tb_x, tb_y, tb_w, tb_h, SWP_SHOWWINDOW);
            ::InvalidateRect(m_screen_only_toolbar_hwnd, nullptr, TRUE);
        }
    } else if (m_state.active_tab == ui::NavTab::Mirror) {
        if (m_screen_only_toolbar_hwnd) {
            ::ShowWindow(m_screen_only_toolbar_hwnd, SW_HIDE);
        }

        float dpi_x = m_view.DpiX();
        float dpi_y = m_view.DpiY();
        if (dpi_x <= 0.0f) dpi_x = 96.0f;
        if (dpi_y <= 0.0f) dpi_y = 96.0f;
        float scale_x = dpi_x / 96.0f;
        float scale_y = dpi_y / 96.0f;

        float total_w_dip = static_cast<float>(client_w) / scale_x;
        float total_h_dip = static_cast<float>(client_h) / scale_y;
        auto tier = ui::MainWindowView::CalculateTier(total_w_dip);

        float sidebar_w = (tier == ui::LayoutTier::Large) ? 200.0f : ((tier == ui::LayoutTier::Medium) ? 68.0f : 60.0f);
        float header_h = 48.0f;
        float status_h = 28.0f;
        float body_y = header_h;
        float body_h = total_h_dip - header_h - status_h;
        float mirror_y = body_y + 8.0f;
        float mirror_h = body_h - 16.0f;

        float right_w = (tier == ui::LayoutTier::Large) ? 340.0f : ((tier == ui::LayoutTier::Medium) ? 310.0f : 0.0f);
        float preview_w = total_w_dip - sidebar_w - right_w;

        float pad = 24.0f;
        float card_l = sidebar_w + pad;
        float card_t = mirror_y + pad;
        float card_w = std::max(64.0f, preview_w - pad * 2.0f);
        float card_h = std::max(64.0f, mirror_h - pad * 2.0f);

        int px = static_cast<int>(card_l * scale_x);
        int py = static_cast<int>(card_t * scale_y);
        int pw = static_cast<int>(card_w * scale_x);
        int ph = static_cast<int>(card_h * scale_y);

        ::SetWindowPos(m_video_surface_hwnd, HWND_TOP, px, py, pw, ph, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        if (m_on_video_surface_resize) {
            m_on_video_surface_resize(static_cast<uint32_t>(pw), static_cast<uint32_t>(ph));
        }
    } else {
        if (m_screen_only_toolbar_hwnd) {
            ::ShowWindow(m_screen_only_toolbar_hwnd, SW_HIDE);
        }
        ::ShowWindow(m_video_surface_hwnd, SW_HIDE);
    }
}

void MainWindow::ToggleFullscreen() noexcept {
    if (!m_hwnd) return;
    if (!m_fullscreen) {
        m_prev_placement.length = sizeof(WINDOWPLACEMENT);
        ::GetWindowPlacement(m_hwnd, &m_prev_placement);
        DWORD style = ::GetWindowLongW(m_hwnd, GWL_STYLE);
        HMONITOR hmon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (::GetMonitorInfoW(hmon, &mi)) {
            ::SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            ::SetWindowPos(m_hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
            m_state.output_fullscreen = true;
        }
    } else {
        DWORD style = ::GetWindowLongW(m_hwnd, GWL_STYLE);
        ::SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        ::SetWindowPlacement(m_hwnd, &m_prev_placement);
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_fullscreen = false;
        m_state.output_fullscreen = false;
    }
    LayoutVideoSurface();
}


LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg,
                                      WPARAM wp, LPARAM lp) noexcept {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hwnd, &ps);
        m_view.Render(m_state);
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == kTimerId) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_SIZE: {
        uint32_t w = LOWORD(lp);
        uint32_t h = HIWORD(lp);
        m_view.Resize(w, h);
        LayoutVideoSurface();
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP + 0x010:
        if (m_on_toggle_mute) m_on_toggle_mute();
        if (m_screen_only_toolbar_hwnd) ::InvalidateRect(m_screen_only_toolbar_hwnd, nullptr, TRUE);
        return 0;

    case WM_APP + 0x020: // Toggle or set screen-only (wp: 0=off, 1=on, 2=toggle)
        if (wp == 2) m_state.is_screen_only = !m_state.is_screen_only;
        else m_state.is_screen_only = (wp != 0);
        LayoutVideoSurface();
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_APP + 0x021: // Set active tab (wp = NavTab enum)
        m_state.active_tab = static_cast<ui::NavTab>(wp);
        LayoutVideoSurface();
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        if (!m_mouse_tracking) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize      = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags     = TME_LEAVE;
            tme.hwndTrack   = hwnd;
            ::TrackMouseEvent(&tme);
            m_mouse_tracking = true;
        }

        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        if (m_view.OnMouseMove(x, y, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        ::SetCapture(hwnd);
        if (m_view.OnMouseDown(x, y, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        if (m_view.OnDoubleClick(x, y, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        ::ReleaseCapture();
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        if (m_view.OnMouseUp(x, y, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        m_mouse_tracking = false;
        if (m_view.OnMouseLeave(m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ::ScreenToClient(hwnd, &pt);
        if (m_view.OnMouseWheel(pt.x, pt.y, delta, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (m_view.OnKeyDown(wp, m_state)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 960;  // Minimum window width (Item 14)
        mmi->ptMinTrackSize.y = 560;  // Minimum window height
        return 0;
    }

    case WM_DPICHANGED: {
        // Per-Monitor DPI scaling support (Item 13)
        SetAppWindowIcons(hwnd);
        auto* prc = reinterpret_cast<RECT*>(lp);
        ::SetWindowPos(hwnd, nullptr,
            prc->left, prc->top,
            prc->right - prc->left, prc->bottom - prc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Direct2D handles all background painting

    case WM_DESTROY:
        if (m_on_close) m_on_close();
        ::PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace duwn::app
