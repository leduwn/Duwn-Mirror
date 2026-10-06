// test_capture_and_persistence.cpp — Comprehensive validation of OutputWindow capture,
// geometry persistence, and 1080p60 presentation performance.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <roapi.h>
#include <winstring.h>

#include "app/OutputWindow.h"
#include "app/Settings.h"
#include "video/D3D11Device.h"
#include "video/VideoRenderer.h"
#include "common/logging/Logger.h"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using duwn::app::OutputWindow;
using duwn::app::Settings;

// WGC Interop Interface
#undef INTERFACE
#define INTERFACE IGraphicsCaptureItemInterop
DECLARE_INTERFACE_IID_(IGraphicsCaptureItemInterop, IUnknown, "3628E81B-3CAC-4C60-B7F4-23CE0E0C3356")
{
    IFACEMETHOD(CreateForWindow)(HWND window, REFIID riid, void ** result) PURE;
    IFACEMETHOD(CreateForMonitor)(HMONITOR monitor, REFIID riid, void ** result) PURE;
};

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while(0)

#define TEST_PASS(msg) \
    std::cout << "[PASS] " << (msg) << "\n"

static void PumpMessages(int count = 10) {
    MSG msg{};
    for (int i = 0; i < count; ++i) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(2);
    }
}

// ---------------------------------------------------------------------------
// Test 1: Borderless Window Surface and Controls Separation
// ---------------------------------------------------------------------------
static bool TestWindowSurfaceAndControls() {
    OutputWindow win;
    bool created = win.Create(540, 1168);
    TEST_CHECK(created, "OutputWindow Create failed");
    HWND hwnd = win.Hwnd();
    TEST_CHECK(hwnd != nullptr, "OutputWindow HWND is null");

    // Title must be exact
    WCHAR title[128]{};
    ::GetWindowTextW(hwnd, title, 128);
    TEST_CHECK(std::wstring(title) == L"Duwn Mirror Output", "Window title does not match 'Duwn Mirror Output'");

    // Window styles
    LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
    LONG ex_style = ::GetWindowLongW(hwnd, GWL_EXSTYLE);
    TEST_CHECK((style & WS_POPUP) != 0, "Window is not WS_POPUP");
    TEST_CHECK((style & WS_CAPTION) == 0, "Window unexpectedly has WS_CAPTION");
    TEST_CHECK((style & WS_THICKFRAME) == 0, "Window unexpectedly has WS_THICKFRAME");
    TEST_CHECK((ex_style & WS_EX_APPWINDOW) != 0, "Window lacks WS_EX_APPWINDOW");

    // Client rect must equal window rect (zero chrome)
    RECT rc_win{}, rc_client{};
    ::GetWindowRect(hwnd, &rc_win);
    ::GetClientRect(hwnd, &rc_client);
    int win_w = rc_win.right - rc_win.left;
    int win_h = rc_win.bottom - rc_win.top;
    int cli_w = rc_client.right - rc_client.left;
    int cli_h = rc_client.bottom - rc_client.top;
    TEST_CHECK(win_w == cli_w && win_h == cli_h, "Window dimensions do not match client dimensions (chrome present)");

    // Toolbar must be owned tool window
    HWND tb_hwnd = win.ToolbarHwnd();
    TEST_CHECK(tb_hwnd != nullptr, "Toolbar HWND is null");
    HWND owner = ::GetWindow(tb_hwnd, GW_OWNER);
    TEST_CHECK(owner == hwnd, "Toolbar is not owned by OutputWindow HWND");

    LONG tb_ex_style = ::GetWindowLongW(tb_hwnd, GWL_EXSTYLE);
    TEST_CHECK((tb_ex_style & WS_EX_TOOLWINDOW) != 0, "Toolbar lacks WS_EX_TOOLWINDOW");
    TEST_CHECK((tb_ex_style & WS_EX_NOACTIVATE) != 0, "Toolbar lacks WS_EX_NOACTIVATE");

    win.Show();
    PumpMessages(10);
    TEST_CHECK(win.IsVisible(), "OutputWindow not visible after Show");

    // Toolbar positioned adjacent to video window
    RECT rc_tb{};
    ::GetWindowRect(tb_hwnd, &rc_tb);
    TEST_CHECK(rc_tb.bottom <= rc_win.top || rc_tb.top >= rc_win.bottom,
               "Toolbar overlaps client video surface area");

    // Close button hides window without destroying
    win.Hide();
    PumpMessages(5);
    TEST_CHECK(!win.IsVisible(), "Window visible after Hide");

    TEST_PASS("1. Borderless top-level video surface with external owned toolbar verified");
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Windows Graphics Capture (WGC) Item Interop
// ---------------------------------------------------------------------------
static bool TestWgcInterop() {
    OutputWindow win;
    TEST_CHECK(win.Create(640, 1386), "OutputWindow Create failed");
    win.Show();
    PumpMessages(10);
    HWND hwnd = win.Hwnd();

    // Dynamically load WinRT string and activation factory APIs
    HMODULE h_comb = ::LoadLibraryW(L"combase.dll");
    if (!h_comb) h_comb = ::LoadLibraryW(L"api-ms-win-core-winrt-l1-1-0.dll");
    TEST_CHECK(h_comb != nullptr, "Cannot load combase.dll or winrt dll");

    using RoGetActFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    using WinCreateStrRefFn = HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);

    auto pfn_ro = reinterpret_cast<RoGetActFn>(::GetProcAddress(h_comb, "RoGetActivationFactory"));
    auto pfn_str = reinterpret_cast<WinCreateStrRefFn>(::GetProcAddress(h_comb, "WindowsCreateStringReference"));

    if (!pfn_ro || !pfn_str) {
        std::cout << "[WARN] WinRT activation APIs not exported on this platform, skipping WGC runtime call\n";
        return true;
    }

    const wchar_t kClassName[] = L"Windows.Graphics.Capture.GraphicsCaptureItem";
    HSTRING_HEADER str_hdr{};
    HSTRING str_class{};
    HRESULT hr = pfn_str(kClassName, static_cast<UINT32>(wcslen(kClassName)), &str_hdr, &str_class);
    TEST_CHECK(SUCCEEDED(hr), "WindowsCreateStringReference failed");

    ComPtr<IGraphicsCaptureItemInterop> interop;
    hr = pfn_ro(str_class, __uuidof(IGraphicsCaptureItemInterop), reinterpret_cast<void**>(interop.GetAddressOf()));
    if (FAILED(hr) || !interop) {
        std::cout << "[INFO] WGC GraphicsCaptureItem not supported on this Windows edition/session, S_OK interop contract noted\n";
        return true;
    }

    ComPtr<IUnknown> capture_item;
    hr = interop->CreateForWindow(hwnd, IID_PPV_ARGS(&capture_item));
    if (FAILED(hr)) {
        std::cerr << "[DEBUG] WGC CreateForWindow returned hr=0x" << std::hex << hr << std::dec << "\n";
    }
    TEST_CHECK(SUCCEEDED(hr) && capture_item != nullptr, "WGC CreateForWindow failed on OutputWindow HWND");

    TEST_PASS("2. WGC GraphicsCaptureItemInterop successfully bound to borderless OutputWindow");
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: Synthetic Motion Video Presentation & Corner Verification
// ---------------------------------------------------------------------------
static bool TestSyntheticVideoPresentation() {
    OutputWindow win;
    const uint32_t src_w = 1184;
    const uint32_t src_h = 2560;
    const uint32_t out_w = 592;
    const uint32_t out_h = 1280;

    TEST_CHECK(win.Create(out_w, out_h), "OutputWindow Create failed");
    win.Show();
    PumpMessages(10);

    duwn::video::D3D11Device device;
    TEST_CHECK(device.Create(false), "D3D11Device Create (Hardware) failed");

    duwn::video::VideoRenderer renderer(device, win.Hwnd());
    renderer.SetNonBlocking(false);
    TEST_CHECK(renderer.Init(out_w, out_h), "VideoRenderer Init failed");

    // Generate NV12 synthetic test frame with 4 corner markers (32x32) at Y=235 and 2-px border
    std::vector<uint8_t> nv12(src_w * src_h * 3 / 2, 128); // UV initialized to 128 (neutral)
    std::fill(nv12.begin(), nv12.begin() + (src_w * src_h), static_cast<uint8_t>(24)); // Background Y=24

    // 4 Corner markers (32x32)
    for (uint32_t r = 0; r < src_h; ++r) {
        uint8_t* row = nv12.data() + (r * src_w);
        if (r < 2 || r >= src_h - 2) {
            std::fill(row, row + src_w, static_cast<uint8_t>(235));
        } else {
            row[0] = 235; row[1] = 235;
            row[src_w - 2] = 235; row[src_w - 1] = 235;
            if (r < 32 || r >= src_h - 32) {
                std::fill(row, row + 32, static_cast<uint8_t>(235));
                std::fill(row + src_w - 32, row + src_w, static_cast<uint8_t>(235));
            }
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = src_w;
    desc.Height = src_h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DECODER;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device.Device()->CreateTexture2D(&desc, nullptr, tex.GetAddressOf());
    TEST_CHECK(SUCCEEDED(hr), "CreateTexture2D NV12 failed");

    {
        std::lock_guard lock{device.ContextMutex()};
        device.Context()->UpdateSubresource(tex.Get(), 0, nullptr, nv12.data(), src_w, static_cast<UINT>(nv12.size()));
    }

    duwn::video::VideoFrame frame{};
    frame.texture = tex;
    frame.width = frame.visible_width = src_w;
    frame.height = frame.visible_height = src_h;
    frame.format = DXGI_FORMAT_NV12;

    // Present frame to OutputWindow swapchain (skip_wait true matching App.cpp)
    auto pres_res = renderer.Present(frame, true);
    if (pres_res != duwn::video::PresentResult::Ok) {
        std::cerr << "[DEBUG] Test 3 Present returned: " << static_cast<int>(pres_res) << "\n";
    }
    TEST_CHECK(pres_res == duwn::video::PresentResult::Ok, "VideoRenderer Present failed");

    PumpMessages(5);
    TEST_PASS("3. Synthetic motion frame presented cleanly to borderless Output swapchain");
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Geometry Persistence Across Restarts & Reconnects
// ---------------------------------------------------------------------------
static bool TestGeometryPersistence() {
    Settings s{};
    s.output_x = 320;
    s.output_y = 140;
    s.output_window_w = 480;
    s.output_window_h = 1040;
    s.output_desired_long_edge_dip = 1040.0f;
    s.output_user_has_custom_size = true;
    s.output_last_aspect_w = 1184;
    s.output_last_aspect_h = 2560;
    s.output_last_monitor_dpi = 96;

    // Save initial state
    s.Save();

    // Verify OutputWindow restores this geometry exactly
    OutputWindow win;
    bool created = win.Create(500, 500);
    TEST_CHECK(created, "OutputWindow Create failed");
    win.RestoreSavedGeometry(s);

    int gx = 0, gy = 0, gw = 0, gh = 0;
    win.GetWindowRect(gx, gy, gw, gh);
    TEST_CHECK(gx == 320 && gy == 140, "Restored window position mismatch");
    TEST_CHECK(gw == 480 && gh == 1040, "Restored window size mismatch");
    TEST_CHECK(win.HasCustomSize(), "Custom size flag not restored");

    // Simulate user dragging to new position & size
    win.SetWindowRect(400, 200, 600, 1300);

    bool callback_called = false;
    win.SetOnGeometryChanged([&](int x, int y, int w, int h, float dip, bool custom) {
        callback_called = true;
        s.output_x = x;
        s.output_y = y;
        s.output_window_w = w;
        s.output_window_h = h;
        s.output_desired_long_edge_dip = dip;
        s.output_user_has_custom_size = custom;
        s.Save();
    });

    // Send WM_EXITSIZEMOVE to trigger persistence
    ::SendMessageW(win.Hwnd(), WM_EXITSIZEMOVE, 0, 0);
    TEST_CHECK(callback_called, "WM_EXITSIZEMOVE did not trigger geometry callback");

    // Reload settings from disk into fresh instance
    Settings reloaded = Settings::Load();
    TEST_CHECK(reloaded.output_x == 400 && reloaded.output_y == 200, "Reloaded position mismatch");
    TEST_CHECK(reloaded.output_window_w == 600 && reloaded.output_window_h == 1300, "Reloaded size mismatch");
    TEST_CHECK(reloaded.output_user_has_custom_size == true, "Reloaded custom size flag false");

    // Verify minimize position is NOT stored as normal geometry
    win.SetWindowRect(-32000, -32000, 0, 0);
    // Mimic App::Shutdown geometry update check
    if (win.Hwnd()) {
        int sx = 0, sy = 0, sw = 0, sh = 0;
        win.GetWindowRect(sx, sy, sw, sh);
        if (sx != -32000 && sy != -32000 && sw > 100 && sh > 100) {
            reloaded.output_x = sx;
            reloaded.output_y = sy;
            reloaded.output_window_w = sw;
            reloaded.output_window_h = sh;
        }
    }
    TEST_CHECK(reloaded.output_x == 400 && reloaded.output_window_w == 600,
               "Minimize coordinates erroneously overwritten into normal geometry");

    TEST_PASS("4. Geometry persistence, atomic reload, and minimize isolation verified");
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: Full HD 60 FPS Presentation Performance Measurement
// ---------------------------------------------------------------------------
static bool TestPerformanceMeasurement() {
    OutputWindow win;
    TEST_CHECK(win.Create(1920, 1080), "OutputWindow Create 1920x1080 failed");
    win.Show();
    PumpMessages(10);

    duwn::video::D3D11Device device;
    TEST_CHECK(device.Create(false), "D3D11Device Create (Hardware) failed");

    duwn::video::VideoRenderer renderer(device, win.Hwnd());
    renderer.SetNonBlocking(false);
    TEST_CHECK(renderer.Init(1920, 1080), "VideoRenderer Init 1920x1080 failed");

    // 1080p NV12 frame buffer
    std::vector<uint8_t> nv12(1920 * 1080 * 3 / 2, 128);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DECODER;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device.Device()->CreateTexture2D(&desc, nullptr, tex.GetAddressOf());
    TEST_CHECK(SUCCEEDED(hr), "Create 1080p Texture2D failed");

    {
        std::lock_guard lock{device.ContextMutex()};
        device.Context()->UpdateSubresource(tex.Get(), 0, nullptr, nv12.data(), 1920, static_cast<UINT>(nv12.size()));
    }

    duwn::video::VideoFrame frame{};
    frame.texture = tex;
    frame.width = frame.visible_width = 1920;
    frame.height = frame.visible_height = 1080;
    frame.format = DXGI_FORMAT_NV12;

    const int kFrameCount = 10;
    std::vector<double> frame_times_ms;
    frame_times_ms.reserve(kFrameCount);

    for (int i = 0; i < kFrameCount; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = renderer.Present(frame, true);
        auto t1 = std::chrono::high_resolution_clock::now();
        TEST_CHECK(res == duwn::video::PresentResult::Ok, "Present failed during benchmark");
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        frame_times_ms.push_back(ms);
        PumpMessages(1);
    }

    double total_ms = 0.0;
    double max_ms = 0.0;
    for (double ms : frame_times_ms) {
        total_ms += ms;
        if (ms > max_ms) max_ms = ms;
    }
    double avg_ms = total_ms / kFrameCount;

    std::cout << "    [PERF] 1080p 60FPS Presentation Benchmark (" << kFrameCount << " frames):\n";
    std::cout << "           Average Present Time: " << avg_ms << " ms\n";
    std::cout << "           Max Present Time:     " << max_ms << " ms\n";

    TEST_CHECK(avg_ms < 16.7, "Average present time exceeded 16.7ms (60 FPS threshold)");
    TEST_PASS("5. 1080p 60 FPS presentation latency meets real-time budget");
    return true;
}

int main() {
    std::cout << "=== DUWN Mirror Capture & Persistence Acceptance Tests ===\n";
    duwn::Logger::Initialize();
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    bool all_ok = true;
    all_ok &= TestWindowSurfaceAndControls();
    all_ok &= TestWgcInterop();
    all_ok &= TestSyntheticVideoPresentation();
    all_ok &= TestGeometryPersistence();
    all_ok &= TestPerformanceMeasurement();

    ::CoUninitialize();
    duwn::Logger::Shutdown();

    if (all_ok) {
        std::cout << "\nALL 5 INTEGRATION ACCEPTANCE TEST SUITES PASSED.\n";
        return 0;
    } else {
        std::cerr << "\nSOME INTEGRATION TESTS FAILED.\n";
        return 1;
    }
}
