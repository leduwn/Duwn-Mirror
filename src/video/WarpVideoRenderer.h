#pragma once

#include "IVideoRenderer.h"
#include "D3D11Device.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <array>
#include <atomic>

namespace duwn::video {

// Software H.264 -> uploaded NV12 -> WARP pixel shader -> DXGI swap chain.
// Isolated from the hardware VideoProcessor path.
class WarpVideoRenderer final : public IVideoRenderer {
public:
    WarpVideoRenderer(D3D11Device& device, HWND hwnd) noexcept : m_device(device), m_hwnd(hwnd) {}
    ~WarpVideoRenderer() override;

    bool Init(uint32_t width, uint32_t height) noexcept override;
    PresentResult Present(VideoFrame& frame, bool skip_wait) noexcept override;
    HANDLE GetFrameLatencyWaitableObject() const noexcept override { return m_waitable; }
    void SignalResize(uint32_t width, uint32_t height) noexcept override {
        if (width && height) m_pending_resize.store((uint64_t(width) << 32) | height, std::memory_order_relaxed);
    }
    void SetAspectRatioMode(AspectRatioMode mode) noexcept override { m_mode.store(mode, std::memory_order_relaxed); }
    void SetPixelPerfect(int mode) noexcept override { m_pixel_perfect.store(mode, std::memory_order_relaxed); }
    void SetScalingQuality(int quality) noexcept override { m_quality.store(quality, std::memory_order_relaxed); }
    void SetColorSpace(int range, int matrix) noexcept override {
        m_range.store(range, std::memory_order_relaxed);
        m_matrix.store(matrix, std::memory_order_relaxed);
    }
    void SetColorControl(size_t index, int value) noexcept override {
        if (index < 5) m_controls[index].store(value, std::memory_order_relaxed);
    }
    bool SupportsColorControl(size_t index) const noexcept override { return index < 5; }
    uint32_t FilterCaps() const noexcept override { return 0x1f; }
    uint32_t SwapWidth() const noexcept override { return m_width; }
    uint32_t SwapHeight() const noexcept override { return m_height; }
    bool HandleDeviceRemoved() noexcept override { return false; }
    void SetExportTarget(ID3D11Texture2D* dst) noexcept override { m_export_target = dst; }
    void CopyBackBufferTo(ID3D11Texture2D* dst) noexcept override;
    void PresentBlack() noexcept override;

private:
    bool CreateRenderTarget() noexcept;
    bool ResizeIfNeeded() noexcept;
    bool GetPlaneViews(ID3D11Texture2D* texture, UINT subresource,
                       ID3D11ShaderResourceView** y, ID3D11ShaderResourceView** uv) noexcept;

    struct CachedViews {
        ComPtr<ID3D11Texture2D> texture;
        UINT subresource{0};
        ComPtr<ID3D11ShaderResourceView> y;
        ComPtr<ID3D11ShaderResourceView> uv;
    };
    D3D11Device& m_device;
    HWND m_hwnd{};
    ComPtr<IDXGISwapChain1> m_swap_chain;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Buffer> m_constants;
    std::array<CachedViews, 6> m_views;
    size_t m_next_view{0};
    HANDLE m_waitable{nullptr};
    uint32_t m_width{0}, m_height{0};
    std::atomic<uint64_t> m_pending_resize{0};
    std::atomic<AspectRatioMode> m_mode{AspectRatioMode::Fit};
    std::atomic<int> m_pixel_perfect{0};
    std::atomic<int> m_quality{0}, m_range{0}, m_matrix{0};
    std::array<std::atomic<int>, 5> m_controls{};
    ID3D11Texture2D* m_export_target{nullptr};
};

} // namespace duwn::video
