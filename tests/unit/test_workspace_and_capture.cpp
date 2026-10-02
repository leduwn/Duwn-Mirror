// test_workspace_and_capture.cpp — Tests for Workspace V2 and Shared Texture Export
#include "capture/CaptureServer.h"
#include "capture/SharedTexture.h"
#include "ui/UiState.h"
#include <windows.h>
#include <thread>
#include <atomic>

using namespace duwn::capture;
using namespace duwn::ui;

// Helper: aspect-ratio fit box calculation (matches Workspace V2 layout logic)
static RECT CalculateFittedVideoRect(int container_w, int container_h, int video_w, int video_h) noexcept {
    if (container_w <= 0 || container_h <= 0 || video_w <= 0 || video_h <= 0) {
        return RECT{0, 0, container_w > 0 ? container_w : 0, container_h > 0 ? container_h : 0};
    }
    double scale_x = static_cast<double>(container_w) / video_w;
    double scale_y = static_cast<double>(container_h) / video_h;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    int fit_w = static_cast<int>(video_w * scale);
    int fit_h = static_cast<int>(video_h * scale);
    int offset_x = (container_w - fit_w) / 2;
    int offset_y = (container_h - fit_h) / 2;

    return RECT{offset_x, offset_y, offset_x + fit_w, offset_y + fit_h};
}

// ---------------------------------------------------------------------------
// 1. CaptureServer Lifecycle and Header Verification
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_LifecycleAndHeader) {
    CaptureServer server;
    DUWN_ASSERT(!server.IsRunning());

    const std::wstring test_session = L"DUWN_TEST_CAPTURE_LIFECYCLE";
    bool ok = server.Start(test_session);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(server.IsRunning());
    DUWN_ASSERT(server.Generation() == 1);

    // Open file mapping as client
    std::wstring map_name = L"Local\\" + test_session;
    HANDLE h_map = ::OpenFileMappingW(FILE_MAP_READ, FALSE, map_name.c_str());
    DUWN_ASSERT(h_map != nullptr);

    auto* header = static_cast<const CaptureMemoryHeader*>(
        ::MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(CaptureMemoryHeader)));
    DUWN_ASSERT(header != nullptr);

    DUWN_ASSERT(std::memcmp(header->magic, "DUWNCAP", 7) == 0);
    DUWN_ASSERT(header->protocol_version == 2);
    DUWN_ASSERT(header->generation == 1);
    DUWN_ASSERT(header->source_name[0] != '\0');

    ::UnmapViewOfFile(header);
    ::CloseHandle(h_map);

    server.Stop();
    DUWN_ASSERT(!server.IsRunning());

    // Restart check: generation increments
    DUWN_ASSERT(server.Start(test_session));
    DUWN_ASSERT(server.Generation() == 2);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 2. CaptureServer PublishFrame and Event Signaling
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_PublishFrame_UpdatesSharedMemory) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_CAPTURE_PUBLISH";
    DUWN_ASSERT(server.Start(test_session));

    // Open event as client
    std::wstring event_name = L"Local\\" + test_session + L"_FRAME_READY";
    HANDLE h_event = ::OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
    DUWN_ASSERT(h_event != nullptr);

    // Initial event state should be non-signaled
    DWORD wait_res = ::WaitForSingleObject(h_event, 0);
    DUWN_ASSERT(wait_res == WAIT_TIMEOUT);

    // Publish frame with ring buffers
    HANDLE dummy_handles[3] = {
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xCAFE0001)),
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xCAFE0002)),
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xCAFE0003))
    };
    LUID luid{123, 456};
    server.PublishFrame(dummy_handles, 3, 1, 1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM, 7, luid, 42, 9876543210LL);

    // Event should now be signaled
    wait_res = ::WaitForSingleObject(h_event, 100);
    DUWN_ASSERT(wait_res == WAIT_OBJECT_0);

    // Read back header using Seqlock
    std::wstring map_name = L"Local\\" + test_session;
    HANDLE h_map = ::OpenFileMappingW(FILE_MAP_READ, FALSE, map_name.c_str());
    DUWN_ASSERT(h_map != nullptr);

    auto* header = static_cast<const CaptureMemoryHeader*>(
        ::MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(CaptureMemoryHeader)));
    DUWN_ASSERT(header != nullptr);

    CaptureMemoryHeader snapshot{};
    DUWN_ASSERT(ReadHeaderConsistent(header, snapshot));

    DUWN_ASSERT(snapshot.ring_buffer_count == 3);
    DUWN_ASSERT(snapshot.active_buffer_index == 1);
    DUWN_ASSERT(snapshot.shared_handles[0] == 0xCAFE0001);
    DUWN_ASSERT(snapshot.shared_handles[1] == 0xCAFE0002);
    DUWN_ASSERT(snapshot.shared_handles[2] == 0xCAFE0003);
    DUWN_ASSERT(snapshot.width == 1920);
    DUWN_ASSERT(snapshot.height == 1080);
    DUWN_ASSERT(snapshot.dxgi_format == DXGI_FORMAT_B8G8R8A8_UNORM);
    DUWN_ASSERT(snapshot.resource_generation == 7);
    DUWN_ASSERT(snapshot.adapter_luid_low == 123);
    DUWN_ASSERT(snapshot.adapter_luid_high == 456);
    DUWN_ASSERT(snapshot.frame_index == 42);
    DUWN_ASSERT(snapshot.timestamp_qpc == 9876543210LL);

    ::UnmapViewOfFile(header);
    ::CloseHandle(h_map);
    ::CloseHandle(h_event);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 3. CaptureServer Rapid Concurrent Publish Stress & Multi-Consumer Broadcast
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_RapidPublishStress) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_CAPTURE_STRESS";
    DUWN_ASSERT(server.Start(test_session));

    std::wstring map_name = L"Local\\" + test_session;
    HANDLE h_map = ::OpenFileMappingW(FILE_MAP_READ, FALSE, map_name.c_str());
    DUWN_ASSERT(h_map != nullptr);

    auto* header = static_cast<const CaptureMemoryHeader*>(
        ::MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(CaptureMemoryHeader)));
    DUWN_ASSERT(header != nullptr);

    std::atomic<bool> reader_running{true};
    std::atomic<uint64_t> max_read_index{0};
    std::atomic<bool> corrupted{false};

    // 2 concurrent reader threads simulating 2 independent consumers (OBS & TikTok Live Studio)
    auto reader_fn = [&]() {
        uint64_t last_idx = 0;
        CaptureMemoryHeader snap{};
        while (reader_running.load(std::memory_order_relaxed)) {
            if (ReadHeaderConsistent(header, snap)) {
                if (snap.frame_index > 0 && (snap.width != 1920 || snap.height != 1080)) {
                    corrupted.store(true);
                }
                if (snap.frame_index > 0 && snap.frame_index >= last_idx) {
                    last_idx = snap.frame_index;
                    max_read_index.store(last_idx, std::memory_order_relaxed);
                }
            }
            std::this_thread::yield();
        }
    };

    std::thread reader1(reader_fn);
    std::thread reader2(reader_fn);

    for (uint64_t i = 1; i <= 50; ++i) {
        server.PublishFrame(
            reinterpret_cast<HANDLE>(static_cast<uintptr_t>(i)),
            1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM, i, 1000LL * i);
        ::Sleep(1);
    }

    reader_running.store(false);
    reader1.join();
    reader2.join();

    DUWN_ASSERT(!corrupted.load());
    DUWN_ASSERT(max_read_index.load() > 0);

    ::UnmapViewOfFile(header);
    ::CloseHandle(h_map);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 4. SharedTexture Initial State and Reset
