#pragma once
// MainWindowView.h — Visual composition, layout engine and interaction handler
// for DUWN Mirror Main Control Window. Conforms to Image A and Image B.

#include "UiRenderer.h"
#include "UiState.h"
#include <functional>
#include <vector>

namespace duwn::ui {

enum class LayoutTier {
    Large,  // >= 1180 DIP: full sidebar (200 DIP), flex center, full right stack (340 DIP)
    Medium, // 900–1179 DIP: compact rail (68 DIP), flex center, compact right stack (310 DIP)
    Small   // < 900 DIP: compact rail (60 DIP), 2-column or stacked layout
};

struct ClickableRegion {
    D2D1_RECT_F rect{};
    int control_id{0};
    std::wstring tooltip{};
    bool scrollable{false};
};

class MainWindowView {
public:
    using ActionCallback = std::function<void()>;
    using TabCallback = std::function<void(NavTab)>;
    using SettingCallback = std::function<void(int, int)>;
    using StringCallback = std::function<void(std::wstring_view)>;
    using TouchTapCallback = std::function<void(uint16_t, uint16_t)>;
    using TouchDragCallback = std::function<void(uint16_t, uint16_t, uint16_t, uint16_t)>;

    MainWindowView() noexcept;
    ~MainWindowView() = default;

    bool Init(HWND hwnd) noexcept;
    void Shutdown() noexcept;

    void Resize(uint32_t width, uint32_t height) noexcept;

    // Responsive tier query
    LayoutTier CurrentLayoutTier() const noexcept { return m_tier; }
    static LayoutTier CalculateTier(float width_dip) noexcept {
        if (width_dip >= 1180.0f) return LayoutTier::Large;
        if (width_dip >= 900.0f)  return LayoutTier::Medium;
        return LayoutTier::Small;
    }

    float DpiX() const noexcept { return m_dpi_x; }
    float DpiY() const noexcept { return m_dpi_y; }

    // Render complete frame
    void Render(const UiState& state) noexcept;

    // Mouse / Interaction Handlers (Return true if redraw needed)
    bool OnMouseMove(int x, int y, UiState& state) noexcept;
    bool OnMouseDown(int x, int y, UiState& state) noexcept;
    bool OnMouseUp(int x, int y, UiState& state) noexcept;
    bool OnDoubleClick(int x, int y, UiState& state) noexcept;
    bool OnMouseLeave(UiState& state) noexcept;
    bool OnMouseWheel(int x, int y, int delta, UiState& state) noexcept;
    bool OnKeyDown(WPARAM vk, UiState& state) noexcept;

    using VolumeCallback = std::function<void(float)>;

