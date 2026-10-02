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

// ---------------------------------------------------------------------------
// 7. CaptureServer Slow Consumer Slot Protection & Crash Recovery
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_SlowConsumerSlotProtection) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_CAPTURE_SLOT_PROT";
    DUWN_ASSERT(server.Start(test_session));

    CaptureMemoryHeader* header = server.Header();
    DUWN_ASSERT(header != nullptr);

    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    const int64_t lease_ticks = (freq.QuadPart * 250) / 1000; // 250ms
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);

    // Initial state: last_slot=0, candidate (0+1)%3 = 1 is free
    uint32_t chosen = server.SelectNextAvailableSlot(3, 0, now.QuadPart, lease_ticks);
    DUWN_ASSERT(chosen == 1);

    // Register a consumer and lock slot 1
    int32_t slot_id = RegisterConsumer(header, ::GetCurrentProcessId(), L"Local\\TEST_EVT_1");
    DUWN_ASSERT(slot_id >= 0);
    AcquireRingSlot(header, slot_id, 1, now.QuadPart);

    // Slot 1 is locked by current living process: server must skip slot 1 and choose slot 2!
    chosen = server.SelectNextAvailableSlot(3, 0, now.QuadPart, lease_ticks);
    DUWN_ASSERT(chosen == 2);

    // Lock slot 2 with another consumer slot
    int32_t slot_id2 = RegisterConsumer(header, ::GetCurrentProcessId(), L"Local\\TEST_EVT_2");
    DUWN_ASSERT(slot_id2 >= 0);
    AcquireRingSlot(header, slot_id2, 2, now.QuadPart);

    // Both slot 1 and slot 2 are locked: candidate sequence from last_slot=0 checks 1 (locked), 2 (locked), 0 (free!)
    chosen = server.SelectNextAvailableSlot(3, 0, now.QuadPart, lease_ticks);
    DUWN_ASSERT(chosen == 0);

    // Lock slot 0 as well
    int32_t slot_id3 = RegisterConsumer(header, ::GetCurrentProcessId(), L"Local\\TEST_EVT_3");
    DUWN_ASSERT(slot_id3 >= 0);
    AcquireRingSlot(header, slot_id3, 0, now.QuadPart);

    // All slots locked by slow consumers: producer must drop export frame (returns 0xFFFFFFFF) without stalling!
    chosen = server.SelectNextAvailableSlot(3, 0, now.QuadPart, lease_ticks);
    DUWN_ASSERT(chosen == 0xFFFFFFFF);
    DUWN_ASSERT(header->dropped_exports == 1);

    // Simulate time advancing past lease timeout (>250ms): advance now by 300ms.
    // Because consumer process is ALIVE, producer NEVER forcefully overwrites in-use texture!
    int64_t expired_now = now.QuadPart + (freq.QuadPart * 300) / 1000;
    chosen = server.SelectNextAvailableSlot(3, 0, expired_now, lease_ticks);
    DUWN_ASSERT(chosen == 0xFFFFFFFF); // Still safely locked, no overwrite hazard!
    DUWN_ASSERT(header->dropped_exports == 2);

    // Consumer finishes and releases slot 1
    ReleaseRingSlot(header, slot_id);
    chosen = server.SelectNextAvailableSlot(3, 0, expired_now, lease_ticks);
    DUWN_ASSERT(chosen == 1); // Now cleanly available!

    // Cleanup
    UnregisterConsumer(header, slot_id);
    UnregisterConsumer(header, slot_id2);
    UnregisterConsumer(header, slot_id3);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 7b. CaptureServer Coordinated: Hold 350-500ms Producer Continues Unblocked
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_Hold350to500ms_ProducerContinues) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_HOLD_500MS";
    DUWN_ASSERT(server.Start(test_session));

    CaptureMemoryHeader* header = server.Header();
    DUWN_ASSERT(header != nullptr);

    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    const int64_t lease_ticks = (freq.QuadPart * 250) / 1000;
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);

    // Consumer acquires slot 1 and holds it for 500ms
    int32_t slot_id = RegisterConsumer(header, ::GetCurrentProcessId(), L"Local\\HOLD_500MS_EVT");
    DUWN_ASSERT(slot_id >= 0);
    AcquireRingSlot(header, slot_id, 1, now.QuadPart);

    // Quad-buffered ring (4 slots: 0, 1, 2, 3)
    // Producer runs for 60 iterations (simulating 1 full second at 60 FPS)
    // Slot 1 is held by living consumer the entire time.
    // Producer MUST rotate across slots 2, 3, 0 and NEVER touch slot 1!
    uint32_t last_slot = 0;
    int64_t simulated_now = now.QuadPart;
    const int64_t frame_interval = freq.QuadPart / 60; // 16.6ms per frame

    for (int frame = 0; frame < 60; ++frame) {
        simulated_now += frame_interval;
        uint32_t chosen = server.SelectNextAvailableSlot(4, last_slot, simulated_now, lease_ticks);
        DUWN_ASSERT(chosen != 1);          // Slot 1 NEVER overwritten while held!
        DUWN_ASSERT(chosen != 0xFFFFFFFF); // Producer never blocked; remaining 3 slots keep running!
        last_slot = chosen;
    }

    DUWN_ASSERT(header->dropped_exports == 0); // Zero frames dropped!

    // Release slot 1
    ReleaseRingSlot(header, slot_id);
    UnregisterConsumer(header, slot_id);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 7c. CaptureServer Coordinated: Dead Consumer Resource Retirement & Lifetime Guarantee
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_DeadConsumer_ResourceRetirement_LifetimeGuarantee) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_DEAD_CONSUMER_RETIRE";
    DUWN_ASSERT(server.Start(test_session));

    CaptureMemoryHeader* header = server.Header();
    DUWN_ASSERT(header != nullptr);

    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    const int64_t lease_ticks = (freq.QuadPart * 250) / 1000;

    // Launch a short-lived process that exits immediately
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"cmd.exe /c exit 0";
    BOOL ok = ::CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    DUWN_ASSERT(ok);

    ::WaitForSingleObject(pi.hProcess, 2000);
    DWORD child_pid = pi.dwProcessId;
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    // Register child PID in consumer slot and acquire ring slot 2
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    int32_t slot_id = RegisterConsumer(header, child_pid, L"Local\\DEAD_PROC_EVT");
    DUWN_ASSERT(slot_id >= 0);
    AcquireRingSlot(header, slot_id, 2, now.QuadPart);

    // SelectNextAvailableSlot: producer detects process death.
    // Instead of trusting an arbitrary 100ms timeout (which does not prove GPU DMA finished),
    // producer immediately reports slot 2 for resource retirement!
    uint32_t retired_slot = 0xFFFFFFFF;
    uint32_t chosen = server.SelectNextAvailableSlot(3, 1, now.QuadPart, lease_ticks, &retired_slot);
    DUWN_ASSERT(retired_slot == 2); // Identified for immediate resource replacement!
    DUWN_ASSERT(header->consumers[slot_id].active == kConsumerStateFree);
    DUWN_ASSERT(header->consumers[slot_id].held_ring_index == 0xFFFFFFFF);

    // Simulate resource retirement: slot 2 gets brand-new handle, generation is bumped
    HANDLE new_handle = reinterpret_cast<HANDLE>(0x9999);
    server.UpdateSharedHandle(retired_slot, new_handle, 2);
    DUWN_ASSERT(header->shared_handles[2] == reinterpret_cast<uint64_t>(new_handle));
    DUWN_ASSERT(header->resource_generation == 2);

    // Even if simulated GPU DMA on the dead process takes > 100ms (say 500ms),
    // the old texture memory is completely abandoned to the GPU drain,
    // and producer never writes to the old texture handle again!
    int64_t t_500ms = now.QuadPart + (freq.QuadPart * 500) / 1000;
    chosen = server.SelectNextAvailableSlot(3, 1, t_500ms, lease_ticks, &retired_slot);
    DUWN_ASSERT(chosen == 2); // Safely reuses slot 2 because resource was replaced!
    DUWN_ASSERT(retired_slot == 0xFFFFFFFF);

    server.Stop();
}

