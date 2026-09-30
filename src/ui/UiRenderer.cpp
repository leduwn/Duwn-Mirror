#include "UiRenderer.h"
#include "resource.h"
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace duwn::ui {

UiRenderer::UiRenderer() noexcept = default;

UiRenderer::~UiRenderer() {
    Shutdown();
}

bool UiRenderer::Init(HWND hwnd) noexcept {
    m_hwnd = hwnd;

    RECT rc{};
    ::GetClientRect(m_hwnd, &rc);
    m_width  = std::max(100L, rc.right - rc.left);
    m_height = std::max(100L, rc.bottom - rc.top);

    // Create D2D Factory
    D2D1_FACTORY_OPTIONS options{};
    HRESULT hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, m_factory.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create DirectWrite Factory
    hr = ::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwrite_factory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    // Create WIC Factory (COM already initialized on main thread)
    ::CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(m_wic_factory.GetAddressOf())
    );

    // Fonts setup (Segoe UI Variable / Segoe UI)
    const wchar_t* kFontFamily = L"Segoe UI Variable Display";
    const wchar_t* kFontFallback = L"Segoe UI";

    auto create_format = [this, kFontFamily, kFontFallback](
        float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
        Microsoft::WRL::ComPtr<IDWriteTextFormat>& out_fmt) {
        HRESULT h = m_dwrite_factory->CreateTextFormat(
            kFontFamily, nullptr, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out_fmt.GetAddressOf()
        );
        if (FAILED(h)) {
            m_dwrite_factory->CreateTextFormat(
                kFontFallback, nullptr, weight, style,
                DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out_fmt.GetAddressOf()
            );
        }
        if (out_fmt) {
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(m_dwrite_factory->CreateEllipsisTrimmingSign(out_fmt.Get(), ellipsis.GetAddressOf()))) {
                out_fmt->SetTrimming(&trimming, ellipsis.Get());
            }
        }
    };

    create_format(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, m_fmt_title);
    create_format(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, m_fmt_header);
    create_format(13.0f, DWRITE_FONT_WEIGHT_MEDIUM,    DWRITE_FONT_STYLE_NORMAL, m_fmt_subheader);
    create_format(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, m_fmt_body_bold);
    create_format(12.5f, DWRITE_FONT_WEIGHT_NORMAL,    DWRITE_FONT_STYLE_NORMAL, m_fmt_body);
    create_format(11.0f, DWRITE_FONT_WEIGHT_NORMAL,    DWRITE_FONT_STYLE_NORMAL, m_fmt_small);
    create_format(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, m_fmt_small_bold);
    create_format(11.5f, DWRITE_FONT_WEIGHT_MEDIUM,    DWRITE_FONT_STYLE_NORMAL, m_fmt_mono);
    create_format(26.0f, DWRITE_FONT_WEIGHT_BOLD,      DWRITE_FONT_STYLE_NORMAL, m_fmt_large_digit);

    return CreateDeviceResources();
}

void UiRenderer::Shutdown() noexcept {
    DiscardDeviceResources();
    m_fmt_title.Reset();
    m_fmt_header.Reset();
    m_fmt_subheader.Reset();
    m_fmt_body_bold.Reset();
    m_fmt_body.Reset();
    m_fmt_small.Reset();
    m_fmt_small_bold.Reset();
    m_fmt_mono.Reset();
    m_fmt_large_digit.Reset();
    m_wic_factory.Reset();
    m_dwrite_factory.Reset();
    m_factory.Reset();
    m_hwnd = nullptr;
}