    // Callbacks binding
    void SetOnToggleOutputWindow(ActionCallback cb) noexcept { m_on_toggle_output = std::move(cb); }
    void SetOnToggleFullscreen(ActionCallback cb) noexcept { m_on_toggle_fullscreen = std::move(cb); }
    void SetOnToggleAspectLock(ActionCallback cb) noexcept { m_on_toggle_aspect_lock = std::move(cb); }
    void SetOnToggleAlwaysOnTop(ActionCallback cb) noexcept { m_on_toggle_always_on_top = std::move(cb); }
    void SetOnToggleScreenOnly(ActionCallback cb) noexcept { m_on_toggle_screen_only = std::move(cb); }
    void SetOnTogglePreview(ActionCallback cb) noexcept { m_on_toggle_preview = std::move(cb); }
    void SetOnFullscreenPreview(ActionCallback cb) noexcept { m_on_fullscreen_preview = std::move(cb); }
    void SetOnTogglePreviewAlwaysOnTop(ActionCallback cb) noexcept { m_on_toggle_preview_always_on_top = std::move(cb); }
    void SetOnToggleMute(ActionCallback cb) noexcept { m_on_toggle_mute = std::move(cb); }
    void SetOnVolumeChanged(VolumeCallback cb) noexcept { m_on_volume_changed = std::move(cb); }
    void SetOnDisconnect(ActionCallback cb) noexcept { m_on_disconnect = std::move(cb); }
    void SetOnFlushPipeline(ActionCallback cb) noexcept { m_on_flush_pipeline = std::move(cb); }
    void SetOnTabChanged(TabCallback cb) noexcept { m_on_tab_changed = std::move(cb); }
    void SetOnSettingChanged(SettingCallback cb) noexcept { m_on_setting_changed = std::move(cb); }
    void SetOnTestAudio(ActionCallback cb) noexcept { m_on_test_audio = std::move(cb); }
    void SetOnAudioDeviceChanged(StringCallback cb) noexcept { m_on_audio_device_changed = std::move(cb); }
    void SetOnLanguageChanged(StringCallback cb) noexcept { m_on_language_changed = std::move(cb); }
    void SetOnTouchTap(TouchTapCallback cb) noexcept { m_on_touch_tap = std::move(cb); }
    void SetOnTouchDrag(TouchDragCallback cb) noexcept { m_on_touch_drag = std::move(cb); }

private:
    // Section Rendering
    void RenderHeader(const UiState& state, float width) noexcept;
    void RenderCrashBanner(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderFirstRunWelcome(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderSidebar(const UiState& state, float top, float height) noexcept;
    void RenderDevicePreview(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderConnectionModeSelector(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderWiredMirrorView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderRightStack(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderStatusBar(const UiState& state, float bottom_y, float width) noexcept;

    // Sub-views for Navigation Tabs
    void RenderPerformanceView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderVideoView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderColorView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderSettingsView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderDiagnosticsView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderAboutView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderScreenOnlyView(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderDropdownOverlay(const UiState& state) noexcept;

    // Settings sub-pages
    void RenderSettingsSubNav(const UiState& state, const D2D1_RECT_F& area, float& out_content_top) noexcept;
    void RenderGeneralPage(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderOutputPage(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderNetworkPage(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderAudioPage(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderPrivacyPage(const UiState& state, const D2D1_RECT_F& area) noexcept;
    void RenderAdvancedPage(const UiState& state, const D2D1_RECT_F& area) noexcept;

    // Sub-components in Right Stack
    void RenderConnectionCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept;
    void RenderPerformanceCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept;
    void RenderControlsCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept;
    void RenderCreatorTipsCard(const UiState& state, const D2D1_RECT_F& card_rc) noexcept;

    // Hit testing helpers
    void RegisterClickable(const D2D1_RECT_F& rect, int control_id, std::wstring_view tooltip = L"", bool scrollable = false) noexcept;
    int HitTest(float fx, float fy, float scroll_y = 0.0f) const noexcept;

    // Scrolling helpers
    float GetCurrentScrollY(NavTab tab) const noexcept;
    void  SetCurrentScrollY(NavTab tab, float y) noexcept;
    void  RenderScrollbar(float scroll_y, float content_h, float viewport_h, const D2D1_RECT_F& viewport_rc) noexcept;

    UiRenderer                      m_renderer;
    HWND                            m_hwnd{nullptr};
    uint32_t                        m_width{1280};
    uint32_t                        m_height{720};
    float                           m_dpi_x{96.0f};
    float                           m_dpi_y{96.0f};
    LayoutTier                      m_tier{LayoutTier::Large};
    std::vector<ClickableRegion>    m_clickables;
    D2D1_RECT_F                     m_dropdown_anchor_rc{};
    D2D1_RECT_F                     m_wired_preview_rect{};

    // Scrolling state
    float                           m_scroll_y_mirror{0.0f};
    float                           m_scroll_y_performance{0.0f};
    float                           m_scroll_y_video{0.0f};
    float                           m_scroll_y_audio{0.0f};
    float                           m_scroll_y_color{0.0f};
    float                           m_scroll_y_settings{0.0f};
    float                           m_scroll_y_diagnostics{0.0f};
    float                           m_scroll_y_about{0.0f};
    float                           m_content_height{0.0f};
    float                           m_viewport_height{0.0f};
    float                           m_viewport_top{0.0f};
    float                           m_viewport_bottom{0.0f};
    float                           m_viewport_left{0.0f};
    float                           m_viewport_right{0.0f};
    bool                            m_is_dragging_scrollbar{false};
    float                           m_drag_start_mouse_y{0.0f};
    float                           m_drag_start_scroll_y{0.0f};

    // Callbacks
    ActionCallback                  m_on_toggle_output;
    ActionCallback                  m_on_toggle_fullscreen;
    ActionCallback                  m_on_toggle_aspect_lock;
    ActionCallback                  m_on_toggle_always_on_top;
    ActionCallback                  m_on_toggle_screen_only;
    ActionCallback                  m_on_toggle_preview;
    ActionCallback                  m_on_fullscreen_preview;
    ActionCallback                  m_on_toggle_preview_always_on_top;
    ActionCallback                  m_on_toggle_mute;
    VolumeCallback                  m_on_volume_changed;
    ActionCallback                  m_on_disconnect;
    ActionCallback                  m_on_flush_pipeline;
    ActionCallback                  m_on_test_audio;
    TabCallback                     m_on_tab_changed;
    SettingCallback                 m_on_setting_changed;
    StringCallback                  m_on_audio_device_changed;
    StringCallback                  m_on_language_changed;
    TouchTapCallback                m_on_touch_tap;
    TouchDragCallback               m_on_touch_drag;
    bool                            m_touch_down{false};
    float                           m_touch_start_x{0.0f};
    float                           m_touch_start_y{0.0f};
    int ColorValueAt(int control_id, float x) const noexcept;
    float VolumeValueAt(int control_id, float x) const noexcept;
};

} // namespace duwn::ui
