#pragma once
// UiRenderer.h — Hardware-accelerated Direct2D & DirectWrite graphics subsystem for DUWN Mirror.

#include "Theme.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string_view>
#include <memory>

namespace duwn::ui {

enum class IconType {
    Monitor,
    Phone,
    Speaker,
    Performance,
    Settings,
    Crown,
    Play,
    Pause,
    Wifi,
    Maximize,
    Lock,
    Pin,
    Chip,
    CheckCircle,
    Power,
    Refresh,
    Palette
};

class UiRenderer {
public:
    UiRenderer() noexcept;
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    bool Init(HWND hwnd) noexcept;
    void Shutdown() noexcept;

    // Resize backbuffer
    void Resize(uint32_t width, uint32_t height) noexcept;

    // Frame lifecycle
    bool BeginDraw() noexcept;
    bool EndDraw() noexcept; // Returns false if device lost (triggers recreate)

    // Render target access
    ID2D1HwndRenderTarget* Target() const noexcept { return m_target.Get(); }
    ID2D1Factory* Factory() const noexcept { return m_factory.Get(); }
    IDWriteFactory* DWriteFactory() const noexcept { return m_dwrite_factory.Get(); }

    // Direct2D Axis-Aligned Scissoring / Clipping
    void PushClip(const D2D1_RECT_F& rect) noexcept;
    void PopClip() noexcept;

    // DIP Dimensions and DPI Query
    float WidthDip() const noexcept;
    float HeightDip() const noexcept;
    void GetDpi(float* out_dpi_x, float* out_dpi_y) const noexcept;

    // Drawing Primitives
    void Clear(const D2D1_COLOR_F& color = colors::Background) noexcept;

    void FillRoundedRect(const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush) noexcept;
    void DrawRoundedRect(const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush, float stroke = 1.0f) noexcept;

    void FillRect(const D2D1_RECT_F& rect, ID2D1Brush* brush) noexcept;
    void DrawRect(const D2D1_RECT_F& rect, ID2D1Brush* brush, float stroke = 1.0f) noexcept;

    void DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush* brush, float stroke = 1.0f) noexcept;

    // Text Rendering
    void DrawTextSimple(std::wstring_view text, IDWriteTextFormat* format,
                        const D2D1_RECT_F& rect, ID2D1Brush* brush,
                        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT p_align = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) noexcept;

    // Pre-composed Components
    void DrawCard(const D2D1_RECT_F& rect, bool hovered = false, float radius = metrics::CardRadius) noexcept;
    void DrawInset(const D2D1_RECT_F& rect, float radius = 8.0f) noexcept;
    void DrawButton(const D2D1_RECT_F& rect, std::wstring_view label,
                    bool is_primary, bool is_hovered, bool is_pressed,
                    IconType icon = IconType::Play) noexcept;
    void DrawBadge(const D2D1_RECT_F& rect, std::wstring_view text,
                   const D2D1_COLOR_F& bg_color, const D2D1_COLOR_F& text_color,
                   bool border = false) noexcept;
    void DrawStatusDot(D2D1_POINT_2F center, float radius,
                       const D2D1_COLOR_F& color, bool glow = true) noexcept;

    // Vector procedural icons
    void DrawIcon(IconType icon, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float stroke = 1.6f) noexcept;

    // Brand Logo (WIC bitmap from the clean embedded asset)
    void DrawLogo(const D2D1_RECT_F& rect) noexcept;

    // Typography Accessors
    IDWriteTextFormat* FontTitle() const noexcept { return m_fmt_title.Get(); }
    IDWriteTextFormat* FontHeader() const noexcept { return m_fmt_header.Get(); }
    IDWriteTextFormat* FontSubheader() const noexcept { return m_fmt_subheader.Get(); }
    IDWriteTextFormat* FontBodyBold() const noexcept { return m_fmt_body_bold.Get(); }
    IDWriteTextFormat* FontBody() const noexcept { return m_fmt_body.Get(); }
    IDWriteTextFormat* FontSmall() const noexcept { return m_fmt_small.Get(); }
    IDWriteTextFormat* FontSmallBold() const noexcept { return m_fmt_small_bold.Get(); }
    IDWriteTextFormat* FontMono() const noexcept { return m_fmt_mono.Get(); }
    IDWriteTextFormat* FontLargeDigit() const noexcept { return m_fmt_large_digit.Get(); }

    // Brush Accessors
    ID2D1SolidColorBrush* BrushTextPrimary() const noexcept { return m_brush_text_primary.Get(); }
    ID2D1SolidColorBrush* BrushTextSecondary() const noexcept { return m_brush_text_secondary.Get(); }
    ID2D1SolidColorBrush* BrushTextMuted() const noexcept { return m_brush_text_muted.Get(); }
    ID2D1SolidColorBrush* BrushTextAccent() const noexcept { return m_brush_text_accent.Get(); }
    ID2D1SolidColorBrush* BrushBrandBlue() const noexcept { return m_brush_brand_blue.Get(); }
    ID2D1SolidColorBrush* BrushBrandPurple() const noexcept { return m_brush_brand_purple.Get(); }
    ID2D1SolidColorBrush* BrushBrandCyan() const noexcept { return m_brush_brand_cyan.Get(); }
    ID2D1SolidColorBrush* BrushStatusGreen() const noexcept { return m_brush_status_green.Get(); }
    ID2D1SolidColorBrush* BrushStatusAmber() const noexcept { return m_brush_status_amber.Get(); }
    ID2D1SolidColorBrush* BrushStatusRed() const noexcept { return m_brush_status_red.Get(); }
    ID2D1SolidColorBrush* BrushCardBorder() const noexcept { return m_brush_card_border.Get(); }
    ID2D1LinearGradientBrush* BrushPrimaryGradient() const noexcept { return m_brush_gradient_primary.Get(); }

private:
    bool CreateDeviceResources() noexcept;
    void DiscardDeviceResources() noexcept;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadBitmapFromResource(int resource_id) noexcept;

    HWND m_hwnd{nullptr};
    uint32_t m_width{1280};
    uint32_t m_height{720};

    // Factories
    Microsoft::WRL::ComPtr<ID2D1Factory>            m_factory;
    Microsoft::WRL::ComPtr<IDWriteFactory>          m_dwrite_factory;
    Microsoft::WRL::ComPtr<IWICImagingFactory>      m_wic_factory;

    // Device resources
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget>   m_target;

    // Cached Logo Bitmaps
    Microsoft::WRL::ComPtr<ID2D1Bitmap>             m_logo_256;
    Microsoft::WRL::ComPtr<ID2D1Bitmap>             m_logo_64;
    Microsoft::WRL::ComPtr<ID2D1Bitmap>             m_logo_32;

    // Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_bg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_card_surface;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_card_hover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_card_border;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_card_border_hover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_inner_surface;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_text_primary;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_text_secondary;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_text_muted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_text_accent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_brand_blue;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_brand_purple;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_brand_cyan;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_status_green;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_status_amber;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_status_red;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    m_brush_dynamic_brush; // reusable for arbitrary colors

    // Gradients
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_brush_gradient_primary;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_brush_gradient_card;

    // DirectWrite Formats
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_title;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_header;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_subheader;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_body_bold;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_body;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_small;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_small_bold;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_mono;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>       m_fmt_large_digit;
};

} // namespace duwn::ui
