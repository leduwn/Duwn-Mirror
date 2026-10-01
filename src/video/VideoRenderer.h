#pragma once
// VideoRenderer — presents VideoFrames to a HWND via IDXGISwapChain1.
// Input:  ID3D11Texture2D NV12 (from MF H.264 decoder)
// Output: BGRA to swap chain via D3D11 VideoProcessor (GPU colour conversion, no CPU copy)
//
// Threading
// ---------
// Present()      → FrameScheduler thread (render thread) — sole owner of all D3D resources
// SignalResize() → UI thread (WM_SIZE)  — only writes one atomic<uint64_t>
//
// Thread-safety
// -------------
// SignalResize() packs (w,h) into a single 64-bit atomic store.
// ApplyPendingResize() is called at the top of every Present() and is the ONLY
// place that calls ResizeBuffers — always on the render thread.
// No D3D resource is ever touched from the UI thread.
//
// RTV lifetime
// ------------
//   INIT:                CreateRTV() called once after CreateSwapChain.
//   WINDOW RESIZE:       ReleaseRTV() before ResizeBuffers; CreateRTV() after.
//   DEVICE RESET:        ReleaseRTV() as part of full teardown.
//   SOURCE SIZE CHANGE:  RTV not touched — back-buffer texture unchanged.
//   STEADY STATE:        0 CreateRenderTargetView calls per frame.
//
// OutputView lifetime
// -------------------
//   For D3D10/11 flip-discard swap chains, the application does NOT need to call
//   GetBuffer(0) again after every Present — DXGI handles buffer rotation
//   internally. The ID3D11Texture2D COM pointer returned by GetBuffer(0) is
//   valid for the lifetime of the swap-chain generation (i.e., until
//   ResizeBuffers or swap-chain destruction). OutputView is therefore valid as
//   long as:
//     (a) the swap chain generation hasn't changed (no ResizeBuffers), and
//     (b) the VP enumerator hasn't changed (no source size change).
//
//   INIT:                No VP yet — OutputView deferred to first source frame.
//   SOURCE SIZE CHANGE:  RebuildVideoProcessor() resets VP enumerator.
//                        CreateOutputView() called once after VP rebuild.
//                        RTV is NOT touched.
//   WINDOW RESIZE:       ReleaseOutputView() before ResizeBuffers (holds back-buffer ref).
//                        After ResizeBuffers: CreateRTV(), then if VP exists CreateOutputView().
//   DEVICE RESET:        Both released as part of full teardown.
//   STEADY STATE:        0 CreateVideoProcessorOutputView calls per frame.
//
// VideoProcessorInputView lifetime
// ---------------------------------
//   Per frame: each decoded VideoFrame carries a distinct texture/subresource.
//   Cannot be cached.

#include "VideoFrame.h"
#include "IVideoRenderer.h"
#include "D3D11Device.h"
#include "VideoGeometry.h"   // ComputeFitDestRect / ComputeFillSrcRect (pure math, no D3D)
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>       // IDXGISwapChain2 for frame-latency waitable object
#include <dxgi1_5.h>       // IDXGIFactory5 for tearing capability query
#include <wrl/client.h>
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <array>

namespace duwn::video {

using Microsoft::WRL::ComPtr;

// Return value of ApplyPendingResize() — distinct from PresentResult so
// device-lost during ResizeBuffers is not swallowed as Skipped.
enum class ResizeResult {
    NoResize,   // nothing to do
    Resized,    // resize completed successfully
    DeviceLost, // DXGI_ERROR_DEVICE_REMOVED / RESET during ResizeBuffers
    Failed,     // other resize failure
};

class VideoRenderer : public IVideoRenderer {
public:
    VideoRenderer(D3D11Device& device, HWND hwnd) noexcept;
    ~VideoRenderer();

    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    // Create swap chain and video device interfaces.
    bool Init(uint32_t width, uint32_t height) noexcept;

