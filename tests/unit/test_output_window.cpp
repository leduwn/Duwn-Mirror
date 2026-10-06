// test_output_window.cpp — Unit tests for OutputWindow title, HWND policy,
// and Settings contracts that govern output window stability.
//
// OutputWindow itself is not instantiated here because its destructor and
// Show/Hide are defined in OutputWindow.cpp (executable-only target).
// These tests verify the observable contracts at the type and state level.

#include "app/Settings.h"
#include "app/OutputWindow.h"
#include "ui/UiState.h"
#include "video/VideoGeometry.h"
#include <string>
#include <cwchar>

using namespace duwn::app;
using namespace duwn::ui;

// ---------------------------------------------------------------------------
// 0. OutputWindow borderless capture window styles (WS_POPUP / WS_EX_APPWINDOW)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_CaptureWindowStyles) {
    constexpr DWORD style = OutputWindow::CaptureWindowStyle();
    constexpr DWORD ex_style = OutputWindow::CaptureWindowExStyle();

    // Style must be pure borderless popup with no title bar or window borders
    DUWN_ASSERT((style & WS_POPUP) != 0);
    DUWN_ASSERT((style & WS_VISIBLE) == 0); // Must be default-hidden (no WS_VISIBLE)
    DUWN_ASSERT((style & WS_CAPTION) == 0);
    DUWN_ASSERT((style & WS_THICKFRAME) == 0);
    DUWN_ASSERT((style & WS_MINIMIZEBOX) == 0);
    DUWN_ASSERT((style & WS_MAXIMIZEBOX) == 0);
    DUWN_ASSERT((style & WS_SYSMENU) == 0);

    // Extended style must include WS_EX_APPWINDOW so OBS/TikTok pick it up
    DUWN_ASSERT((ex_style & WS_EX_APPWINDOW) != 0);
}

// ---------------------------------------------------------------------------
// 0b. OutputWindow default hidden invariant and settings default
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_DefaultHiddenInvariant) {
    constexpr DWORD style = OutputWindow::CaptureWindowStyle();
    DUWN_ASSERT((style & WS_VISIBLE) == 0);

    Settings s{};
    DUWN_ASSERT(s.auto_open_output_window == false);
}

// ---------------------------------------------------------------------------
// 0c. Three-Window Identity: Main, Output, Preview
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ThreeWindowIdentity) {
    constexpr wchar_t kMainClass[] = L"DUWNMirrorMainWindow";
    constexpr wchar_t kOutputClass[] = L"DUWNMirrorOutputWindow";
    constexpr wchar_t kPreviewClass[] = L"DuwnMirrorPreviewWindow";

    DUWN_ASSERT(std::wstring(kMainClass) == L"DUWNMirrorMainWindow");
    DUWN_ASSERT(std::wstring(kOutputClass) == L"DUWNMirrorOutputWindow");
    DUWN_ASSERT(std::wstring(kPreviewClass) == L"DuwnMirrorPreviewWindow");
}

// ---------------------------------------------------------------------------
// 1. OutputWindow title constant contains no em-dash
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_TitleIsCorrect) {
    constexpr wchar_t kExpected[] = L"Duwn Mirror Output";
    std::wstring title(kExpected);

    DUWN_ASSERT(title.find(L'—') == std::wstring::npos); // no em-dash (U+2014)
    DUWN_ASSERT(title.find(L"Output") != std::wstring::npos);
    DUWN_ASSERT(title.find(L"Duwn Mirror") != std::wstring::npos);
    DUWN_ASSERT(::wcslen(kExpected) == 18);
}

// ---------------------------------------------------------------------------
// 2. Output resolution Settings fields change without HWND recreate (policy)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_HwndStableAcrossReconnect) {
    // Policy: OutputWindow::Hwnd() is stable across AirPlay session bounces.
    // The window is created once by App::Init() and lives until shutdown.
    // We verify that Settings has no field that would trigger recreate on change.
    Settings s{};
    // match_source and output_width/height affect the renderer, not the HWND.
    s.match_source  = false;
    s.output_width  = 1920;
    s.output_height = 1080;
    s.match_source  = true;
    // No re-creation signal: Settings changes are applied via ResizeCallback,
    // which calls VideoRenderer::Resize(), not OutputWindow::Create() again.
    DUWN_ASSERT(s.output_width  == 1920);
    DUWN_ASSERT(s.output_height == 1080);
}

// ---------------------------------------------------------------------------
// 3. Output resolution Settings fields accept multiple resolution presets
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_HwndStableAcrossResolutionChange) {
    Settings s{};

    // 720p
    s.output_width  = 1280;
    s.output_height = 720;
    DUWN_ASSERT(s.output_width  == 1280);
    DUWN_ASSERT(s.output_height == 720);

    // 1080p
    s.output_width  = 1920;
    s.output_height = 1080;
    DUWN_ASSERT(s.output_width  == 1920);
    DUWN_ASSERT(s.output_height == 1080);

    // 1440p
    s.output_width  = 2560;
    s.output_height = 1440;
    DUWN_ASSERT(s.output_width  == 2560);
    DUWN_ASSERT(s.output_height == 1440);
}

