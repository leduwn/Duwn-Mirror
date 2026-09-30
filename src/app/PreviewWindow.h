#pragma once
// PreviewWindow — standard Win32 viewer window for local user monitoring.
//
// Distinct from MainWindow (controls/telemetry) and OutputWindow (OBS capture target).
// Lifetime: created by App, kept alive for process lifetime.
// WM_CLOSE hides the window (does not terminate app or destroy HWND).
// Resizing this window never affects OutputWindow or capture resolution.

#include <windows.h>
#include <cstdint>
#include <functional>
#include <atomic>

namespace duwn::app {

enum class PreviewSizingMode {
    AutoSized, // Follows comfortable defaults on stream start and rotate
    UserSized  // User manually resized/moved window; preserve surface area on rotate
};

using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
using VisibilityCallback = std::function<void(bool visible)>;

class PreviewWindow {
public:
    PreviewWindow() noexcept = default;
    ~PreviewWindow();

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    // Create the window. on_resize is called from WM_SIZE with new client dimensions.
    bool Create(uint32_t initial_w = 0, uint32_t initial_h = 0,
                ResizeCallback on_resize = {}) noexcept;

    void Show() noexcept;
    void ShowNoActivate() noexcept;
    void Hide() noexcept;
    void ToggleVisibility() noexcept;

    bool IsVisible() const noexcept;
    bool IsAlwaysOnTop() const noexcept { return m_always_on_top; }
    bool IsFullscreen() const noexcept { return m_fullscreen; }

    void SetAlwaysOnTop(bool top) noexcept;
    void ToggleAlwaysOnTop() noexcept;
    void ToggleFullscreen() noexcept;

    PreviewSizingMode SizingMode() const noexcept { return m_sizing_mode; }
    void SetSizingMode(PreviewSizingMode mode) noexcept { m_sizing_mode = mode; }

    // Stream geometry transition from StreamGeometryCoordinator
    void OnStreamGeometryChanged(uint32_t new_src_w, uint32_t new_src_h) noexcept;

    void SetOnVisibilityChanged(VisibilityCallback cb) noexcept {
        m_on_visibility = std::move(cb);
    }

    static uint32_t InstanceCount() noexcept {
        return s_instance_count.load(std::memory_order_relaxed);
    }

    // Set video aspect ratio and calculate/apply initial comfortable placement if desired
    void SetVideoGeometry(uint32_t video_w, uint32_t video_h) noexcept;
    void ApplyComfortableSize(uint32_t video_w, uint32_t video_h) noexcept;

    static void CalculateComfortableInitialRect(HWND target_hwnd,
                                              uint32_t video_w, uint32_t video_h,
                                              int& out_x, int& out_y,
                                              int& out_w, int& out_h) noexcept;

    uint32_t ClientWidth() const noexcept { return m_client_w.load(std::memory_order_relaxed); }
    uint32_t ClientHeight() const noexcept { return m_client_h.load(std::memory_order_relaxed); }
    uint32_t VideoWidth() const noexcept { return m_video_w.load(std::memory_order_relaxed); }
    uint32_t VideoHeight() const noexcept { return m_video_h.load(std::memory_order_relaxed); }

    HWND Hwnd() const noexcept { return m_hwnd; }

    void GetWindowRect(int& x, int& y, int& w, int& h) const noexcept;
    void SetWindowRect(int x, int y, int w, int h) noexcept;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wparam, LPARAM lparam) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;

    static constexpr wchar_t kClassName[] = L"DuwnMirrorPreviewWindow";
    static constexpr wchar_t kWindowTitle[] = L"Duwn Mirror Preview";
    static bool s_class_registered;
    static inline std::atomic<uint32_t> s_instance_count{0};

    HWND                  m_hwnd{nullptr};
    ResizeCallback        m_on_resize;
    VisibilityCallback    m_on_visibility;
    bool                  m_fullscreen{false};
    bool                  m_always_on_top{false};
    bool                  m_has_custom_placement{false};
    PreviewSizingMode     m_sizing_mode{PreviewSizingMode::AutoSized};
    bool                  m_in_sizemove{false};
    RECT                  m_restore_rect{};

    std::atomic<uint32_t> m_client_w{0};
    std::atomic<uint32_t> m_client_h{0};
    std::atomic<uint32_t> m_video_w{1184};
    std::atomic<uint32_t> m_video_h{2560};
};

} // namespace duwn::app