    // Present a decoded frame. Called on the render thread.
    // skip_wait: when true, indicates the render loop has already waited on the DXGI frame latency object.
    PresentResult Present(VideoFrame& frame, bool skip_wait = false) noexcept;
    PresentResult Present(const VideoFrame& frame) noexcept;

    // Clear the swap chain back buffer to black and present.
    // Call from the render thread when no stream is active (on disconnect).
    void PresentBlack() noexcept;

    // DXGI frame-latency waitable handle for the render presentation clock (Phase 6)
    HANDLE GetFrameLatencyWaitableObject() const noexcept { return m_frame_latency_waitable; }

    // Signal a resize from the UI thread (WM_SIZE). Purely atomic — no D3D.
    void SignalResize(uint32_t width, uint32_t height) noexcept;

    // Thread-safe AR-mode switch. Takes effect on next Present().
    void SetAspectRatioMode(AspectRatioMode mode) noexcept {
        m_ar_mode.store(mode, std::memory_order_relaxed);
    }
    void SetPixelPerfect(int mode) noexcept override {
        m_pixel_perfect.store(mode, std::memory_order_relaxed);
    }
    void SetColorControl(size_t index, int value) noexcept {
        if (index < m_filter_values.size()) {
            m_filter_values[index].store(value, std::memory_order_relaxed);
            m_filters_dirty.store(true, std::memory_order_release);
        }
    }
    bool SupportsColorControl(size_t index) const noexcept {
        constexpr uint32_t bits[] = {1u, 2u, 8u, 4u, 16u};
        return index < 5 && (m_filter_caps.load(std::memory_order_relaxed) & bits[index]) != 0;
    }
    uint32_t FilterCaps() const noexcept { return m_filter_caps.load(std::memory_order_relaxed); }
    void SetScalingQuality(int quality) noexcept {
        m_scaling_quality.store(quality, std::memory_order_relaxed);
        m_filters_dirty.store(true, std::memory_order_release);
    }
    void SetColorSpace(int range, int matrix) noexcept {
        m_color_range.store(range, std::memory_order_relaxed);
        m_color_matrix.store(matrix, std::memory_order_relaxed);
        m_color_space_dirty.store(true, std::memory_order_release);
    }
    AspectRatioMode GetAspectRatioMode() const noexcept {
        return m_ar_mode.load(std::memory_order_relaxed);
    }

    // Signal clean device-lost shutdown. Must be called from the render thread.
    // Milestone 0: releases all resources cleanly; returns false (no recovery).
    // Full device recovery (recreate ID3D11Device + decoder) is post-Milestone 0.
    bool HandleDeviceRemoved() noexcept;

    // Toggle between Present(0, 0) (app-controlled pacing with frame latency waitable object)
    // and Present(1, 0) (vsync-paced).
    void SetUsePresent00(bool use_00) noexcept {
        m_use_present_00.store(use_00, std::memory_order_relaxed);
    }
    bool IsUsingPresent00() const noexcept {
        return m_use_present_00.load(std::memory_order_relaxed);
    }

    void SetNonBlocking(bool non_blocking) noexcept override {
        m_non_blocking.store(non_blocking, std::memory_order_relaxed);
    }
    bool IsNonBlocking() const noexcept override {
        return m_non_blocking.load(std::memory_order_relaxed);
    }

    uint32_t SwapWidth()  const noexcept override { return m_swap_width; }
    uint32_t SwapHeight() const noexcept override { return m_swap_height; }

    void LogSwapChainConfig(const char* label) const noexcept override;

private:
    static bool QueryTearingSupport() noexcept;
    bool CreateSwapChain(uint32_t width, uint32_t height) noexcept;

    // Create cached RTV from GetBuffer(0). Call after swap chain create or ResizeBuffers.
    // Requires context mutex. Asserts m_rtv is null (caller must ReleaseRTV first).
    bool CreateRTV() noexcept;
    void ReleaseRTV() noexcept;