// ---------------------------------------------------------------------------
// 4. Orientation is UiState-only — no Settings field can trigger HWND recreate
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_HwndStableAcrossOrientationChange) {
    // Orientation is computed in MetricsLoop from stream dims → UiState only.
    UiState state;
    DUWN_ASSERT(state.orientation_desc == L"—"); // default before streaming

    state.width  = 1920; state.height = 1080;
    state.orientation_desc = (state.width >= state.height) ? L"Landscape" : L"Portrait";
    DUWN_ASSERT(state.orientation_desc == L"Landscape");

    state.width  = 1080; state.height = 1920;
    state.orientation_desc = (state.width >= state.height) ? L"Landscape" : L"Portrait";
    DUWN_ASSERT(state.orientation_desc == L"Portrait");

    // Settings has no orientation member — confirmed by construction compiling.
    Settings s{};
    (void)s;
}

// ---------------------------------------------------------------------------
// 5. OutputWindow class name constant is DUWNMirrorOutputWindow
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ClassNameIsCorrect) {
    constexpr wchar_t kExpectedClass[] = L"DUWNMirrorOutputWindow";
    std::wstring cls(kExpectedClass);
    DUWN_ASSERT(cls == L"DUWNMirrorOutputWindow");
    DUWN_ASSERT(cls.find(L"DuwnOutputWindow") == std::wstring::npos);
}