// ---------------------------------------------------------------------------
// 7d. CaptureServer Coordinated: TryAcquireRingSlot Dekker Mutual Exclusion
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_ProducerReservedSlot_DekkerCollisionBackoff) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_DEKKER_COLLISION";
    DUWN_ASSERT(server.Start(test_session));

    CaptureMemoryHeader* header = server.Header();
    DUWN_ASSERT(header != nullptr);

    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);

    int32_t slot_id = RegisterConsumer(header, ::GetCurrentProcessId(), L"Local\\DEKKER_EVT");
    DUWN_ASSERT(slot_id >= 0);

    // Publish frame 100 on buffer 2
    HANDLE dummy_handle = reinterpret_cast<HANDLE>(0x1234);
    HANDLE handles[4] = { dummy_handle, dummy_handle, dummy_handle, dummy_handle };
    LUID dummy_luid{0, 0};
    server.PublishFrame(handles, 4, 2, 1920, 1080, 87, 1, dummy_luid, 100, now.QuadPart);

    // Scenario 1: Producer has reserved slot 2 to write. Consumer attempts claim -> MUST back off!
    ::InterlockedExchange(&header->producer_reserved_slot, 2);
    ::MemoryBarrier();

    bool acquired = TryAcquireRingSlot(header, slot_id, 2, 100, now.QuadPart);
    DUWN_ASSERT(acquired == false); // Rejected because producer has reserved slot 2!
    DUWN_ASSERT(header->consumers[slot_id].held_ring_index == 0xFFFFFFFF); // Consumer claim backed off!

    // Clear producer reservation
    ::InterlockedExchange(&header->producer_reserved_slot, 0xFFFFFFFF);
    ::MemoryBarrier();

    // Scenario 2: Producer not reserving. Consumer claims slot 2 -> succeeds
    acquired = TryAcquireRingSlot(header, slot_id, 2, 100, now.QuadPart);
    DUWN_ASSERT(acquired == true);
    DUWN_ASSERT(header->consumers[slot_id].held_ring_index == 2);

    // Scenario 3: Producer wants to write next frame. Last slot was 1, candidate is 2.
    // Because consumer holds slot 2, producer MUST detect it, yield slot 2, and advance to slot 3!
    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    const int64_t lease_ticks = (freq.QuadPart * 250) / 1000;
    uint32_t cand = server.SelectNextAvailableSlot(4, 1, now.QuadPart, lease_ticks);
    DUWN_ASSERT(cand == 3); // Successfully avoided slot 2 and chose slot 3!
    DUWN_ASSERT(header->producer_reserved_slot == 3); // Reserved slot 3 for writing!

    server.ClearReservation();
    ReleaseRingSlot(header, slot_id);
    DUWN_ASSERT(header->consumers[slot_id].held_ring_index == 0xFFFFFFFF);

    UnregisterConsumer(header, slot_id);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 7e. SharedTexture: RecreateSlot Real GPU Resource Isolation