// ---------------------------------------------------------------------------
DUWN_TEST(SharedTexture_InitialStateAndReset) {
    SharedTexture st;
    DUWN_ASSERT(st.Width() == 0);
    DUWN_ASSERT(st.Height() == 0);
    DUWN_ASSERT(st.Texture() == nullptr);
    DUWN_ASSERT(st.SharedHandle() == nullptr);

    // Release on empty texture is safe
    st.Release();
    DUWN_ASSERT(st.Width() == 0);
}

// ---------------------------------------------------------------------------
// 5. Workspace V2 Aspect Ratio Fitting Geometry
// ---------------------------------------------------------------------------
DUWN_TEST(WorkspaceV2_AspectRatioFitting) {
    // 1) Portrait 9:16 (1080x1920) inside landscape container (800x600)
    RECT r1 = CalculateFittedVideoRect(800, 600, 1080, 1920);
    int w1 = r1.right - r1.left;
    int h1 = r1.bottom - r1.top;
    DUWN_ASSERT(h1 == 600); // Pillarbox: bounded by container height
    DUWN_ASSERT(w1 == 337); // 600 * (1080/1920) = 337.5 -> 337
    DUWN_ASSERT(r1.left == (800 - 337) / 2); // Centered horizontally
    DUWN_ASSERT(r1.top == 0);

    // 2) Landscape 16:9 (1920x1080) inside landscape container (800x600)
    RECT r2 = CalculateFittedVideoRect(800, 600, 1920, 1080);
    int w2 = r2.right - r2.left;
    int h2 = r2.bottom - r2.top;
    DUWN_ASSERT(w2 == 800); // Letterbox: bounded by container width
    DUWN_ASSERT(h2 == 450); // 800 * (1080/1920) = 450
    DUWN_ASSERT(r2.left == 0);
    DUWN_ASSERT(r2.top == (600 - 450) / 2); // Centered vertically

    // 3) Degenerate zero inputs
    RECT r3 = CalculateFittedVideoRect(0, 600, 1920, 1080);
    DUWN_ASSERT(r3.left == 0 && r3.right == 0);
}

// ---------------------------------------------------------------------------
// 6. Workspace V2 View Mode Contract
// ---------------------------------------------------------------------------
DUWN_TEST(WorkspaceV2_ViewModeContract) {
    UiState state;
    DUWN_ASSERT(!state.is_screen_only);

    state.is_screen_only = true;
    DUWN_ASSERT(state.is_screen_only);

    // Mirror tab is active by default
    DUWN_ASSERT(state.active_tab == NavTab::Mirror);
}
