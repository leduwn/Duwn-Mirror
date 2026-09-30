#pragma once
// OutputWindow — borderless, black, dedicated video output window.
//
// Separate from MainWindow (Control Window) so OBS / TikTok Live Studio can
// capture this surface cleanly with no title bar, toolbar, or overlay.
//
// Lifetime: created by App, shown/hidden via Show()/Hide().
// WM_SIZE forwards to the VideoRenderer so the swap chain stays in sync.
// WM_CLOSE is suppressed — App controls lifetime.

#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <atomic>

namespace duwn::app {

// Called when the output window client area changes size.
using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
using VisibilityCallback = std::function<void(bool visible)>;

class OutputWindow {
public:
    OutputWindow() noexcept = default;
    ~OutputWindow();

    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;

    // Create the window. width/height = initial content area.
    // on_resize is called from WM_SIZE with new client dimensions.
    bool Create(uint32_t width, uint32_t height,
                ResizeCallback on_resize = {}) noexcept;

    void Show() noexcept;
    void ShowNoActivate() noexcept;
    void Hide() noexcept;

    void SetOnVisibilityChanged(VisibilityCallback cb) noexcept {
        m_on_visibility = std::move(cb);
    }
    static uint32_t InstanceCount() noexcept {
        return s_instance_count.load(std::memory_order_relaxed);
    }

    // Background / transparent capture mode for single monitor capture without desktop obstruction
    void SetBackgroundCaptureMode(bool enabled) noexcept;
    bool IsBackgroundCaptureMode() const noexcept { return m_background_capture; }

    // Toggle borderless fullscreen on the monitor the window is on.
    void ToggleFullscreen() noexcept;

    bool IsFullscreen()   const noexcept { return m_fullscreen; }
    bool IsAspectLocked() const noexcept { return m_aspect_locked; }
    bool IsAlwaysOnTop()  const noexcept { return m_always_on_top; }
    void ToggleAspectLock() noexcept { m_aspect_locked = !m_aspect_locked; }
    void ToggleAlwaysOnTop() noexcept;

    // Dynamically update the target aspect ratio and resize window to match.
    // Safe to call from any thread.
    void SetAspectRatio(uint32_t width, uint32_t height) noexcept;
    void SetCanvasSize(uint32_t width, uint32_t height) noexcept;
    void SetResolvedOutputSize(uint32_t width, uint32_t height) noexcept;
    void ResetToCanvasSize() noexcept;

    // Window style queries (WS_POPUP pure video capture surface)
    static constexpr DWORD CaptureWindowStyle() noexcept {
        return WS_POPUP | WS_CLIPSIBLINGS;
    }
    static constexpr DWORD CaptureWindowExStyle() noexcept {
        return WS_EX_APPWINDOW;
    }

    uint32_t AspectWidth()  const noexcept { return m_aspect_w.load(std::memory_order_relaxed); }
    uint32_t AspectHeight() const noexcept { return m_aspect_h.load(std::memory_order_relaxed); }
    uint32_t ClientWidth()  const noexcept { return m_client_w.load(std::memory_order_relaxed); }
    uint32_t ClientHeight() const noexcept { return m_client_h.load(std::memory_order_relaxed); }
    uint32_t CanvasWidth()  const noexcept { return m_canvas_w.load(std::memory_order_relaxed); }
    uint32_t CanvasHeight() const noexcept { return m_canvas_h.load(std::memory_order_relaxed); }

    HWND Hwnd() const noexcept { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wparam, LPARAM lparam) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;
    void ApplyAspectAdjustment(uint32_t aspect_w, uint32_t aspect_h) noexcept;

    static constexpr wchar_t kClassName[] = L"DUWNMirrorOutputWindow";
    static constexpr UINT    WM_APP_UPDATE_ASPECT = WM_APP + 2;
    static constexpr UINT    WM_APP_SET_CANVAS = WM_APP + 3;
    static bool s_class_registered;
    static inline std::atomic<uint32_t> s_instance_count{0};

    HWND                  m_hwnd{nullptr};
    ResizeCallback        m_on_resize;
    VisibilityCallback    m_on_visibility;
    bool                  m_fullscreen{false};
    bool                  m_aspect_locked{true};
    bool                  m_always_on_top{false};
    bool                  m_background_capture{false};
    RECT                  m_restore_rect{};   // window rect before going fullscreen
    std::atomic<uint32_t> m_aspect_w{16};
    std::atomic<uint32_t> m_aspect_h{9};
    std::atomic<uint32_t> m_client_w{1280};
    std::atomic<uint32_t> m_client_h{720};
    std::atomic<uint32_t> m_canvas_w{1920};
    std::atomic<uint32_t> m_canvas_h{1080};
};

} // namespace duwn::app