bool UiRenderer::CreateDeviceResources() noexcept {
    if (m_target) return true;
    if (!m_hwnd || !m_factory) return false;

    D2D1_SIZE_U size = D2D1::SizeU(m_width, m_height);
    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
        m_hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    HRESULT hr = m_factory->CreateHwndRenderTarget(
        rt_props, hwnd_props, m_target.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    m_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // Create Solid Brushes
    m_target->CreateSolidColorBrush(colors::Background, m_brush_bg.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::CardSurface, m_brush_card_surface.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::CardSurfaceHover, m_brush_card_hover.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::CardBorder, m_brush_card_border.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::CardBorderHover, m_brush_card_border_hover.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::InnerSurface, m_brush_inner_surface.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::TextPrimary, m_brush_text_primary.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::TextSecondary, m_brush_text_secondary.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::TextMuted, m_brush_text_muted.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::TextAccent, m_brush_text_accent.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::BrandBlue, m_brush_brand_blue.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::BrandPurple, m_brush_brand_purple.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::BrandCyan, m_brush_brand_cyan.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::StatusGreen, m_brush_status_green.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::StatusAmber, m_brush_status_amber.GetAddressOf());
    m_target->CreateSolidColorBrush(colors::StatusRed, m_brush_status_red.GetAddressOf());
    m_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_brush_dynamic_brush.GetAddressOf());

    // Primary Gradient Brush (Brand Blue -> Brand Purple)
    D2D1_GRADIENT_STOP stops[2];
    stops[0].color    = colors::BrandBlue;
    stops[0].position = 0.0f;
    stops[1].color    = colors::BrandPurple;
    stops[1].position = 1.0f;

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stop_col;
    hr = m_target->CreateGradientStopCollection(stops, 2, stop_col.GetAddressOf());
    if (SUCCEEDED(hr)) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES grad_props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(200.0f, 40.0f)
        );
        m_target->CreateLinearGradientBrush(
            grad_props, stop_col.Get(), m_brush_gradient_primary.GetAddressOf()
        );
    }

    // Cache Logo Bitmaps from Win32 Resources via WIC
    m_logo_256 = LoadBitmapFromResource(IDR_PNG_LOGO_256);
    m_logo_64  = LoadBitmapFromResource(IDR_PNG_LOGO_64);
    m_logo_32  = LoadBitmapFromResource(IDR_PNG_LOGO_32);

    return true;
}

void UiRenderer::DiscardDeviceResources() noexcept {
    m_logo_256.Reset();
    m_logo_64.Reset();
    m_logo_32.Reset();
    m_brush_gradient_primary.Reset();
    m_brush_gradient_card.Reset();
    m_brush_bg.Reset();
    m_brush_card_surface.Reset();
    m_brush_card_hover.Reset();
    m_brush_card_border.Reset();
    m_brush_card_border_hover.Reset();
    m_brush_inner_surface.Reset();
    m_brush_text_primary.Reset();
    m_brush_text_secondary.Reset();
    m_brush_text_muted.Reset();
    m_brush_text_accent.Reset();
    m_brush_brand_blue.Reset();
    m_brush_brand_purple.Reset();
    m_brush_brand_cyan.Reset();
    m_brush_status_green.Reset();
    m_brush_status_amber.Reset();
    m_brush_status_red.Reset();
    m_brush_dynamic_brush.Reset();
    m_target.Reset();
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> UiRenderer::LoadBitmapFromResource(int resource_id) noexcept {
    if (!m_target || !m_wic_factory) return nullptr;

    HMODULE hmodule = ::GetModuleHandleW(nullptr);
    HRSRC res = ::FindResourceW(hmodule, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!res) return nullptr;

    HGLOBAL res_data = ::LoadResource(hmodule, res);
    if (!res_data) return nullptr;

    void* ptr = ::LockResource(res_data);
    DWORD size = ::SizeofResource(hmodule, res);
    if (!ptr || size == 0) return nullptr;

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = m_wic_factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    hr = stream->InitializeFromMemory(static_cast<BYTE*>(ptr), size);
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = m_wic_factory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = m_wic_factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    hr = m_target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, bitmap.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    return bitmap;
}

void UiRenderer::DrawLogo(const D2D1_RECT_F& rect) noexcept {
    if (!m_target) return;

    float w = rect.right - rect.left;
    ID2D1Bitmap* bmp = nullptr;
    if (w <= 36.0f && m_logo_32) {
        bmp = m_logo_32.Get();
    } else if (w <= 80.0f && m_logo_64) {
        bmp = m_logo_64.Get();
    } else if (m_logo_256) {
        bmp = m_logo_256.Get();
    } else if (m_logo_64) {
        bmp = m_logo_64.Get();
    } else if (m_logo_32) {
        bmp = m_logo_32.Get();
    }

    if (bmp) {
        m_target->DrawBitmap(
            bmp,
            rect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
    }
}

void UiRenderer::Resize(uint32_t width, uint32_t height) noexcept {
    m_width  = std::max(100u, width);
    m_height = std::max(100u, height);
    if (m_target) {
        D2D1_SIZE_U size = D2D1::SizeU(m_width, m_height);
        m_target->Resize(size);
    }
}

bool UiRenderer::BeginDraw() noexcept {
    if (!CreateDeviceResources()) return false;
    m_target->BeginDraw();
    return true;
}

bool UiRenderer::EndDraw() noexcept {
    if (!m_target) return false;
    HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return false;
    }
    return SUCCEEDED(hr);
}

void UiRenderer::PushClip(const D2D1_RECT_F& rect) noexcept {
    if (m_target) {
        m_target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

void UiRenderer::PopClip() noexcept {
    if (m_target) {
        m_target->PopAxisAlignedClip();
    }
}

float UiRenderer::WidthDip() const noexcept {
    if (m_target) {
        return m_target->GetSize().width;
    }
    return static_cast<float>(m_width);
}

float UiRenderer::HeightDip() const noexcept {
    if (m_target) {
        return m_target->GetSize().height;
    }
    return static_cast<float>(m_height);
}

void UiRenderer::GetDpi(float* out_dpi_x, float* out_dpi_y) const noexcept {
    if (m_target) {
        m_target->GetDpi(out_dpi_x, out_dpi_y);
    } else {
        if (out_dpi_x) *out_dpi_x = 96.0f;
        if (out_dpi_y) *out_dpi_y = 96.0f;
    }
}

void UiRenderer::Clear(const D2D1_COLOR_F& color) noexcept {
    if (m_target) m_target->Clear(color);
}

void UiRenderer::FillRoundedRect(const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush) noexcept {
    if (!m_target || !brush) return;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, radius, radius);
    m_target->FillRoundedRectangle(rr, brush);
}

void UiRenderer::DrawRoundedRect(const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush, float stroke) noexcept {
    if (!m_target || !brush) return;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, radius, radius);
    m_target->DrawRoundedRectangle(rr, brush, stroke);
}

void UiRenderer::FillRect(const D2D1_RECT_F& rect, ID2D1Brush* brush) noexcept {
    if (!m_target || !brush) return;
    m_target->FillRectangle(rect, brush);
}

void UiRenderer::DrawRect(const D2D1_RECT_F& rect, ID2D1Brush* brush, float stroke) noexcept {
    if (!m_target || !brush) return;
    m_target->DrawRectangle(rect, brush, stroke);
}

void UiRenderer::DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush* brush, float stroke) noexcept {
    if (!m_target || !brush) return;
    m_target->DrawLine(p0, p1, brush, stroke);
}