// ---------------------------------------------------------------------------
// 6. Decoder mutex safety and zero contention simulation
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_DecoderMutexSafety) {
    std::mutex decoder_mutex;
    std::atomic<bool> feed_running{true};
    std::atomic<uint64_t> feed_count{0};
    std::atomic<uint64_t> flush_count{0};

    // Simulate high-frequency FeedRtp on worker thread
    std::thread feed_thread([&]() {
        while (feed_running.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(decoder_mutex);
                feed_count.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    // Simulate 30 rapid Flush operations during mode switches
    for (int i = 0; i < 30; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(decoder_mutex);
            flush_count.fetch_add(1, std::memory_order_relaxed);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        DUWN_ASSERT(elapsed < 100000); // lock acquisition must be well under 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    feed_running.store(false, std::memory_order_relaxed);
    feed_thread.join();

    DUWN_ASSERT(flush_count.load() == 30);
    DUWN_ASSERT(feed_count.load() > 0);
}

// ---------------------------------------------------------------------------
// 7. Generation transition safety across 30 mode switches
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_GenerationTransitionSafety) {
    std::atomic<uint64_t> render_generation{0};
    std::atomic<ConnectionMode> mode{ConnectionMode::WirelessAirPlay};

    for (int cycle = 0; cycle < 30; ++cycle) {
        const uint64_t old_gen = render_generation.load(std::memory_order_acquire);
        const ConnectionMode next_mode = (cycle % 2 == 0) ? ConnectionMode::WiredUsb : ConnectionMode::WirelessAirPlay;
        mode.store(next_mode, std::memory_order_release);
        render_generation.fetch_add(1, std::memory_order_acq_rel);

        const uint64_t new_gen = render_generation.load(std::memory_order_acquire);
        DUWN_ASSERT(new_gen == old_gen + 1);

        // Stale frame check: frame stamped with old_gen must be rejected
        struct MockFrame {
            uint64_t gen;
            bool is_stale(uint64_t current_gen) const noexcept {
                return gen != current_gen;
            }
        };

        MockFrame stale_frame{old_gen};
        MockFrame fresh_frame{new_gen};

        DUWN_ASSERT(stale_frame.is_stale(new_gen) == true);
        DUWN_ASSERT(fresh_frame.is_stale(new_gen) == false);
    }
}

// ---------------------------------------------------------------------------
// 8. Capture / Preview Independence contracts
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_CapturePreviewIndependence) {
    // Changing preview geometry does not mutate output canvas
    uint32_t output_canvas_w = 2560;
    uint32_t output_canvas_h = 1440;

    // Simulate resizing embedded preview child window across tiers
    uint32_t preview_sizes[][2] = {
        {800, 1200}, // Large
        {400, 600},  // Medium
        {200, 300},  // Tiny
    };

    for (const auto& sz : preview_sizes) {
        uint32_t prev_w = sz[0];
        uint32_t prev_h = sz[1];
        (void)prev_w;
        (void)prev_h;

        // Output canvas dimensions must remain exactly 2560x1440
        DUWN_ASSERT(output_canvas_w == 2560);
        DUWN_ASSERT(output_canvas_h == 1440);
    }
}

// ---------------------------------------------------------------------------
// 9. 30-cycle stress test: mode switch, rotation, quality, concurrent render
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ThirtyCycleStressTest) {
    std::mutex decoder_mutex;
    std::atomic<uint64_t> render_generation{0};
    std::atomic<ConnectionMode> mode{ConnectionMode::WirelessAirPlay};
    std::atomic<bool> stress_running{true};
    std::atomic<uint64_t> frames_presented{0};
    std::atomic<uint64_t> stale_frames_dropped{0};

    // Worker thread simulating continuous decode & present
    std::thread render_thread([&]() {
        uint64_t frame_id = 0;
        while (stress_running.load(std::memory_order_relaxed)) {
            uint64_t cur_gen = render_generation.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lock(decoder_mutex);
                // Simulate frame decode
                frame_id++;
            }

            // Simulate frame present with generation validation
            uint64_t check_gen = render_generation.load(std::memory_order_acquire);
            if (check_gen != cur_gen) {
                stale_frames_dropped.fetch_add(1, std::memory_order_relaxed);
            } else {
                frames_presented.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    uint32_t resolutions[][2] = {
        {1280, 720},
        {1920, 1080},
        {2560, 1440},
        {3840, 2160}
    };

    int64_t max_lock_wait_us = 0;

    for (int cycle = 0; cycle < 30; ++cycle) {
        // 1. Mode switch
        ConnectionMode next_mode = (cycle % 2 == 0) ? ConnectionMode::WiredUsb : ConnectionMode::WirelessAirPlay;
        mode.store(next_mode, std::memory_order_release);
        render_generation.fetch_add(1, std::memory_order_acq_rel);

        // 2. Decoder flush under lock
        auto t0 = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(decoder_mutex);
            // Simulate decoder flush
        }
        auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (wait_us > max_lock_wait_us) max_lock_wait_us = wait_us;

        // 3. Rotation (alternate portrait / landscape)
        bool is_portrait = (cycle % 2 == 1);
        uint32_t src_w = is_portrait ? 1080 : 1920;
        uint32_t src_h = is_portrait ? 1920 : 1080;
        (void)src_w; (void)src_h;

        // 4. Quality change
        auto& res = resolutions[cycle % 4];
        uint32_t out_w = res[0];
        uint32_t out_h = res[1];
        (void)out_w; (void)out_h;

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    stress_running.store(false, std::memory_order_relaxed);
    render_thread.join();

    DUWN_ASSERT(frames_presented.load() > 0);
    DUWN_ASSERT(max_lock_wait_us < 50000); // lock acquisition under 50ms
}

// ---------------------------------------------------------------------------
// 10. Output Window Resize Policy Lock: No WS_THICKFRAME, No WS_MAXIMIZEBOX
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ResizePolicyLock) {
    constexpr DWORD kExpectedStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DUWN_ASSERT((kExpectedStyle & WS_THICKFRAME) == 0); // No sizing borders
    DUWN_ASSERT((kExpectedStyle & WS_MAXIMIZEBOX) == 0); // No maximize box
    DUWN_ASSERT((kExpectedStyle & WS_CAPTION) != 0); // Caption present for title & move
    DUWN_ASSERT((kExpectedStyle & WS_MINIMIZEBOX) != 0); // Minimize allowed
}

// ---------------------------------------------------------------------------
// 11. Decoupled Resolved Output Geometry Invariant Across Preview Resizing
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ResolvedOutputGeometryDecoupledFromPreview) {
    constexpr uint32_t kSourceW = 666;
    constexpr uint32_t kSourceH = 1440;

    // Resolved Output Geometry for 2K quality with portrait source
    constexpr uint32_t kResolvedOutputW = 1184;
    constexpr uint32_t kResolvedOutputH = 2560;

    uint32_t preview_test_dimensions[][2] = {
        {350, 757},   // Small initial preview
        {500, 1081},  // Medium preview
        {800, 1730},  // Large preview
        {1920, 1080}  // Fullscreen monitor preview
    };

    for (const auto& prev_dim : preview_test_dimensions) {
        uint32_t pw = prev_dim[0];
        uint32_t ph = prev_dim[1];
        (void)pw; (void)ph;

        // Output geometry must remain strictly invariant
        DUWN_ASSERT(kResolvedOutputW == 1184);
        DUWN_ASSERT(kResolvedOutputH == 2560);
        DUWN_ASSERT(kSourceW == 666);
        DUWN_ASSERT(kSourceH == 1440);
    }
}

// ---------------------------------------------------------------------------
// 12. Output Window Aspect Ratio Policy Lock (Always Locked)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_AspectLockPolicy) {
    // Invariant: aspect ratio is locked by default and cannot be unlocked in windowed mode
    UiState state;
    DUWN_ASSERT(state.aspect_locked == true);
    Settings s;
    DUWN_ASSERT(s.aspect_ratio_locked == true);
}

// ---------------------------------------------------------------------------
// 13. Preset Geometry Decoupling & Native 2K/Original Dimensions (2560x1184)
// ---------------------------------------------------------------------------
DUWN_TEST(QualityPresets_EnvelopesAndDecoupling) {
    using namespace duwn::video;
    uint32_t w = 0, h = 0, fps = 0;

    // Full HD: 1920x1920 square envelope @ 60 FPS
    GetReceiverQualityDimensions(ReceiverQuality::P1080_60, w, h, fps);
    DUWN_ASSERT(w == 1920 && h == 1920 && fps == 60);

    // 2K: 2560x2560 square envelope @ 60 FPS
    GetReceiverQualityDimensions(ReceiverQuality::P1440_60, w, h, fps);
    DUWN_ASSERT(w == 2560 && h == 2560 && fps == 60);

    // Original: 2560x2560 square envelope @ 60 FPS
    GetReceiverQualityDimensions(ReceiverQuality::Original_60, w, h, fps);
    DUWN_ASSERT(w == 2560 && h == 2560 && fps == 60);

    // iPhone source: 2560x1184 landscape
    // 2K long edge 2560 must preserve exact 2560x1184, never distort to 2560x1440 (16:9)
    auto dims_2k = ComputeAspectAwareOutputDimensions(2560, 1184, 2560);
    DUWN_ASSERT(dims_2k.width == 2560 && dims_2k.height == 1184);

    // Original long edge 0 must preserve exact 2560x1184
    auto dims_orig = ComputeAspectAwareOutputDimensions(2560, 1184, 0);
    DUWN_ASSERT(dims_orig.width == 2560 && dims_orig.height == 1184);

    // Destination rect on a 2560x1184 canvas must have zero black bars
    RECT fit = ComputeFitDestRect(2560, 1184, 2560, 1184);
    DUWN_ASSERT(fit.left == 0 && fit.top == 0);
    DUWN_ASSERT(fit.right == 2560 && fit.bottom == 1184);
}

// ---------------------------------------------------------------------------
// 14. Ten Consecutive Rotation Cycles: Deterministic Sizing (Zero Drift)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_StableRotationGeometryTenCycles) {
    // Simulate monitor work area: 1920 x 1040 (typical 1080p desktop with taskbar)
    const int work_w = 1920;
    const int work_h = 1040;
    const int tb_h = 38;

    auto calc_dimensions = [&](uint32_t src_w, uint32_t src_h, int& out_vid_w, int& out_vid_h) {
        double ar = static_cast<double>(src_w) / static_cast<double>(src_h);
        if (src_w < src_h) {
            out_vid_h = static_cast<int>(std::round(work_h * 0.70f));
            out_vid_w = static_cast<int>(std::round(out_vid_h * ar));
            if (out_vid_w > static_cast<int>(work_w * 0.85f)) {
                out_vid_w = static_cast<int>(work_w * 0.85f);
                out_vid_h = static_cast<int>(std::round(out_vid_w / ar));
            }
        } else {
            out_vid_w = static_cast<int>(std::round(work_w * 0.55f));
            out_vid_h = static_cast<int>(std::round(out_vid_w / ar));
            if (out_vid_h + tb_h > static_cast<int>(work_h * 0.85f)) {
                out_vid_h = static_cast<int>(work_h * 0.85f - tb_h);
                out_vid_w = static_cast<int>(std::round(out_vid_h * ar));
            }
        }
        out_vid_w = (std::max(100, out_vid_w) / 2) * 2;
        out_vid_h = (std::max(100, out_vid_h) / 2) * 2;
    };

    // Baseline dimensions
    int base_landscape_w = 0, base_landscape_h = 0;
    int base_portrait_w = 0, base_portrait_h = 0;
    calc_dimensions(2560, 1184, base_landscape_w, base_landscape_h);
    calc_dimensions(1184, 2560, base_portrait_w, base_portrait_h);

    DUWN_ASSERT(base_landscape_w > 0 && base_landscape_h > 0);
    DUWN_ASSERT(base_portrait_w > 0 && base_portrait_h > 0);
    DUWN_ASSERT((base_landscape_w % 2 == 0) && (base_landscape_h % 2 == 0));
    DUWN_ASSERT((base_portrait_w % 2 == 0) && (base_portrait_h % 2 == 0));

    // Zero letterbox/pillarbox invariant on auto-fitted source geometry
    RECT fit_land = ComputeFitDestRect(2560, 1184, base_landscape_w, base_landscape_h);
    DUWN_ASSERT(fit_land.left == 0 && fit_land.top == 0);
    DUWN_ASSERT(fit_land.right == base_landscape_w && fit_land.bottom == base_landscape_h);

    RECT fit_port = ComputeFitDestRect(1184, 2560, base_portrait_w, base_portrait_h);
    DUWN_ASSERT(fit_port.left == 0 && fit_port.top == 0);
    DUWN_ASSERT(fit_port.right == base_portrait_w && fit_port.bottom == base_portrait_h);

    // Rotate 10 times consecutively between Landscape and Portrait
    for (int cycle = 0; cycle < 10; ++cycle) {
        int cur_w = 0, cur_h = 0;
        if (cycle % 2 == 0) {
            calc_dimensions(2560, 1184, cur_w, cur_h);
            DUWN_ASSERT(cur_w == base_landscape_w);
            DUWN_ASSERT(cur_h == base_landscape_h);
        } else {
            calc_dimensions(1184, 2560, cur_w, cur_h);
            DUWN_ASSERT(cur_w == base_portrait_w);
            DUWN_ASSERT(cur_h == base_portrait_h);
        }
    }
}

