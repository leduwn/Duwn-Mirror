#include "OutputWindow.h"

#include "AppIcon.h"

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



    // Register window class once per process

    if (!s_class_registered) {

        WNDCLASSEXW wc{};

        wc.cbSize        = sizeof(wc);

        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

        wc.lpfnWndProc   = WndProc;

        wc.hInstance     = ::GetModuleHandleW(nullptr);

        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);

        wc.hIcon         = LoadAppIcon(nullptr, false);

        wc.hIconSm       = LoadAppIcon(nullptr, true);

        // Black background: letterbox bars are cleared by renderer, but

        // Win32 paints this color on WM_ERASEBKGND — avoids white flash.

        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));

        wc.lpszClassName = kClassName;



        if (!::RegisterClassExW(&wc)) {

            DUWN_LOG_ERROR("OutputWindow", "RegisterClassExW failed");

            return false;

        }

        s_class_registered = true;

    }



    // WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS = borderless dedicated capture window.

    // Client area strictly matches Resolved Output Geometry (client == window size).

    // Zero non-client chrome: no title bar, no borders, no caption, no sysmenu.

    // WS_EX_APPWINDOW: visible in OBS / TikTok Live Studio window capture pickers.

    // Allows vertical/horizontal desktop overflow for oversize portrait (e.g. 1184x2560 on 1080p).

    constexpr DWORD style    = CaptureWindowStyle();

    constexpr DWORD ex_style = CaptureWindowExStyle();



    m_hwnd = ::CreateWindowExW(

        ex_style,

        kClassName,

        L"Duwn Mirror Output",

        style,

        CW_USEDEFAULT, CW_USEDEFAULT,

        static_cast<int>(width), static_cast<int>(height),

        nullptr, nullptr,

        ::GetModuleHandleW(nullptr),

        this  // passed to WM_NCCREATE via CREATESTRUCT::lpCreateParams

    );



    if (!m_hwnd) {

        DUWN_LOG_ERROR("OutputWindow", "CreateWindowExW failed");

        return false;

    }



    m_client_w.store(width, std::memory_order_relaxed);

    m_client_h.store(height, std::memory_order_relaxed);

    m_canvas_w.store(width, std::memory_order_relaxed);

    m_canvas_h.store(height, std::memory_order_relaxed);



    SetAppWindowIcons(m_hwnd);



    LONG_PTR runtime_style = ::GetWindowLongPtrW(m_hwnd, GWL_STYLE);

    LONG_PTR runtime_ex_style = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

    DUWN_LOG_INFOF("OutputWindow", "Created {}x{} HWND={:p} GWL_STYLE={:#010x} (WS_POPUP={}, WS_CAPTION={}, WS_THICKFRAME={}, WS_MAXIMIZEBOX={}) GWL_EXSTYLE={:#010x} (WS_EX_APPWINDOW={})",

        width, height, static_cast<void*>(m_hwnd),

        static_cast<unsigned long long>(runtime_style),

        (runtime_style & WS_POPUP) != 0,

        (runtime_style & WS_CAPTION) != 0,

        (runtime_style & WS_THICKFRAME) != 0,

        (runtime_style & WS_MAXIMIZEBOX) != 0,

        static_cast<unsigned long long>(runtime_ex_style),

        (runtime_ex_style & WS_EX_APPWINDOW) != 0);

    return true;

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
        if (::IsIconic(m_hwnd)) {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        } else {
            ::ShowWindow(m_hwnd, SW_SHOW);
        }
        EnsureAccessiblePlacement();
    }
}

void OutputWindow::ShowNoActivate() noexcept {
    if (m_hwnd) {
        if (::IsIconic(m_hwnd)) {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        } else {
            ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        EnsureAccessiblePlacement();
    }
}



void OutputWindow::Hide() noexcept {

    if (m_hwnd) ::ShowWindow(m_hwnd, SW_HIDE);

}



void OutputWindow::SetBackgroundCaptureMode(bool enabled) noexcept {

    if (!m_hwnd) return;

    m_background_capture = enabled;

    if (enabled) {

        ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);

        ::SetLayeredWindowAttributes(m_hwnd, 0, 1, LWA_ALPHA);

    } else {

        ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

    }

}



