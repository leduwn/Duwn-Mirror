#include "VideoRenderer.h"
#include "VideoGeometry.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include "common/telemetry/ConnectionTelemetry.h"
#include "common/telemetry/LatencyTelemetry.h"
#include <algorithm>
#include <format>

namespace duwn::video {

// ---------------------------------------------------------------------------
bool VideoRenderer::QueryTearingSupport() noexcept {
    ComPtr<IDXGIFactory5> factory5;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory5)))) return false;
    BOOL allowed = FALSE;
    HRESULT hr = factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed));
    return SUCCEEDED(hr) && allowed;
}

VideoRenderer::VideoRenderer(D3D11Device& device, HWND hwnd) noexcept
    : m_device(device), m_hwnd(hwnd) {}

VideoRenderer::~VideoRenderer() {
    if (m_frame_latency_waitable) {
        ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
    }
    // Dependency order: views → VP → video interfaces → swap chain.
    m_output_view.Reset();
    m_rtv.Reset();
    m_vp_enum.Reset();
    m_video_processor.Reset();
    m_video_context.Reset();
    m_video_device.Reset();
    m_swap_chain.Reset();
}

// ---------------------------------------------------------------------------
bool VideoRenderer::Init(uint32_t width, uint32_t height) noexcept {
    m_tearing_supported = QueryTearingSupport();
    DUWN_LOG_INFOF("VideoRenderer", "Tearing support: {}",
        m_tearing_supported ? "yes" : "no");

    if (!CreateSwapChain(width, height)) return false;

    HRESULT hr = m_device.Device()->QueryInterface(IID_PPV_ARGS(&m_video_device));
    if (FAILED(hr)) {
        DUWN_LOG_ERROR("VideoRenderer", "ID3D11VideoDevice not available");
        return false;
    }
    hr = m_device.Context()->QueryInterface(IID_PPV_ARGS(&m_video_context));
    if (FAILED(hr)) {
        DUWN_LOG_ERROR("VideoRenderer", "ID3D11VideoContext not available");
        return false;
    }

    // RTV created once here. OutputView deferred — no VP enumerator yet.
    // OutputView is created in RebuildVideoProcessor() on first source frame.
    {
        std::lock_guard lock{m_device.ContextMutex()};
        if (!CreateRTV()) return false;
        // CreateOutputView() NOT called here — m_vp_enum is null.

        // Clear swapchain back buffer to black and present once immediately.
        // Ensures the window displays pure black from the start before any stream arrives.
        constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        m_device.Context()->ClearRenderTargetView(m_rtv.Get(), black);
        m_swap_chain->Present(0, 0);
    }

    DUWN_LOG_INFOF("VideoRenderer", "Initialized {}x{}", width, height);
    return true;
    // Creation count: 1× CreateRTV, 0× CreateOutputView
}

bool VideoRenderer::CreateSwapChain(uint32_t width, uint32_t height) noexcept {
    if (m_frame_latency_waitable) {
        ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    m_device.Device()->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    ComPtr<IDXGIAdapter> adapter;
    dxgi_device->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width       = width;
    desc.Height      = height;
    desc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc  = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags       = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearing_supported)
        desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HRESULT hr = factory->CreateSwapChainForHwnd(
        m_device.Device(), m_hwnd, &desc, nullptr, nullptr, m_swap_chain.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "CreateSwapChainForHwnd failed {:#010x}", static_cast<unsigned>(hr));
        return false;
    }
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Query IDXGISwapChain2 for frame-latency waitable object support
    ComPtr<IDXGISwapChain2> swap_chain2;
    hr = m_swap_chain.As(&swap_chain2);
    if (SUCCEEDED(hr) && swap_chain2) {
        hr = swap_chain2->SetMaximumFrameLatency(1);
        if (SUCCEEDED(hr)) {
            m_frame_latency_waitable = swap_chain2->GetFrameLatencyWaitableObject();
            if (m_frame_latency_waitable) {
                DUWN_LOG_INFO("VideoRenderer", "DXGI Frame-Latency waitable object acquired (max latency = 1)");
            } else {
                DUWN_LOG_WARN("VideoRenderer", "GetFrameLatencyWaitableObject returned null");
            }
        } else {
            DUWN_LOG_WARNF("VideoRenderer", "SetMaximumFrameLatency(1) failed hr={:#010x}", static_cast<unsigned>(hr));
        }
    } else {
        DUWN_LOG_WARN("VideoRenderer", "IDXGISwapChain2 interface not supported");
    }

    m_swap_width  = width;
    m_swap_height = height;
    return true;
}

// ---------------------------------------------------------------------------
// CreateRTV — build RTV from current back buffer.
// Precondition: m_rtv must be null. Caller must call ReleaseRTV() first.
// Requires context mutex.
// ---------------------------------------------------------------------------
bool VideoRenderer::CreateRTV() noexcept {
    // WRL ComPtr::GetAddressOf() asserts that the pointer is null in Debug builds.
    // We enforce this via precondition — caller MUST release before calling.
    // Use ReleaseAndGetAddressOf() only here, where the intent is explicit reset+create.
    // (In practice the caller has already called ReleaseRTV(); this is belt-and-suspenders.)
    ComPtr<ID3D11Texture2D> bb;
    HRESULT hr = m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=CreateRTV operation=GetBuffer(0) hr={:#010x} thread_id={}",
            static_cast<unsigned>(hr), ::GetCurrentThreadId());
        return false;
    }
    hr = m_device.Device()->CreateRenderTargetView(
        bb.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=CreateRTV operation=CreateRenderTargetView hr={:#010x} thread_id={}",
            static_cast<unsigned>(hr), ::GetCurrentThreadId());
        return false;
    }
    return true;
}