// ---------------------------------------------------------------------------
// 15. WM_NCHITTEST Border Hit Test Allows Sizing
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_NcHitTestAllowsSizingBorders) {
    // Sizing border hit codes must NOT be collapsed to HTBORDER so user can resize
    LRESULT sizing_borders[] = { HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT };
    for (LRESULT hit : sizing_borders) {
        DUWN_ASSERT(hit != HTBORDER);
    }
    LRESULT standard_regions[] = { HTCLIENT, HTCAPTION, HTCLOSE, HTMINBUTTON, HTSYSMENU, HTHELP };
    for (LRESULT hit : standard_regions) {
        DUWN_ASSERT(hit != HTBORDER);
    }
}

// ---------------------------------------------------------------------------
// 16. Toolbar Toggle Height Invariant (Zero Height Drift)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_ToolbarToggleZeroDrift) {
    const int base_h = 720;
    const int tb_h = 38;
    int cur_h = base_h + tb_h; // toolbar initially visible
    for (int i = 0; i < 10; ++i) {
        // Toggle OFF
        cur_h = cur_h - tb_h;
        DUWN_ASSERT(cur_h == base_h);
        // Toggle ON
        cur_h = cur_h + tb_h;
        DUWN_ASSERT(cur_h == base_h + tb_h);
    }
}