    // Create cached OutputView from GetBuffer(0). Requires VP enumerator to exist.
    // Call after RebuildVideoProcessor or after ResizeBuffers (if VP exists).
    // Requires context mutex. Asserts m_output_view is null.
    bool CreateOutputView() noexcept;
    void ReleaseOutputView() noexcept;

    // Rebuild VP enumerator + processor for new CODED source / swapchain dims.
    // Resets m_vp_enum, m_video_processor, m_output_view.
    // Then calls CreateOutputView() once.
    // Does NOT touch m_rtv.
    // Requires context mutex.
    bool RebuildVideoProcessor(uint32_t coded_src_w, uint32_t coded_src_h,
                               uint32_t dst_w,       uint32_t dst_h,
                               DXGI_FORMAT source_format) noexcept;
    void ApplyVideoFilters(bool upscaling) noexcept;
    void ApplyColorSpace(uint32_t source_height) noexcept;

    // Check pending resize and apply if needed. Render thread only.
    ResizeResult ApplyPendingResize() noexcept;

    D3D11Device& m_device;
    HWND         m_hwnd;
    bool         m_tearing_supported{false};

    std::atomic<AspectRatioMode> m_ar_mode{AspectRatioMode::AspectLocked};
    std::atomic<int> m_pixel_perfect{0};
    std::array<std::atomic<int>, 5> m_filter_values{};
    std::array<D3D11_VIDEO_PROCESSOR_FILTER_RANGE, 5> m_filter_ranges{};
    std::atomic<uint32_t> m_filter_caps{0};
    std::atomic<bool> m_filters_dirty{true};
    std::atomic<int> m_scaling_quality{0};
    bool m_last_upscaling{false};
    std::atomic<int> m_color_range{0};
    std::atomic<int> m_color_matrix{0};
    std::atomic<int> m_source_color_range{0};
    std::atomic<int> m_source_color_matrix{0};
    std::atomic<bool> m_color_space_dirty{true};

    // Pending resize — UI thread writes, render thread reads+clears.
    // Packed: high32 = width, low32 = height. 0 = no pending resize.
    std::atomic<uint64_t> m_pending_resize{0};

    ComPtr<IDXGISwapChain1>                m_swap_chain;
    ComPtr<ID3D11VideoDevice>              m_video_device;
    ComPtr<ID3D11VideoContext>             m_video_context;
    ComPtr<ID3D11VideoProcessorEnumerator> m_vp_enum;
    ComPtr<ID3D11VideoProcessor>           m_video_processor;

    // Cached back-buffer views — distinct lifecycles (see header comment).
    ComPtr<ID3D11RenderTargetView>         m_rtv;          // created at init/resize, NOT at src change
    ComPtr<ID3D11VideoProcessorOutputView> m_output_view;  // created at src change/resize, NOT per-frame

    uint32_t m_swap_width{0};
    uint32_t m_swap_height{0};

    // Cached CODED source dimensions for Video Processor.
    // VP is rebuilt ONLY when coded dimensions or swapchain dimensions change.
    // Visible aperture changes do NOT rebuild VP.
    uint32_t m_src_coded_width{0};
    uint32_t m_src_coded_height{0};
    DXGI_FORMAT m_src_format{DXGI_FORMAT_UNKNOWN};
    uint64_t m_active_source_generation{0};

    // Frame-latency waitable object support
    HANDLE   m_frame_latency_waitable{nullptr};
    std::atomic<bool> m_use_present_00{true}; // Default to app-controlled pacing via Present(0,0) + waitable object
    std::atomic<bool> m_non_blocking{false};

    // Render-thread-only windowed duration samples for 1s percentiles
    std::vector<double> m_vp_samples;
    std::vector<double> m_present_samples;
    int64_t             m_last_render_stats_time_ns{0};

    bool m_vc1_warned{false}; // one-shot: log ID3D11VideoContext1 QI failure once
};

} // namespace duwn::video
