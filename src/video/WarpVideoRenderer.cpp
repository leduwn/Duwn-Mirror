#include "WarpVideoRenderer.h"
#include "VideoGeometry.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include "common/telemetry/ConnectionTelemetry.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <cmath>

#pragma comment(lib, "d3dcompiler.lib")

namespace duwn::video {

namespace {
constexpr char kShader[] = R"hlsl(
Texture2D yPlane : register(t0);
Texture2D uvPlane : register(t1);
SamplerState linearSampler : register(s0);
cbuffer Params : register(b0) {
    float4 uvRect;
    float4 codedSize;    // width, height, 1/width, 1/height
    float4 adjust;       // brightness, contrast, saturation, hue radians
    float4 options;      // range (1 limited / 2 full), matrix, sharp, quality
};
struct VOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VOut VS(uint id : SV_VertexID) {
    VOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1);
    return o;
}
float cubic(float x) {
    x = abs(x);
    if (x < 1) return 1.5*x*x*x - 2.5*x*x + 1;
    if (x < 2) return -0.5*x*x*x + 2.5*x*x - 4*x + 2;
    return 0;
}
float3 sampleYuv(float2 uv) {
    if (options.w < 3.5) return float3(yPlane.Sample(linearSampler, uv).r,
                                       uvPlane.Sample(linearSampler, uv).rg);
    float2 base = floor(uv * codedSize.xy - 0.5);
    float2 fracPart = uv * codedSize.xy - 0.5 - base;
    float3 sum = 0;
    float total = 0;
    [unroll] for (int j = -1; j <= 2; ++j) {
        [unroll] for (int i = -1; i <= 2; ++i) {
            float weight = cubic(i - fracPart.x) * cubic(j - fracPart.y);
            float2 pos = (base + float2(i, j) + 0.5) * codedSize.zw;
            sum += float3(yPlane.SampleLevel(linearSampler, pos, 0).r,
                          uvPlane.SampleLevel(linearSampler, pos, 0).rg) * weight;
            total += weight;
        }
    }
    return sum / max(total, 0.0001);
}
float4 PS(VOut input) : SV_Target {
    float2 uv = lerp(uvRect.xy, uvRect.zw, input.uv);
    float3 sample = sampleYuv(uv);
    float y = sample.x;
    if (options.z > 0) {
        float2 dx = float2(codedSize.z, 0);
        float2 dy = float2(0, codedSize.w);
        float neighbors = yPlane.Sample(linearSampler, uv - dx).r +
                          yPlane.Sample(linearSampler, uv + dx).r +
                          yPlane.Sample(linearSampler, uv - dy).r +
                          yPlane.Sample(linearSampler, uv + dy).r;
        y = saturate(y + options.z * (4*y - neighbors) * 0.25);
    }
    float u = sample.y - 128.0/255.0;
    float v = sample.z - 128.0/255.0;
    if (options.x < 1.5) {
        y = (y - 16.0/255.0) * (255.0/219.0);
        u *= 255.0/224.0;
        v *= 255.0/224.0;
    }
    float s = sin(adjust.w), c = cos(adjust.w);
    float2 chroma = float2(c*u - s*v, s*u + c*v);
    u = chroma.x * adjust.z;
    v = chroma.y * adjust.z;
    float3 rgb;
    if (options.y < 1.5) rgb = float3(y + 1.402*v, y - 0.344136*u - 0.714136*v, y + 1.772*u);
    else if (options.y < 2.5) rgb = float3(y + 1.5748*v, y - 0.187324*u - 0.468124*v, y + 1.8556*u);
    else rgb = float3(y + 1.4746*v, y - 0.16455*u - 0.57135*v, y + 1.8814*u);
    rgb = (rgb - 0.5) * adjust.y + 0.5 + adjust.x;
    return float4(saturate(rgb), 1);
}
)hlsl";

struct alignas(16) ShaderParams {
    float uv_rect[4];
    float coded_size[4];
    float adjust[4];
    float options[4];
};
}

WarpVideoRenderer::~WarpVideoRenderer() {
    if (m_waitable) ::CloseHandle(m_waitable);
}

bool WarpVideoRenderer::CreateRenderTarget() noexcept {
    ComPtr<ID3D11Texture2D> back_buffer;
    if (FAILED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) return false;
    return SUCCEEDED(m_device.Device()->CreateRenderTargetView(
        back_buffer.Get(), nullptr, m_rtv.GetAddressOf()));
}

void WarpVideoRenderer::CopyBackBufferTo(ID3D11Texture2D* dst) noexcept {
    if (!dst || !m_swap_chain) return;
    ComPtr<ID3D11Texture2D> bb;
    if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
        m_device.Context()->CopyResource(dst, bb.Get());
    }
}