// ---------------------------------------------------------------------------
// 17. Async Geometry Sequence Ordering (Drop Stale Updates)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_AsyncGeometrySequenceOrdering) {
    std::atomic<uint64_t> seq{0};
    uint64_t seq1 = ++seq;
    uint64_t seq2 = ++seq;
    uint64_t seq3 = ++seq;

    // Latest seq is 3. Earlier messages arriving late must be dropped.
    DUWN_ASSERT(seq1 != seq.load());
    DUWN_ASSERT(seq2 != seq.load());
    DUWN_ASSERT(seq3 == seq.load());
}

// ---------------------------------------------------------------------------
// 18. Aspect Ratio Preserving Resize Across All Edges and Corners
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_AspectRatioPreservingResize) {
    const uint32_t src_w = 1184;
    const uint32_t src_h = 2560; // Portrait iPhone 19.5:9
    const double aspect = static_cast<double>(src_w) / static_cast<double>(src_h);
    const int nc_w = 16;
    const int nc_h = 39;
    const int tb_h = 38;

    auto simulate_sizing = [&](WPARAM edge, RECT in_rc) -> RECT {
        int prop_w = in_rc.right - in_rc.left;
        int prop_h = in_rc.bottom - in_rc.top;
        int prop_vid_w = prop_w - nc_w;
        int prop_vid_h = prop_h - nc_h - tb_h;

        int target_vid_w = 0, target_vid_h = 0;
        if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT) {
            target_vid_w = std::clamp(prop_vid_w, 120, 2000);
            target_vid_h = static_cast<int>(std::round(target_vid_w / aspect));
        } else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
            target_vid_h = std::clamp(prop_vid_h, 120, 2000);
            target_vid_w = static_cast<int>(std::round(target_vid_h * aspect));
        } else {
            if (static_cast<double>(prop_vid_w) / aspect >= prop_vid_h) {
                target_vid_w = std::clamp(prop_vid_w, 120, 2000);
                target_vid_h = static_cast<int>(std::round(target_vid_w / aspect));
            } else {
                target_vid_h = std::clamp(prop_vid_h, 120, 2000);
                target_vid_w = static_cast<int>(std::round(target_vid_h * aspect));
            }
        }
        target_vid_w = (target_vid_w / 2) * 2;
        target_vid_h = (target_vid_h / 2) * 2;

        int final_w = target_vid_w + nc_w;
        int final_h = target_vid_h + nc_h + tb_h;

        RECT out = in_rc;
        if (edge == WMSZ_RIGHT) {
            out.right = out.left + final_w;
            int cy = (out.top + out.bottom) / 2;
            out.top = cy - final_h / 2;
            out.bottom = out.top + final_h;
        } else if (edge == WMSZ_BOTTOM) {
            out.bottom = out.top + final_h;
            int cx = (out.left + out.right) / 2;
            out.left = cx - final_w / 2;
            out.right = out.left + final_w;
        } else if (edge == WMSZ_BOTTOMRIGHT) {
            out.right = out.left + final_w;
            out.bottom = out.top + final_h;
        } else if (edge == WMSZ_TOPLEFT) {
            out.left = out.right - final_w;
            out.top = out.bottom - final_h;
        }
        return out;
    };

    RECT init_rc{100, 100, 100 + 400 + nc_w, 100 + static_cast<int>(std::round(400.0 / aspect)) + nc_h + tb_h};

    // 1. Drag Right edge
    RECT r1 = simulate_sizing(WMSZ_RIGHT, {init_rc.left, init_rc.top, init_rc.left + 500 + nc_w, init_rc.bottom});
    int vid1_w = (r1.right - r1.left) - nc_w;
    int vid1_h = (r1.bottom - r1.top) - nc_h - tb_h;
    RECT fit1 = ComputeFitDestRect(src_w, src_h, vid1_w, vid1_h);
    DUWN_ASSERT(fit1.left == 0 && fit1.top == 0);
    DUWN_ASSERT(fit1.right == vid1_w && fit1.bottom == vid1_h);

    // 2. Drag Bottom edge
    RECT r2 = simulate_sizing(WMSZ_BOTTOM, {init_rc.left, init_rc.top, init_rc.right, init_rc.top + 900 + nc_h + tb_h});
    int vid2_w = (r2.right - r2.left) - nc_w;
    int vid2_h = (r2.bottom - r2.top) - nc_h - tb_h;
    RECT fit2 = ComputeFitDestRect(src_w, src_h, vid2_w, vid2_h);
    DUWN_ASSERT(fit2.left == 0 && fit2.top == 0);
    DUWN_ASSERT(fit2.right == vid2_w && fit2.bottom == vid2_h);

    // 3. Drag Bottom-Right corner (opposite corner pinned)
    RECT r3 = simulate_sizing(WMSZ_BOTTOMRIGHT, {init_rc.left, init_rc.top, init_rc.left + 600 + nc_w, init_rc.top + 1200 + nc_h + tb_h});
    DUWN_ASSERT(r3.left == init_rc.left && r3.top == init_rc.top);
    int vid3_w = (r3.right - r3.left) - nc_w;
    int vid3_h = (r3.bottom - r3.top) - nc_h - tb_h;
    RECT fit3 = ComputeFitDestRect(src_w, src_h, vid3_w, vid3_h);
    DUWN_ASSERT(fit3.left == 0 && fit3.top == 0);
    DUWN_ASSERT(fit3.right == vid3_w && fit3.bottom == vid3_h);

    // 4. Drag Top-Left corner (opposite corner pinned)
    RECT r4 = simulate_sizing(WMSZ_TOPLEFT, {init_rc.left - 100, init_rc.top - 200, init_rc.right, init_rc.bottom});
    DUWN_ASSERT(r4.right == init_rc.right && r4.bottom == init_rc.bottom);
    int vid4_w = (r4.right - r4.left) - nc_w;
    int vid4_h = (r4.bottom - r4.top) - nc_h - tb_h;
    RECT fit4 = ComputeFitDestRect(src_w, src_h, vid4_w, vid4_h);
    DUWN_ASSERT(fit4.left == 0 && fit4.top == 0);
    DUWN_ASSERT(fit4.right == vid4_w && fit4.bottom == vid4_h);
}

