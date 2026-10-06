#pragma once
// OutputWindow.h — Modern dedicated video output window for DUWN Mirror.
//
// Standalone user-facing output window conforming to approved Workspace specifications:
// - Automatic show on device connection; hidden on startup and disconnect.
// - Compact titlebar; toolbar at the top (above and outside video area).
// - Toolbar contains mute/volume slider, fit to frame, always-on-top, and fullscreen.
// - Toolbar controlled by persistent toggle, no auto-hide on mouse movement.
// - Local state "Đang chờ hình ảnh…" when waiting for frames.
// - Aspect-ratio locked sizing without cropping or distortion.
// - Double-click on video or toolbar button toggles fullscreen; Esc exits fullscreen.
// - User close ('X') only hides window, setting session-hidden flag without stopping session, audio, or VirtualCam.

#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <atomic>

namespace duwn::app {

struct Settings;

using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
using VisibilityCallback = std::function<void(bool visible)>;
using ActionCallback = std::function<void()>;
using VolumeCallback = std::function<void(float)>;
using GeometryChangedCallback = std::function<void(int x, int y, int w, int h, float desired_long_edge_dip, bool custom_size)>;

class OutputWindow {
public:
    OutputWindow() noexcept = default;
    ~OutputWindow();

    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;

    bool Create(uint32_t initial_w, uint32_t initial_h,
                ResizeCallback on_resize = {}) noexcept;

    void Show() noexcept;
    void ShowNoActivate() noexcept;
    void Hide() noexcept;
    void ToggleVisibility() noexcept;

    bool IsVisible() const noexcept;
    bool IsFullscreen() const noexcept { return m_fullscreen; }
    bool IsAlwaysOnTop() const noexcept { return m_always_on_top; }
    bool IsAspectLocked() const noexcept { return true; }
    bool IsToolbarVisible() const noexcept { return m_show_toolbar; }
    bool HasFrame() const noexcept { return m_has_frame; }
    bool IsUserHiddenForSession() const noexcept { return m_user_hidden_for_session; }
    bool HasCustomSize() const noexcept { return m_user_has_custom_size; }
    float DesiredLongEdgeDip() const noexcept { return m_desired_long_edge_dip; }

    void SetAlwaysOnTop(bool top) noexcept;
    void ToggleAlwaysOnTop() noexcept;
    void ToggleFullscreen() noexcept;
    void ToggleAspectLock() noexcept {} // Aspect ratio is strictly locked to source geometry
    void SetToolbarVisible(bool visible) noexcept;
    void SetHasFrame(bool has) noexcept;
    void SetUserHiddenForSession(bool hidden) noexcept { m_user_hidden_for_session = hidden; }

    void SetVideoGeometry(uint32_t video_w, uint32_t video_h) noexcept;
    void ApplyComfortableFit() noexcept;
    void ApplyGeometryToMatchSource(uint32_t src_w, uint32_t src_h) noexcept;
    void OnStreamGeometryChanged(uint32_t new_src_w, uint32_t new_src_h) noexcept;
    void EnsureAccessiblePlacement() noexcept;

    void SetAudioState(bool muted, float volume) noexcept;
    void SetOnToggleMute(ActionCallback cb) noexcept { m_on_toggle_mute = std::move(cb); }
    void SetOnVolumeChanged(VolumeCallback cb) noexcept { m_on_volume_changed = std::move(cb); }
    void SetOnFit(ActionCallback cb) noexcept { m_on_fit = std::move(cb); }
    void SetOnVisibilityChanged(VisibilityCallback cb) noexcept { m_on_visibility = std::move(cb); }
    void SetOnGeometryChanged(GeometryChangedCallback cb) noexcept { m_on_geometry_changed = std::move(cb); }
    void RestoreSavedGeometry(const Settings& s) noexcept;
    void UpdateToolbarPosition() noexcept;

    void GetWindowRect(int& x, int& y, int& w, int& h) const noexcept;
    void SetWindowRect(int x, int y, int w, int h) noexcept;

    HWND Hwnd() const noexcept { return m_hwnd; }
    HWND VideoSurfaceHwnd() const noexcept { return m_hwnd; }
    HWND ToolbarHwnd() const noexcept { return m_toolbar_hwnd; }