bool WarpVideoRenderer::Init(uint32_t width, uint32_t height) noexcept {
    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(m_device.Device()->QueryInterface(IID_PPV_ARGS(&dxgi_device)))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) return false;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        m_device.Device(), m_hwnd, &desc, nullptr, nullptr, m_swap_chain.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("WarpVideoRenderer", "CreateSwapChainForHwnd failed hr={:#010x}", static_cast<unsigned>(hr));
        return false;
    }
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    ComPtr<IDXGISwapChain2> swap2;
    if (SUCCEEDED(m_swap_chain.As(&swap2)) && SUCCEEDED(swap2->SetMaximumFrameLatency(1)))
        m_waitable = swap2->GetFrameLatencyWaitableObject();
    m_width = width; m_height = height;
    if (!CreateRenderTarget()) return false;

    auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
        ComPtr<ID3DBlob> errors;
        HRESULT result = ::D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr,
                                      entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                      blob.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result))
            DUWN_LOG_ERRORF("WarpVideoRenderer", "Shader {} compile failed hr={:#010x}: {}",
                entry, static_cast<unsigned>(result),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown");
        return SUCCEEDED(result);
    };
    ComPtr<ID3DBlob> vs, ps;
    if (!compile("VS", "vs_5_0", vs) || !compile("PS", "ps_5_0", ps)) return false;
    if (FAILED(m_device.Device()->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vs.GetAddressOf())) ||
        FAILED(m_device.Device()->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ps.GetAddressOf()))) return false;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device.Device()->CreateSamplerState(&sampler, m_sampler.GetAddressOf()))) return false;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(ShaderParams);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device.Device()->CreateBuffer(&cb, nullptr, m_constants.GetAddressOf()))) return false;
    DUWN_LOG_INFOF("WarpVideoRenderer", "WARP shader renderer initialized {}x{}", width, height);
    return true;
}

bool WarpVideoRenderer::ResizeIfNeeded() noexcept {
    const uint64_t packed = m_pending_resize.exchange(0, std::memory_order_relaxed);
    if (!packed) return true;
    const uint32_t width = static_cast<uint32_t>(packed >> 32);
    const uint32_t height = static_cast<uint32_t>(packed);
    if (width == m_width && height == m_height) return true;
    std::lock_guard lock{m_device.ContextMutex()};
    m_device.Context()->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
    HRESULT hr = m_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                              DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr)) return false;
    m_width = width; m_height = height;
    return CreateRenderTarget();
}

bool WarpVideoRenderer::GetPlaneViews(ID3D11Texture2D* texture, UINT subresource,
                                     ID3D11ShaderResourceView** y, ID3D11ShaderResourceView** uv) noexcept {
    for (auto& cached : m_views) {
        if (cached.texture.Get() == texture && cached.subresource == subresource && cached.y && cached.uv) {
            *y = cached.y.Get(); *uv = cached.uv.Get();
            return true;
        }
    }
    auto& cached = m_views[m_next_view++ % m_views.size()];
    cached = {};
    cached.texture = texture;
    cached.subresource = subresource;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    HRESULT hr = m_device.Device()->CreateShaderResourceView(texture, &desc, cached.y.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("WarpVideoRenderer", "Y plane SRV failed hr={:#010x}", static_cast<unsigned>(hr));
        return false;
    }
    desc.Format = DXGI_FORMAT_R8G8_UNORM;
    hr = m_device.Device()->CreateShaderResourceView(texture, &desc, cached.uv.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("WarpVideoRenderer", "UV plane SRV failed hr={:#010x}", static_cast<unsigned>(hr));
        return false;
    }
    *y = cached.y.Get(); *uv = cached.uv.Get();
    return true;
}

void WarpVideoRenderer::PresentBlack() noexcept {
    if (!m_swap_chain || !m_rtv) return;
    std::lock_guard lock{m_device.ContextMutex()};
    constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_device.Context()->ClearRenderTargetView(m_rtv.Get(), black);
    m_swap_chain->Present(0, 0);
}