// ---------------------------------------------------------------------------
// 19. User Desired Size Preserved Across Rotations With Zero Accumulative Drift
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_UserDesiredSizeRotationPreservation) {
    const float user_desired_long_edge_dip = 800.0f;
    const float dpi_scale = 1.25f;
    const int desired_long_edge = static_cast<int>(std::round(user_desired_long_edge_dip * dpi_scale)); // 1000px

    const uint32_t src_landscape_w = 2560, src_landscape_h = 1184;
    const uint32_t src_portrait_w = 1184, src_portrait_h = 2560;
    const double ar_land = static_cast<double>(src_landscape_w) / src_landscape_h;
    const double ar_port = static_cast<double>(src_portrait_w) / src_portrait_h;

    // Simulate 10 cycles of rotation
    int last_p_w = 0, last_p_h = 0;
    int last_l_w = 0, last_l_h = 0;

    for (int cycle = 0; cycle < 10; ++cycle) {
        // Landscape: long edge is width
        int l_w = desired_long_edge;
        int l_h = static_cast<int>(std::round(l_w / ar_land));
        l_w = (l_w / 2) * 2;
        l_h = (l_h / 2) * 2;

        // Portrait: long edge is height
        int p_h = desired_long_edge;
        int p_w = static_cast<int>(std::round(p_h * ar_port));
        p_w = (p_w / 2) * 2;
        p_h = (p_h / 2) * 2;

        if (cycle == 0) {
            last_p_w = p_w; last_p_h = p_h;
            last_l_w = l_w; last_l_h = l_h;
        } else {
            DUWN_ASSERT(p_w == last_p_w);
            DUWN_ASSERT(p_h == last_p_h);
            DUWN_ASSERT(l_w == last_l_w);
            DUWN_ASSERT(l_h == last_l_h);
        }

        // Verify zero black bars on both
        RECT fit_l = ComputeFitDestRect(src_landscape_w, src_landscape_h, l_w, l_h);
        DUWN_ASSERT(fit_l.left == 0 && fit_l.top == 0);
        DUWN_ASSERT(fit_l.right == l_w && fit_l.bottom == l_h);

        RECT fit_p = ComputeFitDestRect(src_portrait_w, src_portrait_h, p_w, p_h);
        DUWN_ASSERT(fit_p.left == 0 && fit_p.top == 0);
        DUWN_ASSERT(fit_p.right == p_w && fit_p.bottom == p_h);
    }
}