void VideoRenderer::ReleaseRTV() noexcept {
    m_rtv.Reset();
}

// ---------------------------------------------------------------------------
// CreateOutputView — build OutputView from current back buffer.
// Precondition: m_vp_enum must exist; m_output_view must be null.
// Requires context mutex.
// ---------------------------------------------------------------------------
bool VideoRenderer::CreateOutputView() noexcept {
    ComPtr<ID3D11Texture2D> bb;
    HRESULT hr = m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=CreateOutputView operation=GetBuffer(0) hr={:#010x} thread_id={}",
            static_cast<unsigned>(hr), ::GetCurrentThreadId());
        return false;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov_desc{};
    ov_desc.ViewDimension      = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ov_desc.Texture2D.MipSlice = 0;
    hr = m_video_device->CreateVideoProcessorOutputView(
        bb.Get(), m_vp_enum.Get(), &ov_desc, m_output_view.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=CreateOutputView operation=CreateVideoProcessorOutputView hr={:#010x} thread_id={}",
            static_cast<unsigned>(hr), ::GetCurrentThreadId());
        return false;
    }
    return true;
}

void VideoRenderer::ReleaseOutputView() noexcept {
    m_output_view.Reset();
}

// ---------------------------------------------------------------------------
// RebuildVideoProcessor
//
// Called when:
//   - First decoded frame arrives (m_src_width == 0)
//   - Source resolution or orientation changes
//
// Lifecycle for this event:
//   Reset VP enumerator, VP, output view (all depend on src/dst dims).
//   Create new VP enumerator + processor.
//   Create OutputView once (back buffer pointer still valid — no ResizeBuffers).
//   RTV is NOT touched (back buffer texture is unchanged).
//
// Creation count per source change:
//   0× CreateRTV
//   1× CreateOutputView
//
// Requires context mutex.
// ---------------------------------------------------------------------------
bool VideoRenderer::RebuildVideoProcessor(uint32_t coded_src_w, uint32_t coded_src_h,
                                          uint32_t dst_w,       uint32_t dst_h,
                                          DXGI_FORMAT source_format) noexcept {
    // Release resources that depend on VP enumerator.
    ReleaseOutputView();  // holds ref on back buffer, must go before VP reset
    m_vp_enum.Reset();
    m_video_processor.Reset();
    // m_rtv is intentionally NOT reset here.

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC vp_desc{};
    vp_desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    vp_desc.InputWidth       = coded_src_w;  // MUST be coded texture dims, NOT visible dims
    vp_desc.InputHeight      = coded_src_h;
    vp_desc.OutputWidth      = dst_w;
    vp_desc.OutputHeight     = dst_h;
    vp_desc.Usage            = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HRESULT hr = m_video_device->CreateVideoProcessorEnumerator(
        &vp_desc, m_vp_enum.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=RebuildVideoProcessor operation=CreateVideoProcessorEnumerator hr={:#010x} src={}x{} dst={}x{} thread_id={}",
            static_cast<unsigned>(hr), coded_src_w, coded_src_h, dst_w, dst_h, ::GetCurrentThreadId());
        return false;
    }

    UINT format_support = 0;
    hr = m_vp_enum->CheckVideoProcessorFormat(source_format, &format_support);
    if (FAILED(hr) || !(format_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
        DUWN_LOG_ERRORF("VideoRenderer", "VideoProcessor input format {} unsupported (hr={:#010x})",
            static_cast<unsigned>(source_format), static_cast<unsigned>(hr));
        return false;
    }

    hr = m_video_device->CreateVideoProcessor(
        m_vp_enum.Get(), 0, m_video_processor.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=RebuildVideoProcessor operation=CreateVideoProcessor hr={:#010x} src={}x{} dst={}x{} thread_id={}",
            static_cast<unsigned>(hr), coded_src_w, coded_src_h, dst_w, dst_h, ::GetCurrentThreadId());
        return false;
    }

    D3D11_VIDEO_PROCESSOR_CAPS caps{};
    uint32_t supported = 0;
    if (SUCCEEDED(m_vp_enum->GetVideoProcessorCaps(&caps)))
        supported = caps.FilterCaps;
    constexpr D3D11_VIDEO_PROCESSOR_FILTER filters[] = {
        D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS,
        D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST,
        D3D11_VIDEO_PROCESSOR_FILTER_SATURATION,
        D3D11_VIDEO_PROCESSOR_FILTER_HUE,
        D3D11_VIDEO_PROCESSOR_FILTER_EDGE_ENHANCEMENT
    };
    for (size_t i = 0; i < 5; ++i) {
        if ((supported & (1u << static_cast<unsigned>(filters[i]))) != 0 &&
            FAILED(m_vp_enum->GetVideoProcessorFilterRange(filters[i], &m_filter_ranges[i])))
            supported &= ~(1u << static_cast<unsigned>(filters[i]));
    }
    m_filter_caps.store(supported, std::memory_order_relaxed);
    m_filters_dirty.store(true, std::memory_order_release);

    m_color_space_dirty.store(true, std::memory_order_release);
    ApplyColorSpace(coded_src_h);

    m_src_coded_width  = coded_src_w;
    m_src_coded_height = coded_src_h;
    m_src_format       = source_format;

    // Create OutputView once — back buffer texture unchanged by VP rebuild.
    if (!CreateOutputView()) {
        DUWN_LOG_ERROR("VideoRenderer", "CreateOutputView failed after VP rebuild");
        return false;
    }

    DUWN_LOG_INFOF("VideoRenderer",
        "VideoProcessor rebuilt: coded_src={}x{} dst={}x{}", coded_src_w, coded_src_h, dst_w, dst_h);
    return true;
}

void VideoRenderer::ApplyVideoFilters(bool upscaling) noexcept {
    const bool changed = m_filters_dirty.exchange(false, std::memory_order_acq_rel);
    if (!changed && upscaling == m_last_upscaling) return;
    m_last_upscaling = upscaling;
    constexpr D3D11_VIDEO_PROCESSOR_FILTER filters[] = {
        D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS,
        D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST,
        D3D11_VIDEO_PROCESSOR_FILTER_SATURATION,
        D3D11_VIDEO_PROCESSOR_FILTER_HUE,
        D3D11_VIDEO_PROCESSOR_FILTER_EDGE_ENHANCEMENT
    };
    const uint32_t caps = m_filter_caps.load(std::memory_order_relaxed);
    for (size_t i = 0; i < 5; ++i) {
        if ((caps & (1u << static_cast<unsigned>(filters[i]))) == 0) continue;
        int value = std::clamp(m_filter_values[i].load(std::memory_order_relaxed), -100, 100);
        if (i == 4 && value == 0 && upscaling) {
            const int quality = m_scaling_quality.load(std::memory_order_relaxed);
            if (quality == 2 || quality == 3) value = 20;
        }
        const auto& range = m_filter_ranges[i];
        const int mapped = value < 0
            ? range.Default + (range.Default - range.Minimum) * value / 100
            : range.Default + (range.Maximum - range.Default) * value / 100;
        m_video_context->VideoProcessorSetStreamFilter(
            m_video_processor.Get(), 0, filters[i], value != 0, mapped);
    }
}

void VideoRenderer::ApplyColorSpace(uint32_t source_height) noexcept {
    if (!m_color_space_dirty.exchange(false, std::memory_order_acq_rel)) return;
    ComPtr<ID3D11VideoContext1> vc1;
    if (FAILED(m_video_context->QueryInterface(IID_PPV_ARGS(&vc1)))) {
        if (!m_vc1_warned) {
            DUWN_LOG_WARN("VideoRenderer", "ID3D11VideoContext1 unavailable — colour space not set explicitly");
            m_vc1_warned = true;
        }
        return;
    }
    int matrix = m_color_matrix.load(std::memory_order_relaxed);
    int range = m_color_range.load(std::memory_order_relaxed);
    if (matrix == 0) matrix = m_source_color_matrix.load(std::memory_order_relaxed);
    if (matrix == 0) matrix = source_height >= 720 ? 2 : 1;
    if (range == 0) range = m_source_color_range.load(std::memory_order_relaxed);
    const bool full = range == 2;
    DXGI_COLOR_SPACE_TYPE input = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    if (matrix == 1) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601
                                  : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    else if (matrix == 2) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
                                       : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    else if (matrix == 3) input = full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020
                                       : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
    vc1->VideoProcessorSetStreamColorSpace1(m_video_processor.Get(), 0, input);
    vc1->VideoProcessorSetOutputColorSpace1(m_video_processor.Get(),
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
}

// ---------------------------------------------------------------------------
void VideoRenderer::SignalResize(uint32_t width, uint32_t height) noexcept {
    if (width == 0 || height == 0) return; // minimised — ignore
    uint64_t packed = (static_cast<uint64_t>(width) << 32) | static_cast<uint64_t>(height);
    m_pending_resize.store(packed, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// ApplyPendingResize — called at the top of every Present(), render thread only.
//
// Lifecycle for window resize:
//   1. ReleaseOutputView — holds back-buffer ref, must go before ResizeBuffers
//   2. ReleaseRTV        — holds back-buffer ref, must go before ResizeBuffers
//   3. ResizeBuffers
//   4. CreateRTV         — 1× per resize
//   5. CreateOutputView  — 1× per resize (only if VP exists)
//   NO second CreateOutputView via RebuildVideoProcessor.
//
// Creation count per window resize:
//   1× CreateRTV
//   1× CreateOutputView (if VP exists)
// ---------------------------------------------------------------------------
ResizeResult VideoRenderer::ApplyPendingResize() noexcept {
    uint64_t packed = m_pending_resize.exchange(0, std::memory_order_relaxed);
    if (packed == 0) return ResizeResult::NoResize;

    uint32_t width  = static_cast<uint32_t>(packed >> 32);
    uint32_t height = static_cast<uint32_t>(packed & 0xFFFFFFFF);
    if (width == m_swap_width && height == m_swap_height) return ResizeResult::NoResize;

    std::lock_guard lock{m_device.ContextMutex()};

    // Release all outstanding back-buffer refs before ResizeBuffers.
    ReleaseOutputView();
    ReleaseRTV();

    UINT flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearing_supported)
        flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    HRESULT hr = m_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT removed_reason = m_device.Device()->GetDeviceRemovedReason();
            DUWN_LOG_ERRORF("VideoRenderer",
                "[FATAL] subsystem=VideoRenderer function=ApplyPendingResize operation=ResizeBuffers (DeviceLost) hr={:#010x} removed_reason={:#010x} target={}x{} thread_id={}",
                static_cast<unsigned>(hr), static_cast<unsigned>(removed_reason), width, height, ::GetCurrentThreadId());
            return ResizeResult::DeviceLost;
        }
        DUWN_LOG_WARNF("VideoRenderer",
            "subsystem=VideoRenderer function=ApplyPendingResize operation=ResizeBuffers (Transient Failure) hr={:#010x} target={}x{} thread_id={}",
            static_cast<unsigned>(hr), width, height, ::GetCurrentThreadId());
        return ResizeResult::Failed;
    }
    m_swap_width  = width;
    m_swap_height = height;

    // Recreate RTV for the new back buffer.
    if (!CreateRTV()) return ResizeResult::Failed;

    // Recreate OutputView if VP exists (src dims known).
    // Do NOT call RebuildVideoProcessor here — VP is still valid for old src dims.
    // VP content desc has OutputWidth/Height matching old swap dims; those need
    // updating too. Update them now by rebuilding the VP.
    // NOTE: This is the one case where ResizeBuffers AND VP rebuild both happen.
    // However, we do NOT call CreateRTV/CreateOutputView twice:
    //   - CreateRTV above: 1×
    //   - RebuildVideoProcessor below: resets output_view, then CreateOutputView: 1×
    //   - Total: 1× RTV, 1× OutputView
    if (m_src_coded_width > 0 && m_src_coded_height > 0) {
        // ReleaseOutputView was already called above; RebuildVideoProcessor
        // will call ReleaseOutputView again (idempotent on null) then CreateOutputView.
        if (!RebuildVideoProcessor(m_src_coded_width, m_src_coded_height,
                                   m_swap_width, m_swap_height, m_src_format))
            return ResizeResult::Failed;
    }
    // If no src dims yet: OutputView stays null, will be created on first source frame.

    DUWN_LOG_INFOF("VideoRenderer", "SwapChain resized to {}x{}", width, height);
    return ResizeResult::Resized;
}

// ---------------------------------------------------------------------------
void VideoRenderer::PresentBlack() noexcept {
    if (!m_swap_chain || !m_rtv) return;
    std::lock_guard lock{m_device.ContextMutex()};
    constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_device.Context()->ClearRenderTargetView(m_rtv.Get(), black);
    m_swap_chain->Present(0, 0);
}
void VideoRenderer::CopyBackBufferTo(ID3D11Texture2D* dst) noexcept {
    if (!dst || !m_swap_chain) return;
    std::lock_guard lock{m_device.ContextMutex()};
    ComPtr<ID3D11Texture2D> bb;
    if (SUCCEEDED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
        m_device.Context()->CopyResource(dst, bb.Get());
    }
}


// ---------------------------------------------------------------------------
PresentResult VideoRenderer::Present(const VideoFrame& frame) noexcept {
    VideoFrame copy = frame;
    return Present(copy, false);
}

PresentResult VideoRenderer::Present(VideoFrame& frame, bool skip_wait) noexcept {
    const bool is_non_blocking = m_non_blocking.load(std::memory_order_relaxed);

    if (is_non_blocking) {
        duwn::GlobalMetrics().preview_present_attempts.fetch_add(1, std::memory_order_relaxed);
    } else {
        duwn::GlobalMetrics().video_present_attempts.fetch_add(1, std::memory_order_relaxed);
    }

    auto record_skipped = [is_non_blocking]() noexcept -> PresentResult {
        if (is_non_blocking) {
            duwn::GlobalMetrics().preview_present_skipped.fetch_add(1, std::memory_order_relaxed);
            duwn::GlobalMetrics().preview_skips.fetch_add(1, std::memory_order_relaxed);
        } else {
            duwn::GlobalMetrics().video_present_skipped.fetch_add(1, std::memory_order_relaxed);
        }
        return PresentResult::Skipped;
    };

    auto record_error = [is_non_blocking](PresentResult res) noexcept -> PresentResult {
        if (is_non_blocking) {
            duwn::GlobalMetrics().preview_present_errors.fetch_add(1, std::memory_order_relaxed);
        } else {
            duwn::GlobalMetrics().video_present_errors.fetch_add(1, std::memory_order_relaxed);
        }
        return res;
    };

    if (!m_swap_chain || !m_video_device || !m_video_context)
        return record_skipped();
    if (!frame.texture || (frame.format != DXGI_FORMAT_NV12 && frame.format != DXGI_FORMAT_P010))
        return record_skipped();

    // --- 1. Apply pending window resize ---
    const ResizeResult rr = ApplyPendingResize();
    if (rr == ResizeResult::DeviceLost) return record_error(PresentResult::DeviceLost);
    if (rr == ResizeResult::Failed) {
        // Transient resize failure during interactive drag must NOT terminate app
        DUWN_LOG_WARN("VideoRenderer", "ApplyPendingResize transient failure — frame skipped");
        return record_skipped();
    }

    // --- 2. Rebuild VP ONLY when CODED texture dims change ---
    // If only visible aperture changes, do NOT rebuild VP (only source/dest rects change).
    // Swapchain buffers are NEVER resized on source dimension or generation changes.
    if (frame.width  != m_src_coded_width ||
        frame.height != m_src_coded_height || frame.format != m_src_format) {
        DUWN_LOG_INFOF("VideoRenderer",
            "Coded texture dims changed {}x{} -> {}x{} (gen={})",
            m_src_coded_width, m_src_coded_height,
            frame.width, frame.height, frame.format_generation);
        if (is_non_blocking) {
            std::unique_lock lock{m_device.ContextMutex(), std::try_to_lock};
            if (!lock.owns_lock()) return record_skipped();
            if (!RebuildVideoProcessor(frame.width, frame.height,
                                       m_swap_width, m_swap_height, frame.format))
                return record_skipped();
        } else {
            std::lock_guard lock{m_device.ContextMutex()};
            if (!RebuildVideoProcessor(frame.width, frame.height,
                                       m_swap_width, m_swap_height, frame.format))
                return record_skipped();
        }
    }
    m_active_source_generation = frame.format_generation;

    // m_output_view must exist now (RebuildVideoProcessor ensures it).
    if (!m_output_view) return record_skipped();

    // --- 3. Per-frame InputView (distinct texture/subresource each frame) ---
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv_desc{};
    iv_desc.FourCC               = 0;
    iv_desc.ViewDimension        = D3D11_VPIV_DIMENSION_TEXTURE2D;
    iv_desc.Texture2D.MipSlice   = 0;
    iv_desc.Texture2D.ArraySlice = frame.subresource;

    ComPtr<ID3D11VideoProcessorInputView> input_view;
    HRESULT hr = m_video_device->CreateVideoProcessorInputView(
        frame.texture.Get(), m_vp_enum.Get(), &iv_desc, input_view.GetAddressOf());
    if (FAILED(hr)) {
        DUWN_LOG_WARNF("VideoRenderer",
            "subsystem=VideoRenderer function=Present operation=CreateVideoProcessorInputView hr={:#010x} subresource={} thread_id={}",
            static_cast<unsigned>(hr), frame.subresource, ::GetCurrentThreadId());
        return record_skipped();
    }

    // --- 4. Compute rects ---
    // Fit / Fill aspect ratio uses visible_width / visible_height.
    // Source rect begins at visible_x / visible_y and is clamped to coded texture bounds.
    const AspectRatioMode mode = m_ar_mode.load(std::memory_order_relaxed);
    RECT src_rect, dest_rect;

    const uint32_t vw = (frame.visible_width > 0)  ? frame.visible_width  : frame.width;
    const uint32_t vh = (frame.visible_height > 0) ? frame.visible_height : frame.height;
    const uint32_t vx = frame.visible_x;
    const uint32_t vy = frame.visible_y;
    const bool matrix_changed = m_source_color_matrix.exchange(frame.color_matrix, std::memory_order_relaxed) != frame.color_matrix;
    const bool range_changed = m_source_color_range.exchange(frame.color_range, std::memory_order_relaxed) != frame.color_range;
    if (matrix_changed || range_changed)
        m_color_space_dirty.store(true, std::memory_order_release);

    if (mode == AspectRatioMode::Fill) {
        src_rect  = ComputeFillSourceRect(vx, vy, vw, vh,
                                          m_swap_width, m_swap_height,
                                          frame.width, frame.height);
        dest_rect = {0, 0,
            static_cast<LONG>(m_swap_width), static_cast<LONG>(m_swap_height)};
    } else if (mode == AspectRatioMode::Stretch) {
        // Stretch mode only: full canvas mapped with no bars
        src_rect  = ComputeFitSourceRect(vx, vy, vw, vh,
                                         frame.width, frame.height);
        dest_rect = {0, 0,
            static_cast<LONG>(m_swap_width), static_cast<LONG>(m_swap_height)};
    } else {
        // Fit mode or AspectLocked: fit within canvas with aspect preservation (letterbox/pillarbox)
        src_rect  = ComputeFitSourceRect(vx, vy, vw, vh,
                                         frame.width, frame.height);
        const int pp = m_pixel_perfect.load(std::memory_order_relaxed);
        if (pp == 1 && mode != AspectRatioMode::AspectLocked) {
            // Pixel Perfect On: 1:1 pixel mapping centered if fits canvas, else fit
            dest_rect = ComputePixelPerfectDestRect(vw, vh, m_swap_width, m_swap_height);
        } else if (pp == 0 && mode != AspectRatioMode::AspectLocked) {
            // Pixel Perfect Auto: 1:1 if source matches swapchain dims, else single hardware fit
            if (vw == m_swap_width && vh == m_swap_height) {
                dest_rect = {0, 0, static_cast<LONG>(m_swap_width), static_cast<LONG>(m_swap_height)};
            } else {
                dest_rect = ComputeFitDestRect(vw, vh, m_swap_width, m_swap_height);
            }
        } else {
            // Standard fit scale (preserves aspect ratio, fits within canvas)
            dest_rect = ComputeFitDestRect(vw, vh, m_swap_width, m_swap_height);
        }
    }
    RECT canvas_rect = {0, 0,
        static_cast<LONG>(m_swap_width), static_cast<LONG>(m_swap_height)};

    // Pre-Blt geometry validation: bounds, non-empty, and NV12 even alignment.
    if (!ValidateBltGeometry(src_rect, dest_rect, canvas_rect, frame.width, frame.height)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[INVALID_GEOMETRY] subsystem=VideoRenderer function=Present operation=ValidateBltGeometry "
            "src=({},{},{},{}) dst=({},{},{},{}) canvas=({},{},{},{}) tex={}x{} thread_id={}",
            src_rect.left, src_rect.top, src_rect.right, src_rect.bottom,
            dest_rect.left, dest_rect.top, dest_rect.right, dest_rect.bottom,
            canvas_rect.left, canvas_rect.top, canvas_rect.right, canvas_rect.bottom,
            frame.width, frame.height, ::GetCurrentThreadId());
        return record_skipped();
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable        = TRUE;
    stream.pInputSurface = input_view.Get();

    // --- 5. Wait for previous frame completion if using waitable swapchain ---
    // Do NOT hold ContextMutex while waiting.
    // If skip_wait is true, the caller (render thread) has already waited on the DXGI handle.
    if (!is_non_blocking && !skip_wait && m_frame_latency_waitable) {
        auto wait_start = std::chrono::steady_clock::now();
        DWORD wait_res = ::WaitForSingleObjectEx(m_frame_latency_waitable, 100, TRUE);
        auto wait_end = std::chrono::steady_clock::now();
        double wait_ms = std::chrono::duration<double, std::milli>(wait_end - wait_start).count();

        // Update rolling average and record anomaly if wait exceeds nominal frame duration (> 33ms)
        double cur_avg = duwn::GlobalMetrics().dxgi_wait_avg_ms.load(std::memory_order_relaxed);
        duwn::GlobalMetrics().dxgi_wait_avg_ms.store((cur_avg * 7.0 + wait_ms) / 8.0, std::memory_order_relaxed);
        if (wait_ms > 33.0) {
            DUWN_LOG_WARNF("VideoRenderer", "dxgi_wait_ms={:.2f}ms exceeds frame interval thread_id={}", wait_ms, ::GetCurrentThreadId());
        }

        if (wait_res == WAIT_TIMEOUT) {
            DUWN_LOG_WARNF("VideoRenderer", "FrameLatencyWaitableObject timeout (100ms) thread_id={}", ::GetCurrentThreadId());
        }
    }

    // --- 6. Render ---
    const int64_t vp_begin_qpc = clock::MonotonicClock::NowQpcTicks();
    {
        std::unique_lock lock{m_device.ContextMutex(), std::defer_lock};
        if (is_non_blocking) {
            if (!lock.try_lock()) {
                return record_skipped();
            }
        } else {
            lock.lock();
        }

        if (mode == AspectRatioMode::Fit && m_rtv) {
            constexpr float black[] = {0.f, 0.f, 0.f, 1.f};
            m_device.Context()->ClearRenderTargetView(m_rtv.Get(), black);
        }

        m_video_context->VideoProcessorSetStreamSourceRect(
            m_video_processor.Get(), 0, TRUE, &src_rect);
        m_video_context->VideoProcessorSetStreamDestRect(
            m_video_processor.Get(), 0, TRUE, &dest_rect);
        m_video_context->VideoProcessorSetOutputTargetRect(
            m_video_processor.Get(), TRUE, &canvas_rect);
        ApplyColorSpace(vh);

        const bool upscaling = (dest_rect.right - dest_rect.left) > (src_rect.right - src_rect.left)
            || (dest_rect.bottom - dest_rect.top) > (src_rect.bottom - src_rect.top);
        ApplyVideoFilters(upscaling);

        hr = m_video_context->VideoProcessorBlt(
            m_video_processor.Get(), m_output_view.Get(), 0, 1, &stream);
    }
    const int64_t vp_end_qpc = clock::MonotonicClock::NowQpcTicks();

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT removed_reason = m_device.Device()->GetDeviceRemovedReason();
            DUWN_LOG_ERRORF("VideoRenderer",
                "[FATAL] subsystem=VideoRenderer function=Present operation=VideoProcessorBlt (DeviceLost) hr={:#010x} removed_reason={:#010x} "
                "srcTexture={}x{} NV12 visible={}x{} swapchain={}x{} thread_id={}",
                static_cast<unsigned>(hr), static_cast<unsigned>(removed_reason),
                frame.width, frame.height, vw, vh, m_swap_width, m_swap_height, ::GetCurrentThreadId());
            return record_error(PresentResult::DeviceLost);
        }
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=Present operation=VideoProcessorBlt hr={:#010x} "
            "srcTexture={}x{} NV12 visible={}x{} swapchain={}x{} thread_id={}",
            static_cast<unsigned>(hr), frame.width, frame.height, vw, vh, m_swap_width, m_swap_height, ::GetCurrentThreadId());
        return record_error(PresentResult::Fatal);
    }

    // --- 7. Present ---
    const int64_t present_begin_qpc = vp_end_qpc;
    const UINT present_flags = is_non_blocking ? DXGI_PRESENT_DO_NOT_WAIT : 0;
    hr = m_swap_chain->Present(0, present_flags);
    const int64_t present_end_qpc = clock::MonotonicClock::NowQpcTicks();

    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
        return record_skipped();
    }

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT removed_reason = m_device.Device()->GetDeviceRemovedReason();
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=Present operation=Present (DeviceLost) hr={:#010x} removed_reason={:#010x} swapchain={}x{} thread_id={}",
            static_cast<unsigned>(hr), static_cast<unsigned>(removed_reason), m_swap_width, m_swap_height, ::GetCurrentThreadId());
        return record_error(PresentResult::DeviceLost);
    }
    if (FAILED(hr)) {
        DUWN_LOG_ERRORF("VideoRenderer",
            "[FATAL] subsystem=VideoRenderer function=Present operation=Present hr={:#010x} swapchain={}x{} thread_id={}",
            static_cast<unsigned>(hr), m_swap_width, m_swap_height, ::GetCurrentThreadId());
        return record_error(PresentResult::Fatal);
    }

    // --- 8. Telemetry and Timestamps isolation ---
    if (is_non_blocking) {
        // Preview presentation successful: update preview-only metrics, do NOT touch frame or Output metrics
        duwn::GlobalMetrics().preview_present_ok.fetch_add(1, std::memory_order_relaxed);
        double prev_vp_ms = clock::MonotonicClock::QpcDeltaMs(vp_begin_qpc, vp_end_qpc);
        double prev_pres_ms = clock::MonotonicClock::QpcDeltaMs(present_begin_qpc, present_end_qpc);
        duwn::GlobalMetrics().preview_vp_duration_avg_ms.store(prev_vp_ms, std::memory_order_relaxed);
        duwn::GlobalMetrics().preview_present_duration_avg_ms.store(prev_pres_ms, std::memory_order_relaxed);
        return PresentResult::Ok;
    }

    // Primary Output presentation successful:
    // Update frame QPC timestamps exclusively for primary output
    frame.vp_begin_qpc      = vp_begin_qpc;
    frame.vp_end_qpc        = vp_end_qpc;
    frame.present_begin_qpc = present_begin_qpc;
    frame.present_end_qpc   = present_end_qpc;

    duwn::GlobalMetrics().video_present_ok.fetch_add(1, std::memory_order_relaxed);

    airplay::ConnectionTelemetry::Get().RecordPhase(
        airplay::ConnectionPhase::FirstPresentedFrame,
        "Zero-copy DXGI swap chain presented");

    // Record 8-stage latency telemetry (T0-T7) for primary capture output
    telemetry::LatencyStageTimestamps stages;
    stages.t0_rtp_arrival    = frame.rtp_arrival_qpc;
    stages.t1_au_assembled   = frame.au_received_qpc;
    stages.t2_decoder_input  = frame.process_input_qpc;
    stages.t3_decoder_output = frame.process_output_qpc;
    stages.t4_queue_inserted = frame.queue_push_qpc;
    stages.t5_frame_selected = frame.queue_pop_qpc;
    stages.t6_vp_completed   = frame.vp_end_qpc;
    stages.t7_present_invoked= frame.present_end_qpc;
    telemetry::LatencyTelemetry::Get().RecordFrameStages(stages);

    const double vp_ms = clock::MonotonicClock::QpcDeltaMs(frame.vp_begin_qpc, frame.vp_end_qpc);
    const double pres_ms = clock::MonotonicClock::QpcDeltaMs(frame.present_begin_qpc, frame.present_end_qpc);
    m_vp_samples.push_back(vp_ms);
    m_present_samples.push_back(pres_ms);

    if (frame.au_received_qpc > 0) {
        double total_ms = clock::MonotonicClock::QpcDeltaMs(frame.au_received_qpc, frame.present_end_qpc);
        duwn::GlobalMetrics().total_pipeline_avg_ms.store(total_ms, std::memory_order_relaxed);
    }

    // 1-Hz windowed percentile calculation for VP and Present (Render-thread owned)
    const int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();
    if (m_last_render_stats_time_ns == 0) {
        m_last_render_stats_time_ns = now_ns;
    } else if (now_ns - m_last_render_stats_time_ns >= 1'000'000'000LL) {
        if (!m_vp_samples.empty()) {
            std::sort(m_vp_samples.begin(), m_vp_samples.end());
            double sum_vp = 0.0;
            for (double v : m_vp_samples) sum_vp += v;
            duwn::GlobalMetrics().vp_duration_avg_ms.store(sum_vp / static_cast<double>(m_vp_samples.size()), std::memory_order_relaxed);
            duwn::GlobalMetrics().vp_duration_p50_ms.store(m_vp_samples[m_vp_samples.size() / 2], std::memory_order_relaxed);
            size_t idx95 = static_cast<size_t>(static_cast<double>(m_vp_samples.size()) * 0.95);
            if (idx95 >= m_vp_samples.size()) idx95 = m_vp_samples.size() - 1;
            duwn::GlobalMetrics().vp_duration_p95_ms.store(m_vp_samples[idx95], std::memory_order_relaxed);
            duwn::GlobalMetrics().vp_sample_count.store(m_vp_samples.size(), std::memory_order_relaxed);
            m_vp_samples.clear();
        }
        if (!m_present_samples.empty()) {
            std::sort(m_present_samples.begin(), m_present_samples.end());
            double sum_pres = 0.0;
            for (double p : m_present_samples) sum_pres += p;
            duwn::GlobalMetrics().present_duration_avg_ms.store(sum_pres / static_cast<double>(m_present_samples.size()), std::memory_order_relaxed);
            duwn::GlobalMetrics().present_duration_p50_ms.store(m_present_samples[m_present_samples.size() / 2], std::memory_order_relaxed);
            size_t idx95 = static_cast<size_t>(static_cast<double>(m_present_samples.size()) * 0.95);
            if (idx95 >= m_present_samples.size()) idx95 = m_present_samples.size() - 1;
            duwn::GlobalMetrics().present_duration_p95_ms.store(m_present_samples[idx95], std::memory_order_relaxed);
            duwn::GlobalMetrics().present_sample_count.store(m_present_samples.size(), std::memory_order_relaxed);
            m_present_samples.clear();
        }
        m_last_render_stats_time_ns = now_ns;
    }

    return PresentResult::Ok;
}