void OutputWindow::SetAspectRatio(uint32_t width, uint32_t height) noexcept {

    if (width == 0 || height == 0) return;

    uint32_t prev_w = m_aspect_w.exchange(width, std::memory_order_relaxed);

    uint32_t prev_h = m_aspect_h.exchange(height, std::memory_order_relaxed);



    if (prev_w != width || prev_h != height) {

        if (m_hwnd) {

            ::PostMessageW(m_hwnd, WM_APP_UPDATE_ASPECT, width, height);

        }

    }

}



void OutputWindow::SetCanvasSize(uint32_t width, uint32_t height) noexcept {

    if (width > 0 && height > 0) {

        m_canvas_w.store(width, std::memory_order_relaxed);

        m_canvas_h.store(height, std::memory_order_relaxed);

        if (m_hwnd)

            ::PostMessageW(m_hwnd, WM_APP_SET_CANVAS, width, height);

    }

}



void OutputWindow::SetResolvedOutputSize(uint32_t width, uint32_t height) noexcept {

    if (width == 0 || height == 0 || !m_hwnd) return;

    m_canvas_w.store(width, std::memory_order_relaxed);

    m_canvas_h.store(height, std::memory_order_relaxed);

    m_client_w.store(width, std::memory_order_relaxed);

    m_client_h.store(height, std::memory_order_relaxed);



    if (!m_fullscreen) {

        ::SetWindowPos(m_hwnd, nullptr, 0, 0,

            static_cast<int>(width), static_cast<int>(height),

            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    }

}



void OutputWindow::ResetToCanvasSize() noexcept {

    uint32_t cw = m_canvas_w.load(std::memory_order_relaxed);

    uint32_t ch = m_canvas_h.load(std::memory_order_relaxed);

    if (m_hwnd && !m_fullscreen && cw > 0 && ch > 0) {

        ::SetWindowPos(m_hwnd, nullptr, 0, 0,

            static_cast<int>(cw), static_cast<int>(ch),

            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    }

}



void OutputWindow::ApplyAspectAdjustment(uint32_t aspect_w, uint32_t aspect_h) noexcept {

    if (!m_hwnd || m_fullscreen || ::IsZoomed(m_hwnd)) return;

    if (aspect_w == 0 || aspect_h == 0) return;



    RECT wr{};

    if (!::GetWindowRect(m_hwnd, &wr)) return;



    LONG cur_client_w = wr.right - wr.left;

    LONG cur_client_h = wr.bottom - wr.top;

    if (cur_client_w <= 0 || cur_client_h <= 0) return;



    double target_ar = static_cast<double>(aspect_w) / static_cast<double>(aspect_h);



    LONG new_client_h = cur_client_h;

    LONG new_client_w = static_cast<LONG>(std::round(new_client_h * target_ar));



    LONG center_x = wr.left + (wr.right - wr.left) / 2;

    LONG center_y = wr.top + (wr.bottom - wr.top) / 2;

    LONG new_x = center_x - new_client_w / 2;

    LONG new_y = center_y - new_client_h / 2;



    ::SetWindowPos(m_hwnd, nullptr,

        new_x, new_y, new_client_w, new_client_h,

        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);



    DUWN_LOG_INFOF("OutputWindow", "Aspect ratio adjusted to {}:{} (dims: {}x{})",

        aspect_w, aspect_h, new_client_w, new_client_h);

}



void OutputWindow::ToggleFullscreen() noexcept {

    if (!m_hwnd) return;



    if (!m_fullscreen) {

        // Save current window rect for restore

        ::GetWindowRect(m_hwnd, &m_restore_rect);



        // Find the monitor the window is on

        HMONITOR monitor = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);

        MONITORINFO mi{sizeof(mi)};

        ::GetMonitorInfoW(monitor, &mi);

        const RECT& r = mi.rcMonitor;



        // Borderless fullscreen over full monitor

        ::SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

        ::SetWindowPos(m_hwnd, HWND_TOP,

            r.left, r.top, r.right - r.left, r.bottom - r.top,

            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_fullscreen = true;

    } else {

        constexpr DWORD style = CaptureWindowStyle();

        ::SetWindowLongW(m_hwnd, GWL_STYLE, style);

        ::SetWindowPos(m_hwnd, nullptr,

            m_restore_rect.left, m_restore_rect.top,

            m_restore_rect.right  - m_restore_rect.left,

            m_restore_rect.bottom - m_restore_rect.top,

            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOZORDER);

        m_fullscreen = false;

    }

}



void OutputWindow::ToggleAlwaysOnTop() noexcept {

    if (!m_hwnd) return;

    m_always_on_top = !m_always_on_top;

    ::SetWindowPos(m_hwnd, m_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,

                   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

}



LRESULT CALLBACK OutputWindow::WndProc(HWND hwnd, UINT msg,

                                        WPARAM wparam, LPARAM lparam) noexcept {

    OutputWindow* self = nullptr;



    if (msg == WM_NCCREATE) {

        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);

        self = static_cast<OutputWindow*>(cs->lpCreateParams);

        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        self->m_hwnd = hwnd;

    } else {

        self = reinterpret_cast<OutputWindow*>(

            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    }



    if (self) return self->HandleMessage(hwnd, msg, wparam, lparam);

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);

}



LRESULT OutputWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {

    switch (msg) {



    case WM_APP_UPDATE_ASPECT:

        ApplyAspectAdjustment(static_cast<uint32_t>(wparam), static_cast<uint32_t>(lparam));

        return 0;



    case WM_APP_SET_CANVAS:

        if (wparam > 0 && lparam > 0) {

            m_canvas_w.store(static_cast<uint32_t>(wparam), std::memory_order_relaxed);

            m_canvas_h.store(static_cast<uint32_t>(lparam), std::memory_order_relaxed);

            if (!m_fullscreen) {

                ::SetWindowPos(hwnd, nullptr, 0, 0,

                    static_cast<int>(wparam), static_cast<int>(lparam),

                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            }

        }

        return 0;



    case WM_LBUTTONDOWN: {

        if (!m_fullscreen) {

            // Allow moving window by dragging client area

            ::ReleaseCapture();

            ::SendMessageW(hwnd, WM_SYSCOMMAND, 0xF012 /* SC_MOVE | 0x2 */, 0);

            return 0;

        }

        break;

    }



    case WM_SIZING:

        // Deny user-driven manual resizing; output size strictly controlled by Resolved Output Geometry

        return FALSE;



    case WM_GETMINMAXINFO: {

        auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);

        mmi->ptMinTrackSize.x = 2;

        mmi->ptMinTrackSize.y = 2;

        mmi->ptMaxTrackSize.x = 32767;

        mmi->ptMaxTrackSize.y = 32767;

        mmi->ptMaxSize.x      = 32767;

        mmi->ptMaxSize.y      = 32767;

        return 0;

    }



    case WM_SIZE: {

        // SIZE_MINIMIZED sends lparam=0 — guard against 0x0 resize.

        uint32_t w = LOWORD(lparam);

        uint32_t h = HIWORD(lparam);

        if (w > 0 && h > 0) {

            m_client_w.store(w, std::memory_order_relaxed);

            m_client_h.store(h, std::memory_order_relaxed);

            if (m_on_resize) {

                // m_on_resize stores pending dims in VideoRenderer atomically.

                // The render thread applies the actual ResizeBuffers.

                m_on_resize(w, h);

            }

        }

        return 0;

    }



    case WM_ERASEBKGND:

        // Renderer clears to black — suppress Win32 background erase

        return 1;



    case WM_KEYDOWN:

        // F11 = toggle fullscreen

        if (wparam == VK_F11) {

            ToggleFullscreen();

            return 0;

        }

        break;



    case WM_CLOSE:

        // Intercept close: hide window instead of destroying HWND.

        // Capturing software (OBS/TikTok) retains source binding.

        Hide();

        if (m_on_visibility) {

            m_on_visibility(false);

        }

        return 0;



    case WM_DESTROY:

        m_hwnd = nullptr;

        return 0;

    }



    if (m_hwnd) {

        return ::DefWindowProcW(hwnd, msg, wparam, lparam);

    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);

}



} // namespace duwn::app