void UiRenderer::DrawTextSimple(std::wstring_view text, IDWriteTextFormat* format,
                                const D2D1_RECT_F& rect, ID2D1Brush* brush,
                                DWRITE_TEXT_ALIGNMENT align,
                                DWRITE_PARAGRAPH_ALIGNMENT p_align) noexcept {
    if (!m_target || !format || !brush || text.empty()) return;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(p_align);
    m_target->DrawTextW(text.data(), static_cast<UINT32>(text.size()),
                        format, rect, brush);
}

void UiRenderer::DrawCard(const D2D1_RECT_F& rect, bool hovered, float radius) noexcept {
    if (!m_target) return;
    ID2D1Brush* fill_brush = hovered ? m_brush_card_hover.Get() : m_brush_card_surface.Get();
    ID2D1Brush* border_brush = hovered ? m_brush_card_border_hover.Get() : m_brush_card_border.Get();

    FillRoundedRect(rect, radius, fill_brush);
    DrawRoundedRect(rect, radius, border_brush, hovered ? 1.4f : 1.0f);
}

void UiRenderer::DrawInset(const D2D1_RECT_F& rect, float radius) noexcept {
    if (!m_target) return;
    FillRoundedRect(rect, radius, m_brush_inner_surface.Get());
    DrawRoundedRect(rect, radius, m_brush_card_border.Get(), 0.8f);
}