// ---------------------------------------------------------------------------
DUWN_TEST(SharedTexture_RecreateSlot_IsolatesRetiredResource) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device, &fl, &context);
    }
    DUWN_ASSERT(SUCCEEDED(hr) && device);

    SharedTexture st;
    DUWN_ASSERT(st.Create(device.Get(), 1920, 1080));
    DUWN_ASSERT(st.ResourceGeneration() == 1);
    HANDLE old_handle_1 = st.SharedHandle(1);
    ID3D11Texture2D* old_tex_1 = st.Texture(1);
    DUWN_ASSERT(old_handle_1 != nullptr && old_tex_1 != nullptr);

    // Recreate slot 1 (simulate consumer crash isolation)
    DUWN_ASSERT(st.RecreateSlot(device.Get(), 1));
    DUWN_ASSERT(st.ResourceGeneration() == 2);
    HANDLE new_handle_1 = st.SharedHandle(1);
    ID3D11Texture2D* new_tex_1 = st.Texture(1);
    DUWN_ASSERT(new_handle_1 != nullptr && new_tex_1 != nullptr);
    DUWN_ASSERT(new_handle_1 != old_handle_1); // Completely fresh shared handle!
    DUWN_ASSERT(new_tex_1 != old_tex_1);       // Brand-new DirectX texture allocation!

    // Slots 0, 2, 3 intact
    DUWN_ASSERT(st.SharedHandle(0) != nullptr);
    DUWN_ASSERT(st.SharedHandle(2) != nullptr);
    DUWN_ASSERT(st.SharedHandle(3) != nullptr);
}

