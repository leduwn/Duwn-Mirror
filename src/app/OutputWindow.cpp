#include "OutputWindow.h"
#include "AppIcon.h"
#include "ui/Loc.h"
#include "common/logging/Logger.h"
#include <windowsx.h>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace duwn::app {

bool OutputWindow::s_class_registered = false;

OutputWindow::~OutputWindow() {
    if (m_hwnd) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    s_instance_count.fetch_sub(1, std::memory_order_relaxed);
}

float OutputWindow::GetDpiScale() const noexcept {
    if (!m_hwnd) return 1.0f;
    UINT dpi = ::GetDpiForWindow(m_hwnd);
    return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

int OutputWindow::GetToolbarHeightPx() const noexcept {
    if (!m_show_toolbar || m_fullscreen) return 0;
    return static_cast<int>(std::round(38.0f * GetDpiScale()));
}

uint32_t OutputWindow::VideoSurfaceWidth() const noexcept {
    if (!m_video_surface_hwnd) return m_client_w.load(std::memory_order_relaxed);
    RECT rc{};
    ::GetClientRect(m_video_surface_hwnd, &rc);
    int w = rc.right - rc.left;
    return w > 0 ? static_cast<uint32_t>(w) : m_client_w.load(std::memory_order_relaxed);
}

uint32_t OutputWindow::VideoSurfaceHeight() const noexcept {
    if (!m_video_surface_hwnd) return m_client_h.load(std::memory_order_relaxed);
    RECT rc{};
    ::GetClientRect(m_video_surface_hwnd, &rc);
    int h = rc.bottom - rc.top;
    return h > 0 ? static_cast<uint32_t>(h) : m_client_h.load(std::memory_order_relaxed);
}

bool OutputWindow::Create(uint32_t width, uint32_t height,
                          ResizeCallback on_resize) noexcept {
    const uint32_t current_instances = s_instance_count.fetch_add(1, std::memory_order_relaxed) + 1;
    assert(current_instances <= 1 && "TopLevelOutputWindows must be <= 1");
    if (current_instances > 1) {
        DUWN_LOG_ERROR("OutputWindow", "Attempted to create duplicate OutputWindow instance!");
    }

    m_on_resize = std::move(on_resize);
    m_client_w.store(width, std::memory_order_relaxed);
    m_client_h.store(height, std::memory_order_relaxed);

    HINSTANCE hInst = ::GetModuleHandleW(nullptr);

    // Register classes once
    if (!s_class_registered) {
        // 1. Top-level window class
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = LoadAppIcon(nullptr, false);
        wc.hIconSm       = LoadAppIcon(nullptr, true);
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kClassName;

        if (!::RegisterClassExW(&wc)) {
            DUWN_LOG_ERROR("OutputWindow", "RegisterClassExW for main class failed");
            return false;
        }

        // 2. Child toolbar class
        WNDCLASSEXW tb_wc{};
        tb_wc.cbSize        = sizeof(tb_wc);
        tb_wc.style         = CS_HREDRAW | CS_VREDRAW;
        tb_wc.lpfnWndProc   = ToolbarWndProc;
        tb_wc.hInstance     = hInst;
        tb_wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        tb_wc.hbrBackground = nullptr; // Double-buffered GDI
        tb_wc.lpszClassName = kToolbarClass;
        ::RegisterClassExW(&tb_wc);

        // 3. Child video surface class
        WNDCLASSEXW vid_wc{};
        vid_wc.cbSize        = sizeof(vid_wc);
        vid_wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        vid_wc.lpfnWndProc   = VideoChildWndProc;
        vid_wc.hInstance     = hInst;
        vid_wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        vid_wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
        vid_wc.lpszClassName = kVideoSurfaceClass;
        ::RegisterClassExW(&vid_wc);

        // 4. Child placeholder class (waiting screen when stream inactive)
        WNDCLASSEXW ph_wc{};
        ph_wc.cbSize        = sizeof(ph_wc);
        ph_wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        ph_wc.lpfnWndProc   = PlaceholderWndProc;
        ph_wc.hInstance     = hInst;
        ph_wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        ph_wc.hbrBackground = nullptr;
        ph_wc.lpszClassName = kPlaceholderClass;
        ::RegisterClassExW(&ph_wc);

        s_class_registered = true;
    }

    constexpr DWORD style    = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    constexpr DWORD ex_style = WS_EX_APPWINDOW;

    int win_w = 480;
    int win_h = 854;
    int win_x = CW_USEDEFAULT;
    int win_y = CW_USEDEFAULT;

    // Calculate initial comfortable size on work area
    HMONITOR hmon = ::MonitorFromWindow(::GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    if (::GetMonitorInfoW(hmon, &mi)) {
        int work_h = mi.rcWork.bottom - mi.rcWork.top;
        win_h = static_cast<int>(std::round(work_h * 0.60f));
        win_w = static_cast<int>(std::round(win_h * (9.0 / 16.0)));
        win_x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - win_w) / 2;
        win_y = mi.rcWork.top + (work_h - win_h) / 2;
    }

    RECT wr{ 0, 0, win_w, win_h + 38 };
    ::AdjustWindowRectEx(&wr, style, FALSE, ex_style);

    m_hwnd = ::CreateWindowExW(
        ex_style,
        kClassName,
        kWindowTitle,
        style,
        win_x, win_y,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr,
        hInst,
        this
    );

    if (!m_hwnd) {
        DUWN_LOG_ERROR("OutputWindow", "CreateWindowExW failed");
        return false;
    }

    SetAppWindowIcons(m_hwnd);

    // Create toolbar child
    m_toolbar_hwnd = ::CreateWindowExW(
        0,
        kToolbarClass,
        L"",
        WS_CHILD | (m_show_toolbar ? WS_VISIBLE : 0) | WS_CLIPSIBLINGS,
        0, 0, win_w, 38,
        m_hwnd,
        reinterpret_cast<HMENU>(101),
        hInst,
        this
    );

    // Create video surface child
    m_video_surface_hwnd = ::CreateWindowExW(
        0,
        kVideoSurfaceClass,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 38, win_w, win_h,
        m_hwnd,
        reinterpret_cast<HMENU>(102),
        hInst,
        this
    );

    // Create placeholder child
    m_placeholder_hwnd = ::CreateWindowExW(
        0,
        kPlaceholderClass,
        L"",
        WS_CHILD | (m_has_frame ? 0 : WS_VISIBLE) | WS_CLIPSIBLINGS,
        0, 38, win_w, win_h,
        m_hwnd,
        reinterpret_cast<HMENU>(103),
        hInst,
        this
    );

    LayoutChildren();

    DUWN_LOG_INFOF("OutputWindow", "Created standalone output window HWND={:p} Toolbar={:p} VideoSurface={:p} Placeholder={:p}",
        static_cast<void*>(m_hwnd), static_cast<void*>(m_toolbar_hwnd), static_cast<void*>(m_video_surface_hwnd), static_cast<void*>(m_placeholder_hwnd));

    return true;
}

void OutputWindow::LayoutChildren() noexcept {
    if (!m_hwnd) return;
    RECT rc{};
    ::GetClientRect(m_hwnd, &rc);
    int client_w = rc.right - rc.left;
    int client_h = rc.bottom - rc.top;
    if (client_w <= 0 || client_h <= 0) return;

    m_client_w.store(static_cast<uint32_t>(client_w), std::memory_order_relaxed);
    m_client_h.store(static_cast<uint32_t>(client_h), std::memory_order_relaxed);

    int tb_h = GetToolbarHeightPx();
    if (tb_h > 0 && m_toolbar_hwnd) {
        ::SetWindowPos(m_toolbar_hwnd, HWND_TOP, 0, 0, client_w, tb_h, SWP_SHOWWINDOW);
        ::InvalidateRect(m_toolbar_hwnd, nullptr, TRUE);
    } else if (m_toolbar_hwnd) {
        ::ShowWindow(m_toolbar_hwnd, SW_HIDE);
    }

    int vid_y = tb_h;
    int vid_h = std::max(1, client_h - tb_h);
    if (m_video_surface_hwnd) {
        ::SetWindowPos(m_video_surface_hwnd, HWND_BOTTOM, 0, vid_y, client_w, vid_h, SWP_SHOWWINDOW);
    }
    if (m_placeholder_hwnd) {
        ::SetWindowPos(m_placeholder_hwnd, HWND_TOP, 0, vid_y, client_w, vid_h,
                       m_has_frame ? SWP_HIDEWINDOW : SWP_SHOWWINDOW);
        if (!m_has_frame) {
            ::InvalidateRect(m_placeholder_hwnd, nullptr, TRUE);
        }
    }
}

void OutputWindow::EnsureAccessiblePlacement() noexcept {
    if (!m_hwnd || m_fullscreen) return;
    RECT rc{};
    if (!::GetWindowRect(m_hwnd, &rc)) return;
    HMONITOR hMon = ::MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!hMon) {
        hMon = ::MonitorFromWindow(::GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        if (::GetMonitorInfoW(hMon, &mi)) {
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            const int work_w = mi.rcWork.right - mi.rcWork.left;
            const int work_h = mi.rcWork.bottom - mi.rcWork.top;
            if (w > work_w) w = work_w;
            if (h > work_h) h = work_h;
            int x = mi.rcWork.left + (work_w - w) / 2;
            int y = mi.rcWork.top + (work_h - h) / 2;
            ::SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

void OutputWindow::Show() noexcept {
    if (m_hwnd) {
        m_user_hidden_for_session = false;
        if (::IsIconic(m_hwnd)) {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        } else {
            ::ShowWindow(m_hwnd, SW_SHOW);
        }
        EnsureAccessiblePlacement();
        LayoutChildren();
        if (m_on_visibility) m_on_visibility(true);
    }
}

void OutputWindow::ShowNoActivate() noexcept {
    if (m_hwnd) {
        m_user_hidden_for_session = false;
        if (::IsIconic(m_hwnd)) {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        } else {
            ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        EnsureAccessiblePlacement();
        LayoutChildren();
        if (m_on_visibility) m_on_visibility(true);
    }
}

void OutputWindow::Hide() noexcept {
    if (m_hwnd) {
        ::ShowWindow(m_hwnd, SW_HIDE);
        if (m_on_visibility) m_on_visibility(false);
    }
}

void OutputWindow::ToggleVisibility() noexcept {
    if (IsVisible()) {
        Hide();
        m_user_hidden_for_session = true;
    } else {
        Show();
    }
}

bool OutputWindow::IsVisible() const noexcept {
    return m_hwnd && ::IsWindowVisible(m_hwnd);
}

void OutputWindow::SetAlwaysOnTop(bool top) noexcept {
    m_always_on_top = top;
    if (m_hwnd) {
        ::SetWindowPos(m_hwnd, top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (m_toolbar_hwnd) ::InvalidateRect(m_toolbar_hwnd, nullptr, TRUE);
    }
}

void OutputWindow::ToggleAlwaysOnTop() noexcept {
    SetAlwaysOnTop(!m_always_on_top);
}

void OutputWindow::SetToolbarVisible(bool visible) noexcept {
    m_show_toolbar = visible;
    if (!m_fullscreen) {
        m_restore_toolbar = visible;
    }
    LayoutChildren();
    if (m_on_resize) {
        m_on_resize(VideoSurfaceWidth(), VideoSurfaceHeight());
    }
}

void OutputWindow::SetHasFrame(bool has) noexcept {
    if (m_has_frame != has) {
        m_has_frame = has;
        if (m_placeholder_hwnd) {
            ::ShowWindow(m_placeholder_hwnd, has ? SW_HIDE : SW_SHOW);
            if (!has) {
                ::InvalidateRect(m_placeholder_hwnd, nullptr, TRUE);
            }
        }
        if (m_video_surface_hwnd) {
            ::InvalidateRect(m_video_surface_hwnd, nullptr, TRUE);
        }
    }
}

void OutputWindow::SetAudioState(bool muted, float volume) noexcept {
    m_audio_muted = muted;
    m_audio_volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_toolbar_hwnd) {
        ::InvalidateRect(m_toolbar_hwnd, nullptr, TRUE);
    }
}

void OutputWindow::SetVideoGeometry(uint32_t video_w, uint32_t video_h) noexcept {
    if (video_w == 0 || video_h == 0) return;
    m_video_w.store(video_w, std::memory_order_relaxed);
    m_video_h.store(video_h, std::memory_order_relaxed);
}

void OutputWindow::GetWindowRect(int& x, int& y, int& w, int& h) const noexcept {
    if (!m_hwnd) return;
    RECT rc{};
    ::GetWindowRect(m_hwnd, &rc);
    x = rc.left; y = rc.top;
    w = rc.right - rc.left; h = rc.bottom - rc.top;
}

void OutputWindow::SetWindowRect(int x, int y, int w, int h) noexcept {
    if (!m_hwnd || w <= 0 || h <= 0) return;
    ::SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    EnsureAccessiblePlacement();
    LayoutChildren();
}



void OutputWindow::ApplyComfortableFit() noexcept {
    if (!m_hwnd || m_fullscreen || ::IsZoomed(m_hwnd)) return;
    uint32_t vw = m_video_w.load(std::memory_order_relaxed);
    uint32_t vh = m_video_h.load(std::memory_order_relaxed);
    if (vw == 0 || vh == 0) { vw = 1080; vh = 1920; }

    HMONITOR hmon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(hmon, &mi)) return;

    int work_w = mi.rcWork.right - mi.rcWork.left;
    int work_h = mi.rcWork.bottom - mi.rcWork.top;
    int tb_h = GetToolbarHeightPx();

    int target_client_w = 0;
    int target_client_h = 0;
    double ar = static_cast<double>(vw) / static_cast<double>(vh);

    if (vw < vh) {
        target_client_h = static_cast<int>(std::round(work_h * 0.60f));
        target_client_w = static_cast<int>(std::round(target_client_h * ar));
    } else {
        target_client_w = static_cast<int>(std::round(work_w * 0.50f));
        target_client_h = static_cast<int>(std::round(target_client_w / ar));
    }

    RECT win_rc{0, 0, target_client_w, target_client_h + tb_h};
    DWORD style = static_cast<DWORD>(::GetWindowLongW(m_hwnd, GWL_STYLE));
    DWORD ex_style = static_cast<DWORD>(::GetWindowLongW(m_hwnd, GWL_EXSTYLE));
    ::AdjustWindowRectEx(&win_rc, style, FALSE, ex_style);
    int total_w = win_rc.right - win_rc.left;
    int total_h = win_rc.bottom - win_rc.top;

    RECT cur_rc{};
    ::GetWindowRect(m_hwnd, &cur_rc);
    int cx = cur_rc.left + (cur_rc.right - cur_rc.left) / 2;
    int cy = cur_rc.top + (cur_rc.bottom - cur_rc.top) / 2;

    ::SetWindowPos(m_hwnd, nullptr, cx - total_w / 2, cy - total_h / 2, total_w, total_h,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    EnsureAccessiblePlacement();
    LayoutChildren();
}

void OutputWindow::OnStreamGeometryChanged(uint32_t new_src_w, uint32_t new_src_h) noexcept {
    SetVideoGeometry(new_src_w, new_src_h);
    if (!m_hwnd || m_fullscreen || ::IsZoomed(m_hwnd)) return;

    RECT cur_rc{};
    if (!::GetWindowRect(m_hwnd, &cur_rc)) return;
    int cur_w = cur_rc.right - cur_rc.left;
    int cur_h = cur_rc.bottom - cur_rc.top;

    double area = static_cast<double>(cur_w * cur_h);
    double new_ar = static_cast<double>(new_src_w) / static_cast<double>(new_src_h);

    int tb_h = GetToolbarHeightPx();
    int new_video_h = static_cast<int>(std::round(std::sqrt(area / new_ar)));
    int new_video_w = static_cast<int>(std::round(new_video_h * new_ar));

    HMONITOR hmon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (::GetMonitorInfoW(hmon, &mi)) {
        int work_w = mi.rcWork.right - mi.rcWork.left;
        int work_h = mi.rcWork.bottom - mi.rcWork.top;
        if (new_video_w > work_w * 0.70) {
            new_video_w = static_cast<int>(work_w * 0.70);
            new_video_h = static_cast<int>(std::round(new_video_w / new_ar));
        }
        if (new_video_h + tb_h > work_h * 0.70) {
            new_video_h = static_cast<int>(work_h * 0.70 - tb_h);
            new_video_w = static_cast<int>(std::round(new_video_h * new_ar));
        }
    }

    RECT win_rc{0, 0, new_video_w, new_video_h + tb_h};
    DWORD style = static_cast<DWORD>(::GetWindowLongW(m_hwnd, GWL_STYLE));
    DWORD ex_style = static_cast<DWORD>(::GetWindowLongW(m_hwnd, GWL_EXSTYLE));
    ::AdjustWindowRectEx(&win_rc, style, FALSE, ex_style);
    int final_w = win_rc.right - win_rc.left;
    int final_h = win_rc.bottom - win_rc.top;

    int center_x = cur_rc.left + cur_w / 2;
    int center_y = cur_rc.top + cur_h / 2;
    int new_x = center_x - final_w / 2;
    int new_y = center_y - final_h / 2;

    ::SetWindowPos(m_hwnd, nullptr, new_x, new_y, final_w, final_h,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    EnsureAccessiblePlacement();
    LayoutChildren();
}

void OutputWindow::ToggleFullscreen() noexcept {
    if (!m_hwnd) return;

    if (!m_fullscreen) {
        ::GetWindowRect(m_hwnd, &m_restore_rect);
        m_restore_toolbar = m_show_toolbar;

        HMONITOR monitor = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (::GetMonitorInfoW(monitor, &mi)) {
            const RECT& r = mi.rcMonitor;
            ::SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            ::SetWindowPos(m_hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                           SWP_FRAMECHANGED | SWP_NOACTIVATE);
            m_fullscreen = true;
            LayoutChildren();
            if (m_on_resize) m_on_resize(r.right - r.left, r.bottom - r.top);
        }
    } else {
        constexpr DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        ::SetWindowLongW(m_hwnd, GWL_STYLE, style);
        ::SetWindowPos(m_hwnd, m_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                       m_restore_rect.left, m_restore_rect.top,
                       m_restore_rect.right - m_restore_rect.left,
                       m_restore_rect.bottom - m_restore_rect.top,
                       SWP_FRAMECHANGED | SWP_NOACTIVATE);
        m_fullscreen = false;
        m_show_toolbar = m_restore_toolbar;
        LayoutChildren();
        if (m_on_resize) m_on_resize(VideoSurfaceWidth(), VideoSurfaceHeight());
    }
}



LRESULT CALLBACK OutputWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    OutputWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OutputWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<OutputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK OutputWindow::ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    OutputWindow* self = reinterpret_cast<OutputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OutputWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) return self->HandleToolbarMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK OutputWindow::VideoChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    OutputWindow* self = reinterpret_cast<OutputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OutputWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) return self->HandleVideoChildMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK OutputWindow::PlaceholderWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    OutputWindow* self = reinterpret_cast<OutputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OutputWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) return self->HandlePlaceholderMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OutputWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_SIZE: {
        if (wp != SIZE_MINIMIZED) {
            uint32_t w = LOWORD(lp);
            uint32_t h = HIWORD(lp);
            if (w > 0 && h > 0) {
                m_client_w.store(w, std::memory_order_relaxed);
                m_client_h.store(h, std::memory_order_relaxed);
                LayoutChildren();
                if (m_on_resize) {
                    m_on_resize(VideoSurfaceWidth(), VideoSurfaceHeight());
                }
            }
        }
        return 0;
    }

    case WM_SIZING: {
        if (!m_fullscreen && !::IsZoomed(hwnd)) {
            uint32_t vw = m_video_w.load(std::memory_order_relaxed);
            uint32_t vh = m_video_h.load(std::memory_order_relaxed);
            if (vw == 0 || vh == 0) { vw = 16; vh = 9; }
            double target_ar = static_cast<double>(vw) / static_cast<double>(vh);

            RECT win_rc{}, client_rc{};
            ::GetWindowRect(hwnd, &win_rc);
            ::GetClientRect(hwnd, &client_rc);
            int border_w = (win_rc.right - win_rc.left) - (client_rc.right - client_rc.left);
            int border_h = (win_rc.bottom - win_rc.top) - (client_rc.bottom - client_rc.top);
            int tb_h = GetToolbarHeightPx();

            auto* prc = reinterpret_cast<RECT*>(lp);
            int cur_w = prc->right - prc->left;
            int cur_h = prc->bottom - prc->top;
            int client_vid_w = std::max(100, cur_w - border_w);
            int client_vid_h = std::max(100, cur_h - border_h - tb_h);

            switch (wp) {
            case WMSZ_LEFT:
            case WMSZ_RIGHT:
                client_vid_h = static_cast<int>(std::round(client_vid_w / target_ar));
                prc->bottom = prc->top + client_vid_h + border_h + tb_h;
                break;
            case WMSZ_TOP:
            case WMSZ_BOTTOM:
                client_vid_w = static_cast<int>(std::round(client_vid_h * target_ar));
                prc->right = prc->left + client_vid_w + border_w;
                break;
            case WMSZ_TOPLEFT:
            case WMSZ_BOTTOMLEFT:
                client_vid_w = static_cast<int>(std::round(client_vid_h * target_ar));
                prc->left = prc->right - (client_vid_w + border_w);
                break;
            case WMSZ_TOPRIGHT:
            case WMSZ_BOTTOMRIGHT:
            default:
                client_vid_w = static_cast<int>(std::round(client_vid_h * target_ar));
                prc->right = prc->left + (client_vid_w + border_w);
                break;
            }
            return TRUE;
        }
        return FALSE;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 320;
        mmi->ptMinTrackSize.y = 240;
        return 0;
    }

    case WM_KEYDOWN: {
        if (wp == VK_F11 || (wp == VK_RETURN && (::GetKeyState(VK_MENU) < 0))) {
            ToggleFullscreen();
            return 0;
        }
        if (wp == VK_ESCAPE && m_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        Hide();
        m_user_hidden_for_session = true;
        if (m_on_visibility) m_on_visibility(false);
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

struct ToolbarLayout {
    RECT mute_rc;
    RECT fit_rc;
    RECT pin_rc;
    RECT fs_rc;
    RECT prc;
    int sx0;
    int sx1;
    int sy;
    RECT s_hit;
    bool has_slider;
};

static ToolbarLayout ComputeToolbarLayout(int w, float s) noexcept {
    ToolbarLayout l{};
    int bh = static_cast<int>(26.0f * s), ty = static_cast<int>(6.0f * s);
    int gap = static_cast<int>(6.0f * s);

    int fs_w = static_cast<int>(62.0f * s);
    int pin_w = static_cast<int>(46.0f * s);
    int fit_w = static_cast<int>(44.0f * s);
    int mute_w = static_cast<int>(64.0f * s);

    int cur_r = w - static_cast<int>(8.0f * s);
    l.fs_rc = RECT{ cur_r - fs_w, ty, cur_r, ty + bh };
    cur_r -= (fs_w + gap);

    l.pin_rc = RECT{ cur_r - pin_w, ty, cur_r, ty + bh };
    cur_r -= (pin_w + gap);

    l.fit_rc = RECT{ cur_r - fit_w, ty, cur_r, ty + bh };
    cur_r -= (fit_w + gap);

    int left_x = static_cast<int>(8.0f * s);
    l.mute_rc = RECT{ left_x, ty, left_x + mute_w, ty + bh };
    int avail_w = cur_r - (l.mute_rc.right + static_cast<int>(8.0f * s));

    int prc_w = static_cast<int>(32.0f * s);
    if (avail_w >= static_cast<int>(50.0f * s)) {
        l.has_slider = true;
        l.sx0 = l.mute_rc.right + static_cast<int>(8.0f * s);
        l.sx1 = cur_r - prc_w - static_cast<int>(6.0f * s);
        l.sy  = static_cast<int>(17.0f * s);
        l.s_hit = RECT{ l.sx0 - 4, ty, l.sx1 + 4, ty + bh };
        l.prc = RECT{ l.sx1 + static_cast<int>(4.0f * s), ty, cur_r, ty + bh };
    } else {
        l.has_slider = false;
        l.sx0 = 0; l.sx1 = 0; l.sy = 0;
        l.s_hit = RECT{ 0, 0, 0, 0 };
        l.prc = RECT{ 0, 0, 0, 0 };
    }
    return l;
}

static void DrawOutputToolbar(HWND hwnd, OutputWindow* win, float s,
                              int hov_btn, bool muted, float vol, bool pin, bool fs) {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hwnd, &ps);
    RECT rc{}; ::GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    ToolbarLayout layout = ComputeToolbarLayout(w, s);

    HDC mdc = ::CreateCompatibleDC(hdc);
    HBITMAP mbm = ::CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ obm = ::SelectObject(mdc, mbm);

    HBRUSH bg = ::CreateSolidBrush(RGB(22, 27, 34));
    ::FillRect(mdc, &rc, bg); ::DeleteObject(bg);

    HPEN sp = ::CreatePen(PS_SOLID, 1, RGB(48, 54, 61));
    HGDIOBJ op = ::SelectObject(mdc, sp);
    ::MoveToEx(mdc, 0, h - 1, nullptr); ::LineTo(mdc, w, h - 1);
    ::SelectObject(mdc, op); ::DeleteObject(sp);

    ::SetBkMode(mdc, TRANSPARENT);
    HFONT fnt = ::CreateFontW(static_cast<int>(-11.0f * s), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ ofnt = ::SelectObject(mdc, fnt);

    auto DrawBtn = [&](const RECT& r, const wchar_t* txt, bool act, bool hov) {
        HBRUSH fbr = ::CreateSolidBrush(act ? RGB(30, 58, 138) : (hov ? RGB(33, 38, 45) : RGB(22, 27, 34)));
        HPEN pbr = ::CreatePen(PS_SOLID, 1, act ? RGB(59, 130, 246) : RGB(48, 54, 61));
        HGDIOBJ oP = ::SelectObject(mdc, pbr), oB = ::SelectObject(mdc, fbr);
        ::RoundRect(mdc, r.left, r.top, r.right, r.bottom, static_cast<int>(6 * s), static_cast<int>(6 * s));
        ::SelectObject(mdc, oP); ::SelectObject(mdc, oB);
        ::DeleteObject(fbr); ::DeleteObject(pbr);
        ::SetTextColor(mdc, act || hov ? RGB(240, 246, 252) : RGB(201, 209, 217));
        RECT tr = r; ::DrawTextW(mdc, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    const wchar_t* mtxt = muted ? ui::loc::Get(ui::loc::S::Output_Unmute) : ui::loc::Get(ui::loc::S::Output_Mute);
    DrawBtn(layout.mute_rc, mtxt, muted, hov_btn == 1);

    if (layout.has_slider) {
        int th = static_cast<int>(4 * s);
        RECT trk{ layout.sx0, layout.sy, layout.sx1, layout.sy + th };
        HBRUSH tbg = ::CreateSolidBrush(RGB(48, 54, 61)); ::FillRect(mdc, &trk, tbg); ::DeleteObject(tbg);

        int fx = layout.sx0 + static_cast<int>((layout.sx1 - layout.sx0) * vol);
        RECT frc{ layout.sx0, layout.sy, fx, layout.sy + th };
        HBRUSH fbr = ::CreateSolidBrush(RGB(59, 130, 246)); ::FillRect(mdc, &frc, fbr); ::DeleteObject(fbr);

        int tr = static_cast<int>(6 * s);
        HBRUSH thm = ::CreateSolidBrush(RGB(240, 246, 252));
        HGDIOBJ oB = ::SelectObject(mdc, thm);
        ::Ellipse(mdc, fx - tr, layout.sy + th / 2 - tr, fx + tr, layout.sy + th / 2 + tr);
        ::SelectObject(mdc, oB); ::DeleteObject(thm);

        std::wstring vs = std::format(L"{}%", static_cast<int>(std::round(vol * 100.0f)));
        ::SetTextColor(mdc, RGB(139, 148, 158));
        RECT prc = layout.prc;
        ::DrawTextW(mdc, vs.c_str(), -1, &prc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    DrawBtn(layout.fit_rc, ui::loc::Get(ui::loc::S::Output_Fit), false, hov_btn == 3);
    DrawBtn(layout.pin_rc, ui::loc::Get(ui::loc::S::Output_AlwaysOnTop), pin, hov_btn == 4);
    DrawBtn(layout.fs_rc, ui::loc::Get(ui::loc::S::Output_Fullscreen), fs, hov_btn == 5);

    ::BitBlt(hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
    ::SelectObject(mdc, ofnt); ::DeleteObject(fnt);
    ::SelectObject(mdc, obm); ::DeleteObject(mbm); ::DeleteDC(mdc);
    ::EndPaint(hwnd, &ps);
}

LRESULT OutputWindow::HandleToolbarMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    float s = GetDpiScale();
    RECT cl_rc{}; ::GetClientRect(hwnd, &cl_rc);
    int w = cl_rc.right - cl_rc.left;
    ToolbarLayout layout = ComputeToolbarLayout(w, s);

    auto HitBtn = [&](int px, int py) {
        POINT pt{ px, py };
        if (::PtInRect(&layout.mute_rc, pt)) return 1;
        if (layout.has_slider && ::PtInRect(&layout.s_hit, pt)) return 2;
        if (::PtInRect(&layout.fit_rc, pt)) return 3;
        if (::PtInRect(&layout.pin_rc, pt)) return 4;
        if (::PtInRect(&layout.fs_rc, pt)) return 5;
        return 0;
    };

    switch (msg) {
    case WM_PAINT:
        DrawOutputToolbar(hwnd, this, s, m_tb_hovered_btn, m_audio_muted, m_audio_volume,
                          m_always_on_top, m_fullscreen);
        return 0;

    case WM_MOUSEMOVE: {
        int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
        if (m_is_dragging_volume && layout.has_slider && layout.sx1 > layout.sx0) {
            float vol = std::clamp(static_cast<float>(px - layout.sx0) / static_cast<float>(layout.sx1 - layout.sx0), 0.0f, 1.0f);
            SetAudioState(m_audio_muted, vol);
            if (m_on_volume_changed) m_on_volume_changed(vol);
            return 0;
        }
        int hit = HitBtn(px, py);
        if (hit != m_tb_hovered_btn) {
            m_tb_hovered_btn = hit;
            ::InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            ::TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (m_tb_hovered_btn != 0) {
            m_tb_hovered_btn = 0;
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
        int hit = HitBtn(px, py);
        if (hit == 1) {
            bool next_mute = !m_audio_muted;
            SetAudioState(next_mute, m_audio_volume);
            if (m_on_toggle_mute) m_on_toggle_mute();
        } else if (hit == 2 && layout.has_slider && layout.sx1 > layout.sx0) {
            m_is_dragging_volume = true;
            ::SetCapture(hwnd);
            float vol = std::clamp(static_cast<float>(px - layout.sx0) / static_cast<float>(layout.sx1 - layout.sx0), 0.0f, 1.0f);
            SetAudioState(m_audio_muted, vol);
            if (m_on_volume_changed) m_on_volume_changed(vol);
        } else if (hit == 3) {
            ApplyComfortableFit();
            if (m_on_fit) m_on_fit();
        } else if (hit == 4) {
            ToggleAlwaysOnTop();
        } else if (hit == 5) {
            ToggleFullscreen();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (m_is_dragging_volume) {
            m_is_dragging_volume = false;
            ::ReleaseCapture();
        }
        return 0;
    case WM_SETCURSOR: {
        POINT pt{}; ::GetCursorPos(&pt); ::ScreenToClient(hwnd, &pt);
        if (HitBtn(pt.x, pt.y) != 0) {
            ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OutputWindow::HandleVideoChildMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_PAINT: {
        if (!m_has_frame) {
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd, &ps);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && m_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        if (wp == VK_F11 || (wp == VK_RETURN && (::GetKeyState(VK_MENU) < 0))) {
            ToggleFullscreen();
            return 0;
        }
        break;
    }

    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OutputWindow::HandlePlaceholderMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hwnd, &ps);
        RECT rc{}; ::GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        HDC mdc = ::CreateCompatibleDC(hdc);
        HBITMAP mbm = ::CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ obm = ::SelectObject(mdc, mbm);

        // Dark background matching theme (#0B1020)
        HBRUSH bg = ::CreateSolidBrush(RGB(11, 16, 32));
        ::FillRect(mdc, &rc, bg);
        ::DeleteObject(bg);

        float s = GetDpiScale();
        ::SetBkMode(mdc, TRANSPARENT);

        int cy = h / 2;

        // Title: DUWN MIRROR
        HFONT titleFont = ::CreateFontW(static_cast<int>(-18.0f * s), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldFont = ::SelectObject(mdc, titleFont);
        ::SetTextColor(mdc, RGB(243, 246, 255));
        RECT t_rc{ 16, cy - static_cast<int>(40 * s), w - 16, cy - static_cast<int>(12 * s) };
        ::DrawTextW(mdc, L"DUWN MIRROR", -1, &t_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Subtitle: Status / Waiting for video
        HFONT subFont = ::CreateFontW(static_cast<int>(-13.0f * s), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        ::SelectObject(mdc, subFont);
        ::SetTextColor(mdc, RGB(59, 130, 246)); // Accent blue
        const wchar_t* wait_str = ui::loc::Get(ui::loc::S::Output_WaitingForFrame);
        RECT s_rc{ 16, cy - static_cast<int>(8 * s), w - 16, cy + static_cast<int>(18 * s) };
        ::DrawTextW(mdc, wait_str, -1, &s_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Helper hint: Control center
        HFONT hintFont = ::CreateFontW(static_cast<int>(-11.0f * s), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        ::SelectObject(mdc, hintFont);
        ::SetTextColor(mdc, RGB(139, 148, 158));
        const wchar_t* hint_str = ui::loc::Get(ui::loc::S::Mirror_Empty_Wireless_Desc);
        RECT h_rc{ 20, cy + static_cast<int>(22 * s), w - 20, cy + static_cast<int>(60 * s) };
        ::DrawTextW(mdc, hint_str, -1, &h_rc, DT_CENTER | DT_WORDBREAK);

        ::BitBlt(hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
        ::SelectObject(mdc, oldFont);
        ::DeleteObject(titleFont);
        ::DeleteObject(subFont);
        ::DeleteObject(hintFont);
        ::SelectObject(mdc, obm); ::DeleteObject(mbm); ::DeleteDC(mdc);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && m_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        if (wp == VK_F11 || (wp == VK_RETURN && (::GetKeyState(VK_MENU) < 0))) {
            ToggleFullscreen();
            return 0;
        }
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace duwn::app