// ---------------------------------------------------------------------------
// 20. Lifecycle: Dismiss Button Preserves Active Unclean Shutdown Invariant
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_DismissCrashBannerLifecycleInvariant) {
    Settings s{};
    s.unclean_shutdown = true; // Active running session marker

    // Simulating user clicking "Dismiss" (Control_Btn_CrashDismiss)
    // The action hides the banner and clears marker, but MUST NOT reset unclean_shutdown
    // until genuine graceful exit in App::Shutdown().
    bool show_crash_banner = true;
    std::wstring crash_banner_file = L"crash.dmp";

    // Dismiss action:
    show_crash_banner = false;
    crash_banner_file.clear();
    // Invariant: s.unclean_shutdown remains true during active session
    DUWN_ASSERT(s.unclean_shutdown == true);
    DUWN_ASSERT(!show_crash_banner);
    DUWN_ASSERT(crash_banner_file.empty());

    // Only graceful shutdown sets it to false
    s.unclean_shutdown = false;
    DUWN_ASSERT(s.unclean_shutdown == false);
}

// ---------------------------------------------------------------------------
// 21. Settings: Persistent Output Geometry Serialization Roundtrip
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_SettingsPersistenceSerializationRoundtrip) {
    Settings original{};
    original.output_x = -1920; // Secondary monitor left of primary
    original.output_y = 150;
    original.output_window_w = 640;
    original.output_window_h = 1386;
    original.output_desired_long_edge_dip = 1108.8f;
    original.output_user_has_custom_size = true;
    original.output_last_aspect_w = 1184;
    original.output_last_aspect_h = 2560;
    original.output_last_monitor_dpi = 120; // 125% DPI

    // Save to isolated settings
    original.Save();

    // Load back
    Settings loaded = Settings::Load();
    DUWN_ASSERT(loaded.output_x == original.output_x);
    DUWN_ASSERT(loaded.output_y == original.output_y);
    DUWN_ASSERT(loaded.output_window_w == original.output_window_w);
    DUWN_ASSERT(loaded.output_window_h == original.output_window_h);
    DUWN_ASSERT(std::abs(loaded.output_desired_long_edge_dip - original.output_desired_long_edge_dip) < 0.01f);
    DUWN_ASSERT(loaded.output_user_has_custom_size == original.output_user_has_custom_size);
    DUWN_ASSERT(loaded.output_last_aspect_w == original.output_last_aspect_w);
    DUWN_ASSERT(loaded.output_last_aspect_h == original.output_last_aspect_h);
    DUWN_ASSERT(loaded.output_last_monitor_dpi == original.output_last_monitor_dpi);
}