// ---------------------------------------------------------------------------
// HandleDeviceRemoved — Milestone 0: clean teardown, no recovery.
//
// Full D3D11 device recovery requires recreating:
//   - ID3D11Device + ID3D11DeviceContext (owned by D3D11Device, not VideoRenderer)
//   - IMFDXGIDeviceManager + MF decoder resources (owned by VideoDecoder)
//   - All VideoRenderer resources (owned here)
//
// VideoRenderer cannot recreate the device — it doesn't own D3D11Device.
// Attempting to call Init() on the same D3D11Device after device removal would
// silently use a zombie device and produce incorrect/crashed results.
//
// Correct Milestone 0 behavior: release all renderer resources, return false
// so caller (App) can shut down cleanly.
// ---------------------------------------------------------------------------
bool VideoRenderer::HandleDeviceRemoved() noexcept {
    DUWN_LOG_ERROR("VideoRenderer",
        "Device removed — full recovery requires D3D11Device recreation. "
        "Milestone 0: clean shutdown.");

    if (m_frame_latency_waitable) {
        ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
    }

    // Release in dependency order.
    ReleaseOutputView();
    ReleaseRTV();
    m_vp_enum.Reset();
    m_video_processor.Reset();
    m_video_context.Reset();
    m_video_device.Reset();
    m_swap_chain.Reset();
    m_src_coded_width          = 0;
    m_src_coded_height         = 0;
    m_src_format               = DXGI_FORMAT_UNKNOWN;
    m_active_source_generation = 0;

    // Return false — caller must treat this as fatal for Milestone 0.
    return false;
}

