#pragma once
// MainWindow — Modern Direct2D Control Window for DUWN Mirror.
// Hosts the main user interface matching Image A and Image B.
// Completely decoupled from video presentation thread.

#include "Settings.h"
#include "ui/MainWindowView.h"
#include "ui/UiState.h"
#include "airplay/AirPlayEngine.h"
#include <windows.h>
#include <string>
#include <memory>
#include <functional>

namespace duwn::app {

class MainWindow {
public:
    explicit MainWindow(std::function<void()> on_close) noexcept;
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(const Settings& settings) noexcept;

    void SetTitle(std::wstring_view title) noexcept;
    void SetStatusText(std::wstring_view status) noexcept;

    // Telemetry and session update methods (called from App)
    void UpdateSessionState(airplay::AirPlaySessionState state) noexcept;
    void UpdateClientInfo(const airplay::AirPlayClientInfo& info) noexcept;
    void UpdateSessionPhase(airplay::SessionPhase phase) noexcept;
    void UpdateStreamMetadata(const airplay::StreamMetadata& meta) noexcept;
    void UpdateTelemetry(double render_fps, double latency_ms,
                         uint32_t queue_depth, uint64_t drops,
                         int64_t uptime_sec) noexcept;
    void UpdateExtendedTelemetry(uint64_t video_rtp, uint64_t audio_rtp,
                                 uint64_t audio_underruns, bool audio_active,
                                 bool audio_muted, std::wstring_view client_ip,
                                 std::wstring_view device_name) noexcept;

    void SetOutputControlsState(bool visible, bool fullscreen, bool aspect_locked, bool always_on_top) noexcept;

    // Action callbacks for user controls
    void SetOnToggleOutputWindow(std::function<void()> cb) noexcept;
    void SetOnToggleFullscreen(std::function<void()> cb) noexcept;
    void SetOnToggleAspectLock(std::function<void()> cb) noexcept;
    void SetOnToggleAlwaysOnTop(std::function<void()> cb) noexcept;
    void SetOnToggleScreenOnly(std::function<void()> cb) noexcept;
    void SetOnTogglePreview(std::function<void()> cb) noexcept;
    void SetOnFullscreenPreview(std::function<void()> cb) noexcept;
    void SetOnTogglePreviewAlwaysOnTop(std::function<void()> cb) noexcept;
    void SetOnToggleMute(std::function<void()> cb) noexcept;
    void SetOnVolumeChanged(std::function<void(float)> cb) noexcept;
    void SetOnDisconnect(std::function<void()> cb) noexcept;
    void SetOnFlushPipeline(std::function<void()> cb) noexcept;
    void SetOnSettingChanged(ui::MainWindowView::SettingCallback cb) noexcept;
    void SetOnTestAudio(ui::MainWindowView::ActionCallback cb) noexcept;
    void SetOnAudioDeviceChanged(ui::MainWindowView::StringCallback cb) noexcept;
    void SetOnLanguageChanged(ui::MainWindowView::StringCallback cb) noexcept;
    void SetOnTouchTap(ui::MainWindowView::TouchTapCallback cb) noexcept;
    void SetOnTouchDrag(ui::MainWindowView::TouchDragCallback cb) noexcept;

    HWND Hwnd() const noexcept { return m_hwnd; }
    HWND VideoSurfaceHwnd() const noexcept { return m_video_surface_hwnd; }
    ui::UiState& State() noexcept { return m_state; }

    void LayoutVideoSurface() noexcept;
    void SetOnVideoSurfaceResize(std::function<void(uint32_t, uint32_t)> cb) noexcept {
        m_on_video_surface_resize = std::move(cb);
    }
    void ToggleFullscreen() noexcept;
    bool IsFullscreen() const noexcept { return m_fullscreen; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK VideoChildWndProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK ScreenOnlyToolbarWndProc(HWND hwnd, UINT msg,
                                                     WPARAM wp, LPARAM lp) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;

    void ApplyImmersiveDarkMode() noexcept;

    HWND                    m_hwnd{nullptr};
    HWND                    m_video_surface_hwnd{nullptr};
    HWND                    m_screen_only_toolbar_hwnd{nullptr};
    std::function<void()>   m_on_close;
    std::function<void()>   m_on_toggle_mute;
    std::function<void()>   m_on_toggle_screen_only;
    std::function<void(uint32_t, uint32_t)> m_on_video_surface_resize;
    bool                    m_fullscreen{false};
    WINDOWPLACEMENT         m_prev_placement{};
    std::wstring            m_status;

    ui::MainWindowView      m_view;
    ui::UiState             m_state;
    bool                    m_mouse_tracking{false};

    ui::MainWindowView::TouchTapCallback  m_on_touch_tap;
    ui::MainWindowView::TouchDragCallback m_on_touch_drag;
};

} // namespace duwn::app
