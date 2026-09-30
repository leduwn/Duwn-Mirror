#pragma once
// WiredControlClient.h — Out-of-process CoreDevice HID client for DUWN Mirror.
// Manages the persistent control_bridge.py sidecar over 127.0.0.1 TCP loopback.

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <d2d1.h>

namespace duwn::wired {

enum class WiredControlState {
    Disabled,
    Starting,
    ConnectingRsd,
    OpeningHid,
    Ready,
    Active,
    Reconnecting,
    Error
};

struct ControlMetrics {
    uint64_t events_sent{0};
    uint64_t events_failed{0};
    double latency_avg_ms{0.0};
    double latency_p50_ms{0.0};
    double latency_p95_ms{0.0};
    double latency_p99_ms{0.0};
};

struct ControlDeviceInfo {
    std::wstring device_name;
    std::wstring ios_version;
    std::wstring product_type;
    uint32_t screen_width{0};
    uint32_t screen_height{0};
    int orientation{0}; // 0 = Portrait, 90 = Landscape Left, 270 = Landscape Right, 180 = Portrait Inverted
};

class WiredControlClient {
public:
    WiredControlClient() noexcept;
    ~WiredControlClient();

    // Non-copyable
    WiredControlClient(const WiredControlClient&) = delete;
    WiredControlClient& operator=(const WiredControlClient&) = delete;

    // Start / Stop persistent sidecar process & IPC session
    bool Start();
    void Stop();

    // Hardware Buttons (IndigoHIDService: Consumer Usage Page 0x0C)
    // names: "home", "lock", "power", "volume-up", "volume-down", "mute", "siri"
    // states: "press", "down", "up"
    bool SendButton(const std::string& name, const std::string& state = "press");

    // Touchscreen Gestures (UniversalHIDServiceService: 58-byte report, ServiceID 257)
    // Normalized coordinates: 0..65535
    bool SendTap(uint16_t x, uint16_t y);
    bool SendContact(uint16_t x, uint16_t y);
    bool SendRelease(uint16_t x, uint16_t y);
    bool SendDrag(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t duration_ms = 250);
    bool SendSwipe(const std::string& direction); // "up", "down", "left", "right"

    // Queries
    void RequestStatus();

    // State & Metrics inspection
    WiredControlState GetState() const noexcept { return m_state.load(); }
    ControlMetrics GetMetrics() const noexcept;
    ControlDeviceInfo GetDeviceInfo() const noexcept;
    std::wstring GetLastErrorMessage() const noexcept;

    // Coordinate Mapping & Letterbox Handling
    // Maps a client window coordinate (mouse_x, mouse_y) against the rendered video rectangle.
    // Returns false if the coordinate falls outside the video area (e.g. inside letterbox black bars).
    // Otherwise transforms into normalized iOS HID coordinates (0..65535) according to orientation.
    static bool ScreenToHidCoordinates(float mouse_x, float mouse_y,
                                       const D2D1_RECT_F& video_rect,
                                       int orientation,
                                       uint16_t& out_x, uint16_t& out_y) noexcept;

private:
    void WorkerLoop();
    bool LaunchBridgeProcess(uint16_t& out_port);
    void TerminateBridgeProcess();
    bool ConnectSocket(uint16_t port);
    void CloseSocket();
    bool SendRawJson(const std::string& json_line);
    bool ReadResponse(std::string& out_line, uint32_t timeout_ms = 1000);

    std::atomic<WiredControlState> m_state{WiredControlState::Disabled};
    std::atomic<bool> m_stop_requested{false};
    std::thread m_worker_thread;

    // Process handles
    void* m_process_handle{nullptr}; // HANDLE
    void* m_pipe_read{nullptr};      // HANDLE for bridge stdout pipe

    // Network socket (SOCKET cast to uintptr_t)
    uintptr_t m_socket{~uintptr_t(0)};

    // Metrics and device info guarded by mutex
    mutable std::mutex m_mutex;
    ControlMetrics m_metrics{};
    ControlDeviceInfo m_device_info{};
    std::wstring m_last_error{};
    std::atomic<uint64_t> m_cmd_counter{0};
};

} // namespace duwn::wired