void UiRenderer::DrawButton(const D2D1_RECT_F& rect, std::wstring_view label,
                            bool is_primary, bool is_hovered, bool is_pressed,
                            IconType icon) noexcept {
    if (!m_target) return;

    D2D1_RECT_F r = rect;
    if (is_pressed) {
        r.top += 1.0f;
        r.bottom += 1.0f;
    }

    if (is_primary) {
        if (m_brush_gradient_primary) {
            m_brush_gradient_primary->SetStartPoint(D2D1::Point2F(r.left, r.top));
            m_brush_gradient_primary->SetEndPoint(D2D1::Point2F(r.right, r.bottom));
            FillRoundedRect(r, metrics::ButtonRadius, m_brush_gradient_primary.Get());
        } else {
            FillRoundedRect(r, metrics::ButtonRadius, m_brush_brand_blue.Get());
        }

        if (is_hovered) {
            DrawRoundedRect(r, metrics::ButtonRadius, m_brush_text_primary.Get(), 1.5f);
        }

        // Draw Icon if specified
        float icon_sz = 14.0f;
        float text_off = (icon == IconType::Play || icon == IconType::Power || icon == IconType::Refresh) ? 22.0f : 0.0f;
        if (text_off > 0.0f) {
            D2D1_RECT_F icon_rc = D2D1::RectF(
                r.left + 16.0f, r.top + (r.bottom - r.top - icon_sz) * 0.5f,
                r.left + 16.0f + icon_sz, r.top + (r.bottom - r.top + icon_sz) * 0.5f
            );
            DrawIcon(icon, icon_rc, colors::TextPrimary, 1.8f);
        }

        D2D1_RECT_F text_rc = D2D1::RectF(r.left + text_off, r.top, r.right, r.bottom);
        DrawTextSimple(label, m_fmt_body_bold.Get(), text_rc, m_brush_text_primary.Get(),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        ID2D1Brush* bg = is_hovered ? m_brush_card_hover.Get() : m_brush_card_surface.Get();
        FillRoundedRect(r, metrics::ButtonRadius, bg);
        DrawRoundedRect(r, metrics::ButtonRadius,
                       is_hovered ? m_brush_brand_blue.Get() : m_brush_card_border.Get(),
                       is_hovered ? 1.2f : 1.0f);

        DrawTextSimple(label, m_fmt_body.Get(), r,
                       is_hovered ? m_brush_text_primary.Get() : m_brush_text_secondary.Get(),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void UiRenderer::DrawBadge(const D2D1_RECT_F& rect, std::wstring_view text,
                           const D2D1_COLOR_F& bg_color, const D2D1_COLOR_F& text_color,
                           bool border) noexcept {
    if (!m_target) return;
    m_brush_dynamic_brush->SetColor(bg_color);
    FillRoundedRect(rect, metrics::PillRadius, m_brush_dynamic_brush.Get());

    if (border) {
        D2D1_COLOR_F b_color = bg_color;
        b_color.a = std::min(1.0f, b_color.a * 2.0f);
        m_brush_dynamic_brush->SetColor(b_color);
        DrawRoundedRect(rect, metrics::PillRadius, m_brush_dynamic_brush.Get(), 1.0f);
    }

    m_brush_dynamic_brush->SetColor(text_color);
    DrawTextSimple(text, m_fmt_small_bold.Get(), rect, m_brush_dynamic_brush.Get(),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void UiRenderer::DrawStatusDot(D2D1_POINT_2F center, float radius,
                               const D2D1_COLOR_F& color, bool glow) noexcept {
    if (!m_target) return;

    if (glow) {
        // Outer halo
        D2D1_COLOR_F halo_col = color;
        halo_col.a = 0.25f;
        m_brush_dynamic_brush->SetColor(halo_col);
        D2D1_ELLIPSE halo = D2D1::Ellipse(center, radius * 2.2f, radius * 2.2f);
        m_target->FillEllipse(halo, m_brush_dynamic_brush.Get());
    }

    // Solid core
    m_brush_dynamic_brush->SetColor(color);
    D2D1_ELLIPSE core = D2D1::Ellipse(center, radius, radius);
    m_target->FillEllipse(core, m_brush_dynamic_brush.Get());
}

void UiRenderer::DrawIcon(IconType icon, const D2D1_RECT_F& rect,
                          const D2D1_COLOR_F& color, float stroke) noexcept {
    if (!m_target) return;
    m_brush_dynamic_brush->SetColor(color);
    ID2D1Brush* b = m_brush_dynamic_brush.Get();

    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    float cx = rect.left + w * 0.5f;
    float cy = rect.top + h * 0.5f;

    switch (icon) {
    case IconType::Monitor: {
        // Screen frame
        D2D1_RECT_F screen = D2D1::RectF(rect.left + 1.0f, rect.top + 1.0f, rect.right - 1.0f, rect.bottom - 4.0f);
        DrawRoundedRect(screen, 2.0f, b, stroke);
        // Stand base
        DrawLine(D2D1::Point2F(cx, rect.bottom - 4.0f), D2D1::Point2F(cx, rect.bottom - 1.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx - w * 0.25f, rect.bottom - 1.0f), D2D1::Point2F(cx + w * 0.25f, rect.bottom - 1.0f), b, stroke);
        break;
    }
    case IconType::Phone: {
        // Phone body
        D2D1_RECT_F phone = D2D1::RectF(cx - w * 0.35f, rect.top + 1.0f, cx + w * 0.35f, rect.bottom - 1.0f);
        DrawRoundedRect(phone, 3.5f, b, stroke);
        // Dynamic island / notch
        DrawLine(D2D1::Point2F(cx - 3.0f, rect.top + 4.0f), D2D1::Point2F(cx + 3.0f, rect.top + 4.0f), b, stroke * 1.2f);
        // Home indicator line
        DrawLine(D2D1::Point2F(cx - 4.0f, rect.bottom - 4.0f), D2D1::Point2F(cx + 4.0f, rect.bottom - 4.0f), b, stroke * 1.1f);
        break;
    }
    case IconType::Speaker: {
        // Speaker box + horn
        D2D1_POINT_2F pts[6] = {
            { rect.left + 2.0f, cy - 3.0f },
            { rect.left + 5.0f, cy - 3.0f },
            { rect.left + 9.0f, cy - 6.0f },
            { rect.left + 9.0f, cy + 6.0f },
            { rect.left + 5.0f, cy + 3.0f },
            { rect.left + 2.0f, cy + 3.0f }
        };
        for (int i = 0; i < 5; ++i) DrawLine(pts[i], pts[i + 1], b, stroke);
        DrawLine(pts[5], pts[0], b, stroke);
        // Wave arc
        D2D1_RECT_F arc_rc = D2D1::RectF(cx + 1.0f, cy - 4.0f, cx + 5.0f, cy + 4.0f);
        DrawRoundedRect(arc_rc, 2.0f, b, stroke);
        break;
    }
    case IconType::Performance: {
        // Bar meter
        float bar_w = w * 0.18f;
        DrawRect(D2D1::RectF(rect.left + 1.0f, cy + 1.0f, rect.left + 1.0f + bar_w, rect.bottom - 1.0f), b, stroke);
        DrawRect(D2D1::RectF(cx - bar_w * 0.5f, cy - 3.0f, cx + bar_w * 0.5f, rect.bottom - 1.0f), b, stroke);
        DrawRect(D2D1::RectF(rect.right - 1.0f - bar_w, rect.top + 1.0f, rect.right - 1.0f, rect.bottom - 1.0f), b, stroke);
        break;
    }
    case IconType::Settings: {
        // Gear hub + circle
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), w * 0.35f, h * 0.35f);
        m_target->DrawEllipse(el, b, stroke);
        D2D1_ELLIPSE inner = D2D1::Ellipse(D2D1::Point2F(cx, cy), w * 0.15f, h * 0.15f);
        m_target->DrawEllipse(inner, b, stroke);
        break;
    }
    case IconType::Crown: {
        // Crown shape for "Built for Creators"
        D2D1_POINT_2F p1 = { rect.left + 2.0f, rect.bottom - 2.0f };
        D2D1_POINT_2F p2 = { rect.left + 2.0f, rect.top + 5.0f };
        D2D1_POINT_2F p3 = { cx - w * 0.2f, cy + 1.0f };
        D2D1_POINT_2F p4 = { cx, rect.top + 2.0f };
        D2D1_POINT_2F p5 = { cx + w * 0.2f, cy + 1.0f };
        D2D1_POINT_2F p6 = { rect.right - 2.0f, rect.top + 5.0f };
        D2D1_POINT_2F p7 = { rect.right - 2.0f, rect.bottom - 2.0f };
        DrawLine(p1, p2, b, stroke);
        DrawLine(p2, p3, b, stroke);
        DrawLine(p3, p4, b, stroke);
        DrawLine(p4, p5, b, stroke);
        DrawLine(p5, p6, b, stroke);
        DrawLine(p6, p7, b, stroke);
        DrawLine(p7, p1, b, stroke);
        break;
    }
    case IconType::Play: {
        D2D1_POINT_2F t0 = { cx - 3.5f, cy - 5.5f };
        D2D1_POINT_2F t1 = { cx + 5.5f, cy };
        D2D1_POINT_2F t2 = { cx - 3.5f, cy + 5.5f };
        DrawLine(t0, t1, b, stroke * 1.2f);
        DrawLine(t1, t2, b, stroke * 1.2f);
        DrawLine(t2, t0, b, stroke * 1.2f);
        break;
    }
    case IconType::Pause: {
        DrawLine(D2D1::Point2F(cx - 3.0f, cy - 5.0f), D2D1::Point2F(cx - 3.0f, cy + 5.0f), b, stroke * 1.5f);
        DrawLine(D2D1::Point2F(cx + 3.0f, cy - 5.0f), D2D1::Point2F(cx + 3.0f, cy + 5.0f), b, stroke * 1.5f);
        break;
    }
    case IconType::Wifi: {
        // Arc + dot
        D2D1_ELLIPSE d = D2D1::Ellipse(D2D1::Point2F(cx, rect.bottom - 3.0f), 1.5f, 1.5f);
        m_target->FillEllipse(d, b);
        D2D1_RECT_F r1 = D2D1::RectF(cx - 5.0f, cy - 2.0f, cx + 5.0f, rect.bottom - 3.0f);
        DrawRoundedRect(r1, 3.0f, b, stroke);
        break;
    }
    case IconType::Maximize: {
        D2D1_RECT_F r_out = D2D1::RectF(rect.left + 2.0f, rect.top + 2.0f, rect.right - 2.0f, rect.bottom - 2.0f);
        DrawRoundedRect(r_out, 2.0f, b, stroke);
        break;
    }
    case IconType::Lock: {
        D2D1_RECT_F body = D2D1::RectF(rect.left + 3.0f, cy - 1.0f, rect.right - 3.0f, rect.bottom - 2.0f);
        DrawRoundedRect(body, 2.0f, b, stroke);
        D2D1_RECT_F shackle = D2D1::RectF(cx - 4.0f, rect.top + 2.0f, cx + 4.0f, cy);
        DrawRoundedRect(shackle, 3.0f, b, stroke);
        break;
    }
    case IconType::Pin: {
        DrawLine(D2D1::Point2F(cx, rect.top + 2.0f), D2D1::Point2F(cx, cy + 2.0f), b, stroke * 2.0f);
        DrawLine(D2D1::Point2F(cx - 4.0f, cy + 2.0f), D2D1::Point2F(cx + 4.0f, cy + 2.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx, cy + 2.0f), D2D1::Point2F(cx, rect.bottom - 2.0f), b, stroke);
        break;
    }
    case IconType::Chip: {
        D2D1_RECT_F chip = D2D1::RectF(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f);
        DrawRoundedRect(chip, 1.5f, b, stroke);
        // Pin leads
        DrawLine(D2D1::Point2F(cx - 7.0f, cy - 2.0f), D2D1::Point2F(cx - 5.0f, cy - 2.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx - 7.0f, cy + 2.0f), D2D1::Point2F(cx - 5.0f, cy + 2.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx + 5.0f, cy - 2.0f), D2D1::Point2F(cx + 7.0f, cy - 2.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx + 5.0f, cy + 2.0f), D2D1::Point2F(cx + 7.0f, cy + 2.0f), b, stroke);
        break;
    }
    case IconType::CheckCircle: {
        D2D1_ELLIPSE circ = D2D1::Ellipse(D2D1::Point2F(cx, cy), w * 0.42f, h * 0.42f);
        m_target->DrawEllipse(circ, b, stroke);
        DrawLine(D2D1::Point2F(cx - 3.0f, cy), D2D1::Point2F(cx - 1.0f, cy + 2.5f), b, stroke * 1.2f);
        DrawLine(D2D1::Point2F(cx - 1.0f, cy + 2.5f), D2D1::Point2F(cx + 4.0f, cy - 2.5f), b, stroke * 1.2f);
        break;
    }
    case IconType::Power: {
        D2D1_ELLIPSE circ = D2D1::Ellipse(D2D1::Point2F(cx, cy + 1.0f), w * 0.35f, h * 0.35f);
        m_target->DrawEllipse(circ, b, stroke);
        DrawLine(D2D1::Point2F(cx, rect.top + 2.0f), D2D1::Point2F(cx, cy + 1.0f), b, stroke * 1.2f);
        break;
    }
    case IconType::Refresh: {
        D2D1_ELLIPSE circ = D2D1::Ellipse(D2D1::Point2F(cx, cy), w * 0.35f, h * 0.35f);
        m_target->DrawEllipse(circ, b, stroke);
        DrawLine(D2D1::Point2F(cx + w * 0.35f - 2.0f, cy - 2.0f), D2D1::Point2F(cx + w * 0.35f, cy + 1.0f), b, stroke);
        DrawLine(D2D1::Point2F(cx + w * 0.35f, cy + 1.0f), D2D1::Point2F(cx + w * 0.35f + 3.0f, cy - 2.0f), b, stroke);
        break;
    }
    }
}

} // namespace duwn::ui