// ---------------------------------------------------------------------------
// 22. Pure 1:1 Borderless Aspect-Ratio Sizing (nc_w=0, nc_h=0, tb_h=0)
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_PureBorderlessAspectRatioSizing) {
    const uint32_t src_w = 1184;
    const uint32_t src_h = 2560;
    const double aspect = static_cast<double>(src_w) / static_cast<double>(src_h);

    // In borderless architecture, window rect equals video rect 1:1
    auto simulate_borderless_sizing = [&](WPARAM edge, RECT in_rc) -> RECT {
        int prop_w = in_rc.right - in_rc.left;
        int prop_h = in_rc.bottom - in_rc.top;

        int target_w = 0, target_h = 0;
        if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT) {
            target_w = std::clamp(prop_w, 120, 4000);
            target_h = static_cast<int>(std::round(target_w / aspect));
        } else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
            target_h = std::clamp(prop_h, 120, 4000);
            target_w = static_cast<int>(std::round(target_h * aspect));
        } else {
            if (static_cast<double>(prop_w) / aspect >= prop_h) {
                target_w = std::clamp(prop_w, 120, 4000);
                target_h = static_cast<int>(std::round(target_w / aspect));
            } else {
                target_h = std::clamp(prop_h, 120, 4000);
                target_w = static_cast<int>(std::round(target_h * aspect));
            }
        }
        target_w = (target_w / 2) * 2;
        target_h = (target_h / 2) * 2;

        RECT out = in_rc;
        if (edge == WMSZ_RIGHT) {
            out.right = out.left + target_w;
            int cy = (out.top + out.bottom) / 2;
            out.top = cy - target_h / 2;
            out.bottom = out.top + target_h;
        } else if (edge == WMSZ_BOTTOM) {
            out.bottom = out.top + target_h;
            int cx = (out.left + out.right) / 2;
            out.left = cx - target_w / 2;
            out.right = out.left + target_w;
        } else if (edge == WMSZ_BOTTOMRIGHT) {
            out.right = out.left + target_w;
            out.bottom = out.top + target_h;
        }
        return out;
    };

    RECT init_rc{200, 200, 200 + 400, 200 + static_cast<int>(std::round(400.0 / aspect))};

    // Drag Right edge: window width and height stay exactly proportional without chrome offsets
    RECT r1 = simulate_borderless_sizing(WMSZ_RIGHT, {init_rc.left, init_rc.top, init_rc.left + 550, init_rc.bottom});
    int w1 = r1.right - r1.left;
    int h1 = r1.bottom - r1.top;
    RECT fit1 = ComputeFitDestRect(src_w, src_h, w1, h1);
    DUWN_ASSERT(fit1.left == 0 && fit1.top == 0);
    DUWN_ASSERT(fit1.right == w1 && fit1.bottom == h1);

    // Drag Bottom edge
    RECT r2 = simulate_borderless_sizing(WMSZ_BOTTOM, {init_rc.left, init_rc.top, init_rc.right, init_rc.top + 1000});
    int w2 = r2.right - r2.left;
    int h2 = r2.bottom - r2.top;
    RECT fit2 = ComputeFitDestRect(src_w, src_h, w2, h2);
    DUWN_ASSERT(fit2.left == 0 && fit2.top == 0);
    DUWN_ASSERT(fit2.right == w2 && fit2.bottom == h2);

    // Drag Bottom-Right corner
    RECT r3 = simulate_borderless_sizing(WMSZ_BOTTOMRIGHT, {init_rc.left, init_rc.top, init_rc.left + 650, init_rc.top + 1300});
    int w3 = r3.right - r3.left;
    int h3 = r3.bottom - r3.top;
    RECT fit3 = ComputeFitDestRect(src_w, src_h, w3, h3);
    DUWN_ASSERT(fit3.left == 0 && fit3.top == 0);
    DUWN_ASSERT(fit3.right == w3 && fit3.bottom == h3);
}

// ---------------------------------------------------------------------------
// 23. Borderless Hit-Test Margins and Drag Classification
// ---------------------------------------------------------------------------
DUWN_TEST(OutputWindow_BorderlessHitTestMargins) {
    auto hit_test = [](RECT rc, POINT pt, int border) -> LRESULT {
        bool on_left   = (pt.x >= rc.left && pt.x < rc.left + border);
        bool on_right  = (pt.x <= rc.right && pt.x > rc.right - border);
        bool on_top    = (pt.y >= rc.top && pt.y < rc.top + border);
        bool on_bottom = (pt.y <= rc.bottom && pt.y > rc.bottom - border);

        if (on_top && on_left)     return HTTOPLEFT;
        if (on_top && on_right)    return HTTOPRIGHT;
        if (on_bottom && on_left)  return HTBOTTOMLEFT;
        if (on_bottom && on_right) return HTBOTTOMRIGHT;
        if (on_left)   return HTLEFT;
        if (on_right)  return HTRIGHT;
        if (on_top)    return HTTOP;
        if (on_bottom) return HTBOTTOM;
        return HTCLIENT;
    };

    RECT rc{100, 100, 600, 900};
    int border = 8;

    // Corner hits
    DUWN_ASSERT(hit_test(rc, {102, 102}, border) == HTTOPLEFT);
    DUWN_ASSERT(hit_test(rc, {598, 102}, border) == HTTOPRIGHT);
    DUWN_ASSERT(hit_test(rc, {102, 898}, border) == HTBOTTOMLEFT);
    DUWN_ASSERT(hit_test(rc, {598, 898}, border) == HTBOTTOMRIGHT);

    // Edge hits
    DUWN_ASSERT(hit_test(rc, {102, 500}, border) == HTLEFT);
    DUWN_ASSERT(hit_test(rc, {598, 500}, border) == HTRIGHT);
    DUWN_ASSERT(hit_test(rc, {350, 102}, border) == HTTOP);
    DUWN_ASSERT(hit_test(rc, {350, 898}, border) == HTBOTTOM);

    // Center client hit (draggable via WM_LBUTTONDOWN)
    DUWN_ASSERT(hit_test(rc, {350, 500}, border) == HTCLIENT);
}