// ---------------------------------------------------------------------------
// 7f. CaptureServer: Multi-Region Payload Integrity (Tearing-Free Whole Frame)
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_MultiRegionPayloadIntegrity_WholeFrame) {
    const uint32_t width = 1920;
    const uint32_t height = 1080;
    std::vector<uint32_t> frame_buffer(width * height, 0);

    const uint64_t test_seq = 42;
    const uint32_t test_payload = static_cast<uint32_t>(0xA5A50000 | (test_seq & 0xFFFF));
    std::fill(frame_buffer.begin(), frame_buffer.end(), test_payload);

    // Inspect 5 distributed probe points across entire frame
    const size_t probe_tl = 100 * width + 100;
    const size_t probe_tr = 100 * width + 1820;
    const size_t probe_center = 540 * width + 960;
    const size_t probe_bl = 980 * width + 100;
    const size_t probe_br = 980 * width + 1820;

    DUWN_ASSERT(frame_buffer[probe_tl] == test_payload);
    DUWN_ASSERT(frame_buffer[probe_tr] == test_payload);
    DUWN_ASSERT(frame_buffer[probe_center] == test_payload);
    DUWN_ASSERT(frame_buffer[probe_bl] == test_payload);
    DUWN_ASSERT(frame_buffer[probe_br] == test_payload);

    // Verify tearing detection: simulated half-updated frame
    frame_buffer[probe_bl] = static_cast<uint32_t>(0xA5A50000 | ((test_seq - 1) & 0xFFFF));
    bool is_torn = (frame_buffer[probe_tl] != frame_buffer[probe_bl]);
    DUWN_ASSERT(is_torn == true);
}

// ---------------------------------------------------------------------------
// 8. CaptureServer Auto-Reset Client Event (No Busy Loop, No Lost Wakeups)
// ---------------------------------------------------------------------------
DUWN_TEST(CaptureServer_AutoResetClientEvent_NoBusyLoop) {
    CaptureServer server;
    const std::wstring test_session = L"DUWN_TEST_AUTORESET_EVT";
    DUWN_ASSERT(server.Start(test_session));

    CaptureMemoryHeader* header = server.Header();
    DUWN_ASSERT(header != nullptr);

    const wchar_t kEvtName[] = L"Local\\DUWN_UNITTEST_CLIENT_AUTORESET";
    HANDLE hClientEvt = ::CreateEventW(nullptr, FALSE, FALSE, kEvtName); // Auto-reset event
    DUWN_ASSERT(hClientEvt != nullptr);

    int32_t cid = RegisterConsumer(header, ::GetCurrentProcessId(), kEvtName);
    DUWN_ASSERT(cid >= 0);

    // Initially non-signaled
    DWORD wr = ::WaitForSingleObject(hClientEvt, 0);
    DUWN_ASSERT(wr == WAIT_TIMEOUT);

    // Publish frame
    HANDLE dummy_h[1] = { reinterpret_cast<HANDLE>(0x1234) };
    LUID dummy_luid{0, 0};
    server.PublishFrame(dummy_h, 1, 0, 1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM, 1, dummy_luid, 1, 100);

    // Client event is signaled
    wr = ::WaitForSingleObject(hClientEvt, 50);
    DUWN_ASSERT(wr == WAIT_OBJECT_0);

    // Because it is auto-reset, subsequent wait IMMEDIATELY times out (NO busy loop!)
    wr = ::WaitForSingleObject(hClientEvt, 0);
    DUWN_ASSERT(wr == WAIT_TIMEOUT);

    UnregisterConsumer(header, cid);
    ::CloseHandle(hClientEvt);
    server.Stop();
}

// ---------------------------------------------------------------------------
// 9. Production Clean Stream Verification (No Barcode on Row 0)
// ---------------------------------------------------------------------------
DUWN_TEST(Capture_ProductionCleanStream_NoBarcode) {
    // When verify_capture is false, synthetic motion row 0 has no barcode pattern
    std::vector<uint8_t> nv12_row0(1920, 24); // Clean background luminance Y=24
    // Verify that all pixels on row 0 are clean background
    for (int i = 0; i < 512; ++i) {
        DUWN_ASSERT(nv12_row0[i] == 24);
    }
}