PresentResult WarpVideoRenderer::Present(VideoFrame& frame, bool skip_wait) noexcept {
    if (!m_swap_chain || !m_rtv || !frame.texture || frame.format != DXGI_FORMAT_NV12)
        return PresentResult::Skipped;
    if (!ResizeIfNeeded()) return PresentResult::Skipped;
    const uint32_t vw = frame.visible_width ? frame.visible_width : frame.width;
    const uint32_t vh = frame.visible_height ? frame.visible_height : frame.height;
    const AspectRatioMode mode = m_mode.load(std::memory_order_relaxed);
    RECT src = ComputeFitSourceRect(frame.visible_x, frame.visible_y, vw, vh, frame.width, frame.height);
    RECT dst;
    if (mode == AspectRatioMode::Fill) {
        src = ComputeFillSourceRect(frame.visible_x, frame.visible_y, vw, vh,
                                    m_width, m_height, frame.width, frame.height);
        dst = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    } else if (mode == AspectRatioMode::AspectLocked || mode == AspectRatioMode::Stretch) {
        dst = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    } else {
        const int pp = m_pixel_perfect.load(std::memory_order_relaxed);
        if (pp == 1) {
            dst = ComputePixelPerfectDestRect(vw, vh, m_width, m_height);
        } else if (pp == 0) {
            if (vw == m_width && vh == m_height) {
                dst = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
            } else {
                dst = ComputeFitDestRect(vw, vh, m_width, m_height);
            }
        } else {
            dst = ComputeFitDestRect(vw, vh, m_width, m_height);
        }
    }
    RECT canvas{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    if (!ValidateBltGeometry(src, dst, canvas, frame.width, frame.height)) return PresentResult::Skipped;
    if (!skip_wait && m_waitable) ::WaitForSingleObjectEx(m_waitable, 100, TRUE);

    ShaderParams params{};
    params.uv_rect[0] = float(src.left) / frame.width;
    params.uv_rect[1] = float(src.top) / frame.height;
    params.uv_rect[2] = float(src.right) / frame.width;
    params.uv_rect[3] = float(src.bottom) / frame.height;
    params.coded_size[0] = static_cast<float>(frame.width);
    params.coded_size[1] = static_cast<float>(frame.height);
    params.coded_size[2] = 1.0f / frame.width;
    params.coded_size[3] = 1.0f / frame.height;
    params.adjust[0] = m_controls[0].load(std::memory_order_relaxed) / 200.0f;
    params.adjust[1] = 1.0f + m_controls[1].load(std::memory_order_relaxed) / 100.0f;
    params.adjust[2] = 1.0f + m_controls[2].load(std::memory_order_relaxed) / 100.0f;
    params.adjust[3] = m_controls[3].load(std::memory_order_relaxed) * 0.015708f;
    int range = m_range.load(std::memory_order_relaxed);
    int matrix = m_matrix.load(std::memory_order_relaxed);
    if (range == 0) range = frame.color_range ? frame.color_range : 1;
    if (matrix == 0) matrix = frame.color_matrix ? frame.color_matrix : (vh >= 720 ? 2 : 1);
    params.options[0] = static_cast<float>(range);
    params.options[1] = static_cast<float>(matrix);
    const int quality = m_quality.load(std::memory_order_relaxed);
    const bool upscale = (dst.right - dst.left) > (src.right - src.left) ||
                         (dst.bottom - dst.top) > (src.bottom - src.top);
    int sharp = m_controls[4].load(std::memory_order_relaxed);
    if (sharp == 0 && upscale && quality == 2) sharp = 20;
    params.options[2] = std::max(0, sharp) / 100.0f;
    params.options[3] = upscale && quality == 3 ? 4.0f : 0.0f;

    ID3D11ShaderResourceView* planes[2]{};
    frame.vp_begin_qpc = clock::MonotonicClock::NowQpcTicks();
    {
        std::lock_guard lock{m_device.ContextMutex()};
        if (!GetPlaneViews(frame.texture.Get(), frame.subresource, &planes[0], &planes[1]))
            return PresentResult::Skipped;
        auto* ctx = m_device.Context();
        constexpr float black[] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(m_rtv.Get(), black);
        ID3D11RenderTargetView* target = m_rtv.Get();
        ctx->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = static_cast<float>(dst.left);
        viewport.TopLeftY = static_cast<float>(dst.top);
        viewport.Width = static_cast<float>(dst.right - dst.left);
        viewport.Height = static_cast<float>(dst.bottom - dst.top);
        viewport.MinDepth = 0; viewport.MaxDepth = 1;
        ctx->RSSetViewports(1, &viewport);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs.Get(), nullptr, 0);
        ctx->PSSetShader(m_ps.Get(), nullptr, 0);
        ID3D11SamplerState* sampler = m_sampler.Get();
        ctx->PSSetSamplers(0, 1, &sampler);
        ctx->UpdateSubresource(m_constants.Get(), 0, nullptr, &params, 0, 0);
        ID3D11Buffer* constants = m_constants.Get();
        ctx->PSSetConstantBuffers(0, 1, &constants);
        ctx->PSSetShaderResources(0, 2, planes);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* empty[2]{};
        ctx->PSSetShaderResources(0, 2, empty);
    }
    frame.vp_end_qpc = clock::MonotonicClock::NowQpcTicks();
    frame.present_begin_qpc = frame.vp_end_qpc;
    HRESULT hr = m_swap_chain->Present(0, 0);
    frame.present_end_qpc = clock::MonotonicClock::NowQpcTicks();
    if (SUCCEEDED(hr)) {
        airplay::ConnectionTelemetry::Get().RecordPhase(
            airplay::ConnectionPhase::FirstPresentedFrame,
            "WARP rasterizer swap chain presented");
    }
    GlobalMetrics().vp_duration_avg_ms.store(
        clock::MonotonicClock::QpcDeltaMs(frame.vp_begin_qpc, frame.vp_end_qpc), std::memory_order_relaxed);
    GlobalMetrics().present_duration_avg_ms.store(
        clock::MonotonicClock::QpcDeltaMs(frame.present_begin_qpc, frame.present_end_qpc), std::memory_order_relaxed);
    if (frame.au_received_qpc > 0) {
        GlobalMetrics().total_pipeline_avg_ms.store(
            clock::MonotonicClock::QpcDeltaMs(frame.au_received_qpc, frame.present_end_qpc),
            std::memory_order_relaxed);
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) return PresentResult::DeviceLost;
    return SUCCEEDED(hr) ? PresentResult::Ok : PresentResult::Fatal;
}

} // namespace duwn::video