    uint32_t ClientWidth() const noexcept { return m_client_w.load(std::memory_order_relaxed); }
    uint32_t ClientHeight() const noexcept { return m_client_h.load(std::memory_order_relaxed); }
    uint32_t VideoSurfaceWidth() const noexcept;
    uint32_t VideoSurfaceHeight() const noexcept;
    uint32_t VideoWidth() const noexcept { return m_video_w.load(std::memory_order_relaxed); }
    uint32_t VideoHeight() const noexcept { return m_video_h.load(std::memory_order_relaxed); }

    // Backward compatibility helpers for tests & telemetry
    static constexpr DWORD CaptureWindowStyle() noexcept { return WS_POPUP | WS_CLIPSIBLINGS; }
    static constexpr DWORD CaptureWindowExStyle() noexcept { return WS_EX_APPWINDOW; }
    static uint32_t InstanceCount() noexcept { return s_instance_count.load(std::memory_order_relaxed); }
    void SetCanvasSize(uint32_t w, uint32_t h) noexcept { SetVideoGeometry(w, h); }
    void SetAspectRatio(uint32_t w, uint32_t h) noexcept { SetVideoGeometry(w, h); }
    void SetResolvedOutputSize(uint32_t /*w*/, uint32_t /*h*/) noexcept {}
    void ResetToCanvasSize() noexcept { ApplyComfortableFit(); }
    void SetBackgroundCaptureMode(bool /*enabled*/) noexcept {}
    bool IsBackgroundCaptureMode() const noexcept { return false; }
    uint32_t CanvasWidth() const noexcept { return m_video_w.load(std::memory_order_relaxed); }
    uint32_t CanvasHeight() const noexcept { return m_video_h.load(std::memory_order_relaxed); }
    uint32_t AspectWidth() const noexcept { return m_video_w.load(std::memory_order_relaxed); }
    uint32_t AspectHeight() const noexcept { return m_video_h.load(std::memory_order_relaxed); }

    static constexpr wchar_t kClassName[] = L"DUWNMirrorOutputWindow";
    static constexpr wchar_t kToolbarClass[] = L"DUWNMirrorOutputToolbar";
    static constexpr wchar_t kVideoSurfaceClass[] = L"DUWNMirrorPreviewChild";
    static constexpr wchar_t kPlaceholderClass[] = L"DUWNMirrorPlaceholderChild";
    static constexpr wchar_t kWindowTitle[] = L"Duwn Mirror Output";
    static constexpr UINT WM_APP_UPDATE_GEOMETRY = WM_APP + 201;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK VideoChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK PlaceholderWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT HandleToolbarMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT HandleVideoChildMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT HandlePlaceholderMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;

    void LayoutChildren() noexcept;
    int GetToolbarHeightPx() const noexcept;
    float GetDpiScale() const noexcept;
    LRESULT HandleSizing(WPARAM edge, RECT* prc) noexcept;
    void RecordUserDesiredSize() noexcept;

    static bool s_class_registered;
    static inline std::atomic<uint32_t> s_instance_count{0};

    HWND                  m_hwnd{nullptr};
    HWND                  m_toolbar_hwnd{nullptr};
    HWND                  m_video_surface_hwnd{nullptr};
    HWND                  m_placeholder_hwnd{nullptr};

    ResizeCallback        m_on_resize;
    VisibilityCallback    m_on_visibility;
    ActionCallback        m_on_toggle_mute;
    VolumeCallback        m_on_volume_changed;
    ActionCallback        m_on_fit;
    GeometryChangedCallback m_on_geometry_changed;

    bool                  m_fullscreen{false};
    bool                  m_aspect_locked{true};
    bool                  m_always_on_top{false};
    bool                  m_show_toolbar{true};
    bool                  m_restore_toolbar{true};
    bool                  m_has_frame{false};
    bool                  m_user_hidden_for_session{false};
    bool                  m_pending_geometry_update{false};

    bool                  m_audio_muted{false};
    float                 m_audio_volume{1.0f};

    RECT                  m_restore_rect{};

    std::atomic<uint32_t> m_video_w{1080};
    std::atomic<uint32_t> m_video_h{1920};
    std::atomic<uint32_t> m_client_w{1280};
    std::atomic<uint32_t> m_client_h{720};
    std::atomic<uint64_t> m_geometry_seq{0};

    float                 m_desired_long_edge_dip{0.0f};
    bool                  m_user_has_custom_size{false};

    int                   m_tb_hovered_btn{0}; // 1 Mute, 2 Slider, 3 Fit, 4 Pin, 5 Fullscreen
    int                   m_tb_pressed_btn{0};
    bool                  m_is_dragging_volume{false};
};

} // namespace duwn::app