void VideoRenderer::LogSwapChainConfig(const char* label) const noexcept {
    if (!m_swap_chain) {
        DUWN_LOG_WARNF("VideoRenderer", "[SWAPCHAIN] {} - SwapChain is null", label);
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 desc1{};
    m_swap_chain->GetDesc1(&desc1);

    UINT max_latency = 0;
    ComPtr<IDXGISwapChain2> swap_chain2;
    if (SUCCEEDED(m_swap_chain.As(&swap_chain2)) && swap_chain2) {
        swap_chain2->GetMaximumFrameLatency(&max_latency);
    }

    BOOL is_fullscreen = FALSE;
    m_swap_chain->GetFullscreenState(&is_fullscreen, nullptr);

    LONG_PTR style = m_hwnd ? ::GetWindowLongPtrW(m_hwnd, GWL_STYLE) : 0;
    LONG_PTR ex_style = m_hwnd ? ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) : 0;

    const char* swap_effect_str = "UNKNOWN";
    switch (desc1.SwapEffect) {
    case DXGI_SWAP_EFFECT_DISCARD: swap_effect_str = "DISCARD"; break;
    case DXGI_SWAP_EFFECT_SEQUENTIAL: swap_effect_str = "SEQUENTIAL"; break;
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: swap_effect_str = "FLIP_SEQUENTIAL"; break;
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: swap_effect_str = "FLIP_DISCARD"; break;
    }

    const char* scaling_str = "UNKNOWN";
    switch (desc1.Scaling) {
    case DXGI_SCALING_STRETCH: scaling_str = "STRETCH"; break;
    case DXGI_SCALING_NONE: scaling_str = "NONE"; break;
    case DXGI_SCALING_ASPECT_RATIO_STRETCH: scaling_str = "ASPECT_RATIO_STRETCH"; break;
    }

    DUWN_LOG_INFOF("VideoRenderer",
        "[SWAPCHAIN CONFIG] [{}] HWND={:p} SwapChain={:p} SwapEffect={} BufferCount={} Format={} Flags={:#x} Scaling={} "
        "WaitableObj={:p} MaxFrameLatency={} SyncInterval={} PresentFlags={:#x} Tearing={} Fullscreen={} Style={:#010x} ExStyle={:#010x}",
        label,
        static_cast<void*>(m_hwnd),
        static_cast<void*>(m_swap_chain.Get()),
        swap_effect_str,
        desc1.BufferCount,
        static_cast<int>(desc1.Format),
        desc1.Flags,
        scaling_str,
        static_cast<void*>(m_frame_latency_waitable),
        max_latency,
        (m_use_present_00.load(std::memory_order_relaxed) ? 0 : 1),
        (m_non_blocking.load(std::memory_order_relaxed) ? DXGI_PRESENT_DO_NOT_WAIT : 0),
        m_tearing_supported ? "true" : "false",
        is_fullscreen ? "true" : "false",
        static_cast<uint32_t>(style),
        static_cast<uint32_t>(ex_style)
    );
}

} // namespace duwn::video
