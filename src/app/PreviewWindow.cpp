#include "PreviewWindow.h"
#include "AppIcon.h"
#include "common/logging/Logger.h"
#include "video/VideoGeometry.h"
#include <windowsx.h>
#include <algorithm>
#include <cassert>

namespace duwn::app {

bool PreviewWindow::s_class_registered = false;

PreviewWindow::~PreviewWindow() {
    if (m_hwnd) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    s_instance_count.fetch_sub(1, std::memory_order_relaxed);
}

bool PreviewWindow::Create(uint32_t initial_w, uint32_t initial_h,
                           ResizeCallback on_resize) noexcept {
    const uint32_t current_instances = s_instance_count.fetch_add(1, std::memory_order_relaxed) + 1;
    assert(current_instances <= 1 && "TopLevelPreviewWindows must be <= 1");
    if (current_instances > 1) {
        DUWN_LOG_ERROR("PreviewWindow", "Attempted to create duplicate PreviewWindow instance!");
    }

    m_on_resize = std::move(on_resize);

    if (!s_class_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = ::GetModuleHandleW(nullptr);
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = LoadAppIcon(nullptr, false);
        wc.hIconSm       = LoadAppIcon(nullptr, true);
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kClassName;

        if (!::RegisterClassExW(&wc)) {
            DUWN_LOG_ERROR("PreviewWindow", "RegisterClassExW failed");
            return false;
        }
        s_class_registered = true;
    }

    constexpr DWORD style    = WS_OVERLAPPEDWINDOW;
    constexpr DWORD ex_style = WS_EX_APPWINDOW;

    int win_x = CW_USEDEFAULT;
    int win_y = CW_USEDEFAULT;
    int win_w = CW_USEDEFAULT;
    int win_h = CW_USEDEFAULT;

    if (initial_w > 0 && initial_h > 0) {
        CalculateComfortableInitialRect(nullptr, initial_w, initial_h, win_x, win_y, win_w, win_h);
    }

    m_hwnd = ::CreateWindowExW(
        ex_style,
        kClassName,
        kWindowTitle,
        style,
        win_x, win_y,
        win_w, win_h,
        nullptr, nullptr,
        ::GetModuleHandleW(nullptr),
        this
    );

    if (!m_hwnd) {
        DUWN_LOG_ERROR("PreviewWindow", "CreateWindowExW failed");
        return false;
    }

    RECT client_rc{};
    ::GetClientRect(m_hwnd, &client_rc);
    m_client_w.store(static_cast<uint32_t>(client_rc.right - client_rc.left), std::memory_order_relaxed);
    m_client_h.store(static_cast<uint32_t>(client_rc.bottom - client_rc.top), std::memory_order_relaxed);

    SetAppWindowIcons(m_hwnd);

    DUWN_LOG_INFOF("PreviewWindow", "Created HWND={:p} initial client={}x{}",
        static_cast<void*>(m_hwnd), m_client_w.load(), m_client_h.load());
    return true;
}

void PreviewWindow::Show() noexcept {
    if (m_hwnd) {
        ::ShowWindow(m_hwnd, SW_SHOW);
    }
}

void PreviewWindow::ShowNoActivate() noexcept {
    if (m_hwnd) {
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
}

void PreviewWindow::Hide() noexcept {
    if (m_hwnd) {
        ::ShowWindow(m_hwnd, SW_HIDE);
    }
}

void PreviewWindow::ToggleVisibility() noexcept {
    if (IsVisible()) {
        Hide();
    } else {
        Show();
    }
}

bool PreviewWindow::IsVisible() const noexcept {
    return m_hwnd && ::IsWindowVisible(m_hwnd);
}

void PreviewWindow::SetAlwaysOnTop(bool top) noexcept {
    m_always_on_top = top;
    if (m_hwnd) {
        ::SetWindowPos(m_hwnd,
            top ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void PreviewWindow::ToggleAlwaysOnTop() noexcept {
    SetAlwaysOnTop(!m_always_on_top);
}

void PreviewWindow::ToggleFullscreen() noexcept {
    if (!m_hwnd) return;

    if (!m_fullscreen) {
        // Enter borderless fullscreen on current monitor
        ::GetWindowRect(m_hwnd, &m_restore_rect);

        HMONITOR hmon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (::GetMonitorInfoW(hmon, &mi)) {
            DWORD style = ::GetWindowLongW(m_hwnd, GWL_STYLE);
            style &= ~WS_OVERLAPPEDWINDOW;
            style |= WS_POPUP;
            ::SetWindowLongW(m_hwnd, GWL_STYLE, style);

            ::SetWindowPos(m_hwnd, HWND_TOP,
                mi.rcMonitor.left,
                mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);
            m_fullscreen = true;
        }
    } else {
        // Restore windowed mode
        DWORD style = ::GetWindowLongW(m_hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_OVERLAPPEDWINDOW;
        ::SetWindowLongW(m_hwnd, GWL_STYLE, style);

        ::SetWindowPos(m_hwnd,
            m_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
            m_restore_rect.left,
            m_restore_rect.top,
            m_restore_rect.right - m_restore_rect.left,
            m_restore_rect.bottom - m_restore_rect.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
        m_fullscreen = false;
    }
}

void PreviewWindow::SetVideoGeometry(uint32_t video_w, uint32_t video_h) noexcept {
    if (video_w == 0 || video_h == 0) return;
    m_video_w.store(video_w, std::memory_order_relaxed);
    m_video_h.store(video_h, std::memory_order_relaxed);
}

void PreviewWindow::ApplyComfortableSize(uint32_t video_w, uint32_t video_h) noexcept {
    if (!m_hwnd || video_w == 0 || video_h == 0) return;
    if (m_fullscreen) return;

    SetVideoGeometry(video_w, video_h);

    int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
    CalculateComfortableInitialRect(m_hwnd, video_w, video_h, win_x, win_y, win_w, win_h);

    ::SetWindowPos(m_hwnd, nullptr, win_x, win_y, win_w, win_h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

void PreviewWindow::OnStreamGeometryChanged(uint32_t new_src_w, uint32_t new_src_h) noexcept {
    if (!m_hwnd || new_src_w == 0 || new_src_h == 0) return;
    if (m_fullscreen) {
        // In fullscreen: keep window size, let renderer letterbox/pillarbox
        SetVideoGeometry(new_src_w, new_src_h);
        return;
    }

    HMONITOR hmon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(hmon, &mi)) {
        mi.rcWork = {0, 0, 1920, 1080};
    }
    const uint32_t work_w = static_cast<uint32_t>(mi.rcWork.right - mi.rcWork.left);
    const uint32_t work_h = static_cast<uint32_t>(mi.rcWork.bottom - mi.rcWork.top);

    video::PreviewDimensions dims;
    if (m_sizing_mode == PreviewSizingMode::AutoSized) {
        dims = video::ComputeComfortablePreviewDimensions(new_src_w, new_src_h, work_w, work_h);
    } else {
        const uint32_t cur_w = m_client_w.load(std::memory_order_relaxed);
        const uint32_t cur_h = m_client_h.load(std::memory_order_relaxed);
        dims = video::ComputeAreaPreservingPreviewDimensions(cur_w, cur_h, new_src_w, new_src_h, work_w, work_h);
    }

    RECT wr{0, 0, static_cast<LONG>(dims.width), static_cast<LONG>(dims.height)};
    ::AdjustWindowRectEx(&wr, ::GetWindowLongW(m_hwnd, GWL_STYLE), FALSE, ::GetWindowLongW(m_hwnd, GWL_EXSTYLE));

    const int target_w = wr.right - wr.left;
    const int target_h = wr.bottom - wr.top;

    RECT cur_wr{};
    ::GetWindowRect(m_hwnd, &cur_wr);
    const int center_x = cur_wr.left + (cur_wr.right - cur_wr.left) / 2;
    const int center_y = cur_wr.top + (cur_wr.bottom - cur_wr.top) / 2;

    int new_x = center_x - target_w / 2;
    int new_y = center_y - target_h / 2;

    // Clamp within work area bounds
    if (new_x < mi.rcWork.left) new_x = mi.rcWork.left;
    if (new_y < mi.rcWork.top)  new_y = mi.rcWork.top;
    if (new_x + target_w > mi.rcWork.right) new_x = std::max<int>(mi.rcWork.left, mi.rcWork.right - target_w);
    if (new_y + target_h > mi.rcWork.bottom) new_y = std::max<int>(mi.rcWork.top, mi.rcWork.bottom - target_h);

    ::SetWindowPos(m_hwnd, nullptr,
        new_x, new_y, target_w, target_h,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SetVideoGeometry(new_src_w, new_src_h);
}

void PreviewWindow::CalculateComfortableInitialRect(HWND target_hwnd,
                                                  uint32_t video_w, uint32_t video_h,
                                                  int& out_x, int& out_y,
                                                  int& out_w, int& out_h) noexcept {
    if (video_w == 0 || video_h == 0) {
        video_w = 1184;
        video_h = 2560;
    }

    HMONITOR hMon = ::MonitorFromWindow(target_hwnd ? target_hwnd : ::GetDesktopWindow(),
                                        MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(hMon, &mi)) {
        mi.rcWork = {0, 0, 1920, 1080};
    }

    const int work_w = mi.rcWork.right - mi.rcWork.left;
    const int work_h = mi.rcWork.bottom - mi.rcWork.top;

    const bool is_portrait = (video_h >= video_w);
    float client_w = 0.0f;
    float client_h = 0.0f;

    if (is_portrait) {
        // Portrait target: ~60% work area height (clamped 55%-65%, max 70%)
        client_h = static_cast<float>(work_h) * 0.60f;
        client_w = client_h * (static_cast<float>(video_w) / static_cast<float>(video_h));

        // Clamp width if it exceeds 70% of work area width
        if (client_w > static_cast<float>(work_w) * 0.70f) {
            client_w = static_cast<float>(work_w) * 0.70f;
            client_h = client_w * (static_cast<float>(video_h) / static_cast<float>(video_w));
        }
    } else {
        // Landscape target: ~50% work area width (clamped 45%-55%, max 70%)
        client_w = static_cast<float>(work_w) * 0.50f;
        client_h = client_w * (static_cast<float>(video_h) / static_cast<float>(video_w));

        // Clamp height if it exceeds 70% of work area height
        if (client_h > static_cast<float>(work_h) * 0.70f) {
            client_h = static_cast<float>(work_h) * 0.70f;
            client_w = client_h * (static_cast<float>(video_w) / static_cast<float>(video_h));
        }
    }

    RECT rc{0, 0, static_cast<LONG>(client_w + 0.5f), static_cast<LONG>(client_h + 0.5f)};
    ::AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);

    out_w = rc.right - rc.left;
    out_h = rc.bottom - rc.top;

    // Center in monitor work area
    out_x = mi.rcWork.left + (work_w - out_w) / 2;
    out_y = mi.rcWork.top + (work_h - out_h) / 2;
}

void PreviewWindow::GetWindowRect(int& x, int& y, int& w, int& h) const noexcept {
    if (!m_hwnd) {
        x = y = w = h = 0;
        return;
    }
    RECT rc{};
    ::GetWindowRect(m_hwnd, &rc);
    x = rc.left;
    y = rc.top;
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
}

void PreviewWindow::SetWindowRect(int x, int y, int w, int h) noexcept {
    if (!m_hwnd || w <= 0 || h <= 0) return;
    m_has_custom_placement = true;

    // Multi-monitor sanity check: verify coordinates intersect a valid monitor work area.
    // If a secondary monitor was disconnected, clamp to primary monitor work area.
    RECT req_rc{x, y, x + w, y + h};
    HMONITOR hMon = ::MonitorFromRect(&req_rc, MONITOR_DEFAULTTONULL);
    if (!hMon) {
        hMon = ::MonitorFromWindow(::GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        if (::GetMonitorInfoW(hMon, &mi)) {
            const int work_w = mi.rcWork.right - mi.rcWork.left;
            const int work_h = mi.rcWork.bottom - mi.rcWork.top;
            if (w > work_w) w = work_w;
            if (h > work_h) h = work_h;
            x = mi.rcWork.left + (work_w - w) / 2;
            y = mi.rcWork.top + (work_h - h) / 2;
        }
    } else {
        MONITORINFO mi{sizeof(mi)};
        if (::GetMonitorInfoW(hMon, &mi)) {
            // Ensure title bar remains accessible inside work area
            if (x < mi.rcWork.left) x = mi.rcWork.left;
            if (y < mi.rcWork.top)  y = mi.rcWork.top;
            if (x + 100 > mi.rcWork.right)  x = std::max<int>(mi.rcWork.left, mi.rcWork.right - w);
            if (y + 60 > mi.rcWork.bottom)  y = std::max<int>(mi.rcWork.top, mi.rcWork.bottom - h);
        }
    }

    ::SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK PreviewWindow::WndProc(HWND hwnd, UINT msg,
                                       WPARAM wparam, LPARAM lparam) noexcept {
    PreviewWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<PreviewWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<PreviewWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wparam, lparam);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT PreviewWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    switch (msg) {
    case WM_SIZING: {
        auto* prc = reinterpret_cast<RECT*>(lparam);
        if (prc && hwnd && !m_fullscreen && !::IsZoomed(hwnd)) {
            uint32_t vw = m_video_w.load(std::memory_order_relaxed);
            uint32_t vh = m_video_h.load(std::memory_order_relaxed);
            if (vw == 0 || vh == 0) {
                vw = 1184;
                vh = 2560;
            }
            const double target_ar = static_cast<double>(vw) / static_cast<double>(vh);

            // Compute actual non-client border size
            RECT win_rc{}, client_rc{};
            ::GetWindowRect(hwnd, &win_rc);
            ::GetClientRect(hwnd, &client_rc);
            int border_w = (win_rc.right - win_rc.left) - (client_rc.right - client_rc.left);
            int border_h = (win_rc.bottom - win_rc.top) - (client_rc.bottom - client_rc.top);
            if (border_w <= 0 || border_h <= 0) {
                const DWORD style = static_cast<DWORD>(::GetWindowLongW(hwnd, GWL_STYLE));
                const DWORD ex_style = static_cast<DWORD>(::GetWindowLongW(hwnd, GWL_EXSTYLE));
                RECT border_test{0, 0, 100, 100};
                ::AdjustWindowRectEx(&border_test, style, FALSE, ex_style);
                border_w = (border_test.right - border_test.left) - 100;
                border_h = (border_test.bottom - border_test.top) - 100;
            }

            const int cur_w = prc->right - prc->left;
            const int cur_h = prc->bottom - prc->top;
            int client_w = std::max(100, cur_w - border_w);
            int client_h = std::max(100, cur_h - border_h);

            switch (wparam) {
            case WMSZ_LEFT:
            case WMSZ_RIGHT:
                client_h = static_cast<int>(std::lround(client_w / target_ar));
                prc->bottom = prc->top + client_h + border_h;
                break;
            case WMSZ_TOP:
            case WMSZ_BOTTOM:
                client_w = static_cast<int>(std::lround(client_h * target_ar));
                prc->right = prc->left + client_w + border_w;
                break;
            case WMSZ_TOPLEFT: {
                if (static_cast<double>(client_w) / target_ar > client_h) {
                    client_h = static_cast<int>(std::lround(client_w / target_ar));
                } else {
                    client_w = static_cast<int>(std::lround(client_h * target_ar));
                }
                prc->left = prc->right - (client_w + border_w);
                prc->top = prc->bottom - (client_h + border_h);
                break;
            }
            case WMSZ_TOPRIGHT: {
                if (static_cast<double>(client_w) / target_ar > client_h) {
                    client_h = static_cast<int>(std::lround(client_w / target_ar));
                } else {
                    client_w = static_cast<int>(std::lround(client_h * target_ar));
                }
                prc->right = prc->left + (client_w + border_w);
                prc->top = prc->bottom - (client_h + border_h);
                break;
            }
            case WMSZ_BOTTOMLEFT: {
                if (static_cast<double>(client_w) / target_ar > client_h) {
                    client_h = static_cast<int>(std::lround(client_w / target_ar));
                } else {
                    client_w = static_cast<int>(std::lround(client_h * target_ar));
                }
                prc->left = prc->right - (client_w + border_w);
                prc->bottom = prc->top + (client_h + border_h);
                break;
            }
            case WMSZ_BOTTOMRIGHT:
            default: {
                if (static_cast<double>(client_w) / target_ar > client_h) {
                    client_h = static_cast<int>(std::lround(client_w / target_ar));
                } else {
                    client_w = static_cast<int>(std::lround(client_h * target_ar));
                }
                prc->right = prc->left + (client_w + border_w);
                prc->bottom = prc->top + (client_h + border_h);
                break;
            }
            }
            return TRUE;
        }
        break;
    }

    case WM_ENTERSIZEMOVE:
        m_in_sizemove = true;
        return 0;

    case WM_EXITSIZEMOVE:
        m_in_sizemove = false;
        m_sizing_mode = PreviewSizingMode::UserSized; // Lock user sizing preference on manual drag/resize
        return 0;

    case WM_CLOSE:
        // Hide window on user close; keep HWND alive for process lifetime
        Hide();
        if (m_on_visibility) {
            m_on_visibility(false);
        }
        return 0;

    case WM_SHOWWINDOW:
        if (m_on_visibility) {
            m_on_visibility(wparam != 0);
        }
        break;

    case WM_SIZE: {
        if (wparam != SIZE_MINIMIZED) {
            const uint32_t w = LOWORD(lparam);
            const uint32_t h = HIWORD(lparam);
            m_client_w.store(w, std::memory_order_relaxed);
            m_client_h.store(h, std::memory_order_relaxed);
            if (m_on_resize && w > 0 && h > 0) {
                m_on_resize(w, h);
            }
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
        mmi->ptMinTrackSize.x = 320;
        mmi->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_KEYDOWN: {
        if (wparam == VK_F11) {
            ToggleFullscreen();
            return 0;
        }
        if (wparam == VK_RETURN && (::GetKeyState(VK_MENU) & 0x8000)) {
            ToggleFullscreen();
            return 0;
        }
        if (wparam == VK_ESCAPE && m_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent background erase flicker
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace duwn::app
