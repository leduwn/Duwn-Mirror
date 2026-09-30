// WiredControlClient.cpp — Implementation of out-of-process CoreDevice HID client.

#include "WiredControlClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

namespace duwn::wired {

namespace {

// Helper to extract JSON integer/double field from simple JSON string
bool ExtractJsonField(const std::string& json, const std::string& key, std::string& out_val) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"" + key + "\" :";
        pos = json.find(needle);
    }
    if (pos == std::string::npos) return false;

    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.length()) return false;

    if (json[pos] == '"') {
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return false;
        out_val = json.substr(pos, end - pos);
        return true;
    } else {
        size_t end = json.find_first_of(",}\r\n ", pos);
        if (end == std::string::npos) end = json.length();
        out_val = json.substr(pos, end - pos);
        return true;
    }
}

// Find Python interpreter and bridge script relative to current module or working directory
bool ResolveBridgePaths(std::wstring& out_python_exe, std::wstring& out_bridge_py) {
    namespace fs = std::filesystem;

    wchar_t mod_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, mod_path, MAX_PATH);
    fs::path base_dir = fs::path(mod_path).parent_path();

    // Check candidate project root paths
    std::vector<fs::path> search_roots = {
        base_dir,
        base_dir.parent_path(),
        base_dir.parent_path().parent_path(),
        base_dir.parent_path().parent_path().parent_path(),
        fs::current_path()
    };

    for (const auto& root : search_roots) {
        fs::path venv_py = root / L"tools" / L"wired-probe" / L".venv" / L"Scripts" / L"python.exe";
        fs::path bridge = root / L"tools" / L"wired-control" / L"control_bridge.py";

        if (fs::exists(venv_py) && fs::exists(bridge)) {
            out_python_exe = venv_py.wstring();
            out_bridge_py = bridge.wstring();
            return true;
        }

        // Also check if system python is available
        fs::path alt_bridge = root / L"tools" / L"wired-control" / L"control_bridge.py";
        if (fs::exists(alt_bridge)) {
            // Check Windows default python paths
            const wchar_t* sys_pythons[] = {
                L"C:\\Program Files\\Python314\\python.exe",
                L"C:\\Program Files\\Python313\\python.exe",
                L"C:\\Program Files\\Python312\\python.exe",
                L"python.exe"
            };
            for (const auto* sp : sys_pythons) {
                if (fs::exists(sp) || std::wstring(sp) == L"python.exe") {
                    out_python_exe = sp;
                    out_bridge_py = alt_bridge.wstring();
                    return true;
                }
            }
        }
    }

    return false;
}

} // anonymous namespace

WiredControlClient::WiredControlClient() noexcept {
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

WiredControlClient::~WiredControlClient() {
    Stop();
    WSACleanup();
}

bool WiredControlClient::Start() {
    if (m_state.load() != WiredControlState::Disabled &&
        m_state.load() != WiredControlState::Error) {
        return true;
    }

    m_stop_requested.store(false);
    m_state.store(WiredControlState::Starting);

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_worker_thread = std::thread(&WiredControlClient::WorkerLoop, this);
    return true;
}

void WiredControlClient::Stop() {
    m_stop_requested.store(true);

    if (m_socket != ~uintptr_t(0)) {
        // Send clean quit command
        SendRawJson("{\"type\":\"quit\"}\n");
    }

    CloseSocket();
    TerminateBridgeProcess();

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_state.store(WiredControlState::Disabled);
}

void WiredControlClient::WorkerLoop() {
    uint16_t port = 0;
    m_state.store(WiredControlState::ConnectingRsd);

    if (!LaunchBridgeProcess(port)) {
        m_state.store(WiredControlState::Error);
        return;
    }

    m_state.store(WiredControlState::OpeningHid);

    if (!ConnectSocket(port)) {
        m_state.store(WiredControlState::Error);
        TerminateBridgeProcess();
        return;
    }

    m_state.store(WiredControlState::Ready);

    // Initial status and display info request
    RequestStatus();

    // Health monitoring loop
    while (!m_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (m_process_handle) {
            DWORD exit_code = 0;
            if (GetExitCodeProcess(static_cast<HANDLE>(m_process_handle), &exit_code)) {
                if (exit_code != STILL_ACTIVE) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_last_error = std::format(L"Bridge process exited with code {}", exit_code);
                    m_state.store(WiredControlState::Error);
                    break;
                }
            }
        }
    }

    CloseSocket();
}

bool WiredControlClient::LaunchBridgeProcess(uint16_t& out_port) {
    std::wstring python_exe, bridge_py;
    if (!ResolveBridgePaths(python_exe, bridge_py)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = L"Could not locate control_bridge.py or Python interpreter";
        return false;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE pipe_read = nullptr;
    HANDLE pipe_write = nullptr;
    if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = L"Failed to create stdout pipe for bridge";
        return false;
    }
    SetHandleInformation(pipe_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = pipe_write;
    si.hStdError = pipe_write;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmd_line = std::format(L"\"{}\" \"{}\" --port 0", python_exe, bridge_py);
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(pipe_write); // Close write end in parent

    if (!created) {
        CloseHandle(pipe_read);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_error = std::format(L"CreateProcess failed: error {}", GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    m_process_handle = pi.hProcess;
    m_pipe_read = pipe_read;

    // Read lines from bridge stdout until handshake JSON is received
    std::string line_buf;
    char ch = 0;
    DWORD bytes_read = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start_time).count() < 15) {
        if (ReadFile(pipe_read, &ch, 1, &bytes_read, nullptr) && bytes_read > 0) {
            if (ch == '\n') {
                if (!line_buf.empty() && line_buf.back() == '\r') {
                    line_buf.pop_back();
                }

                std::string event_val, port_val;
                if (ExtractJsonField(line_buf, "event", event_val) && event_val == "ready") {
                    if (ExtractJsonField(line_buf, "port", port_val)) {
                        out_port = static_cast<uint16_t>(std::stoi(port_val));
                        return true;
                    }
                }
                line_buf.clear();
            } else {
                line_buf.push_back(ch);
            }
        } else {
            // Check if process terminated prematurely
            DWORD exit_code = 0;
            if (GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_last_error = std::format(L"Bridge terminated early with exit code {}", exit_code);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_last_error = L"Timed out waiting for ready handshake from bridge";
    return false;
}

void WiredControlClient::TerminateBridgeProcess() {
    if (m_pipe_read) {
        CloseHandle(static_cast<HANDLE>(m_pipe_read));
        m_pipe_read = nullptr;
    }

    if (m_process_handle) {
        HANDLE h = static_cast<HANDLE>(m_process_handle);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(h, &exit_code) && exit_code == STILL_ACTIVE) {
            // Wait up to 1 second for graceful exit
            if (WaitForSingleObject(h, 1000) != WAIT_OBJECT_0) {
                TerminateProcess(h, 1);
            }
        }
        CloseHandle(h);
        m_process_handle = nullptr;
    }
}

bool WiredControlClient::ConnectSocket(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }

    // Enable TCP_NODELAY for lowest interactive latency
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // Attempt connection with retries up to 2 seconds
    for (int retry = 0; retry < 20; ++retry) {
        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            m_socket = static_cast<uintptr_t>(s);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    closesocket(s);
    return false;
}

void WiredControlClient::CloseSocket() {
    if (m_socket != ~uintptr_t(0)) {
        SOCKET s = static_cast<SOCKET>(m_socket);
        closesocket(s);
        m_socket = ~uintptr_t(0);
    }
}

bool WiredControlClient::SendRawJson(const std::string& json_line) {
    if (m_socket == ~uintptr_t(0)) return false;
    SOCKET s = static_cast<SOCKET>(m_socket);
    int sent = send(s, json_line.data(), static_cast<int>(json_line.length()), 0);
    return sent == static_cast<int>(json_line.length());
}

bool WiredControlClient::ReadResponse(std::string& out_line, uint32_t timeout_ms) {
    if (m_socket == ~uintptr_t(0)) return false;
    SOCKET s = static_cast<SOCKET>(m_socket);

    DWORD tv = timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    out_line.clear();
    char ch = 0;
    while (true) {
        int r = recv(s, &ch, 1, 0);
        if (r <= 0) return false;
        if (ch == '\n') break;
        out_line.push_back(ch);
    }
    if (!out_line.empty() && out_line.back() == '\r') {
        out_line.pop_back();
    }
    return true;
}

bool WiredControlClient::SendButton(const std::string& name, const std::string& state) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    m_state.store(WiredControlState::Active);
    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"button\",\"name\":\"{}\",\"state\":\"{}\",\"id\":{}}}\n",
                                  name, state, id);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, 2000)) {
        auto t1 = std::chrono::steady_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
        m_metrics.latency_avg_ms = (m_metrics.latency_avg_ms * 0.9) + (lat_ms * 0.1);
        m_metrics.latency_p50_ms = lat_ms;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }

    m_state.store(WiredControlState::Ready);
    return ok;
}

bool WiredControlClient::SendTap(uint16_t x, uint16_t y) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    m_state.store(WiredControlState::Active);
    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"tap\",\"x\":{},\"y\":{},\"id\":{}}}\n", x, y, id);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, 1000)) {
        auto t1 = std::chrono::steady_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
        m_metrics.latency_avg_ms = (m_metrics.latency_avg_ms * 0.9) + (lat_ms * 0.1);
        m_metrics.latency_p50_ms = lat_ms;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }

    m_state.store(WiredControlState::Ready);
    return ok;
}

bool WiredControlClient::SendContact(uint16_t x, uint16_t y) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"contact\",\"x\":{},\"y\":{},\"id\":{}}}\n", x, y, id);
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, 500)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }
    return ok;
}

bool WiredControlClient::SendRelease(uint16_t x, uint16_t y) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"release\",\"x\":{},\"y\":{},\"id\":{}}}\n", x, y, id);
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, 500)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }
    return ok;
}

bool WiredControlClient::SendDrag(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t duration_ms) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    m_state.store(WiredControlState::Active);
    uint64_t id = ++m_cmd_counter;
    std::string req = std::format(
        "{{\"type\":\"drag\",\"x1\":{},\"y1\":{},\"x2\":{},\"y2\":{},\"duration_ms\":{},\"id\":{}}}\n",
        x1, y1, x2, y2, duration_ms, id);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, duration_ms + 1000)) {
        auto t1 = std::chrono::steady_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
        m_metrics.latency_avg_ms = (m_metrics.latency_avg_ms * 0.9) + (lat_ms * 0.1);
        m_metrics.latency_p50_ms = lat_ms;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }

    m_state.store(WiredControlState::Ready);
    return ok;
}

bool WiredControlClient::SendSwipe(const std::string& direction) {
    if (m_state.load() != WiredControlState::Ready && m_state.load() != WiredControlState::Active) {
        return false;
    }

    m_state.store(WiredControlState::Active);
    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"swipe\",\"direction\":\"{}\",\"id\":{}}}\n", direction, id);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = SendRawJson(req);
    std::string resp;
    if (ok && ReadResponse(resp, 2000)) {
        auto t1 = std::chrono::steady_clock::now();
        double lat_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_sent++;
        m_metrics.latency_avg_ms = (m_metrics.latency_avg_ms * 0.9) + (lat_ms * 0.1);
        m_metrics.latency_p50_ms = lat_ms;
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.events_failed++;
        ok = false;
    }

    m_state.store(WiredControlState::Ready);
    return ok;
}

void WiredControlClient::RequestStatus() {
    uint64_t id = ++m_cmd_counter;
    std::string req = std::format("{{\"type\":\"status\",\"id\":{}}}\n", id);
    if (SendRawJson(req)) {
        std::string resp;
        if (ReadResponse(resp, 1000)) {
            std::string p50_val, p95_val, p99_val, avg_val;
            if (ExtractJsonField(resp, "latency_avg_ms", avg_val)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_metrics.latency_avg_ms = std::stod(avg_val);
                if (ExtractJsonField(resp, "latency_p50_ms", p50_val)) m_metrics.latency_p50_ms = std::stod(p50_val);
                if (ExtractJsonField(resp, "latency_p95_ms", p95_val)) m_metrics.latency_p95_ms = std::stod(p95_val);
                if (ExtractJsonField(resp, "latency_p99_ms", p99_val)) m_metrics.latency_p99_ms = std::stod(p99_val);
            }
        }
    }
}

ControlMetrics WiredControlClient::GetMetrics() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metrics;
}

ControlDeviceInfo WiredControlClient::GetDeviceInfo() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_device_info;
}

std::wstring WiredControlClient::GetLastErrorMessage() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last_error;
}

bool WiredControlClient::ScreenToHidCoordinates(float mouse_x, float mouse_y,
                                                const D2D1_RECT_F& video_rect,
                                                int orientation,
                                                uint16_t& out_x, uint16_t& out_y) noexcept {
    // 1. Reject clicks in letterbox / pillarbox black bars
    if (mouse_x < video_rect.left || mouse_x > video_rect.right ||
        mouse_y < video_rect.top  || mouse_y > video_rect.bottom) {
        return false;
    }

    float width = video_rect.right - video_rect.left;
    float height = video_rect.bottom - video_rect.top;
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    // 2. Normalize local coordinates to [0.0, 1.0]
    float norm_x = (mouse_x - video_rect.left) / width;
    float norm_y = (mouse_y - video_rect.top) / height;

    norm_x = std::clamp(norm_x, 0.0f, 1.0f);
    norm_y = std::clamp(norm_y, 0.0f, 1.0f);

    // 3. Apply orientation rotation
    // CoreDevice HID coordinate space:
    // (0, 0) is top-left of physical portrait device, (65535, 65535) is bottom-right.
    float hx = norm_x;
    float hy = norm_y;

    switch (orientation) {
    case 90: // Landscape Left (device rotated 90 deg counter-clockwise / screen rendered 90 deg CW)
        hx = norm_y;
        hy = 1.0f - norm_x;
        break;
    case 180: // Portrait Inverted
        hx = 1.0f - norm_x;
        hy = 1.0f - norm_y;
        break;
    case 270: // Landscape Right
        hx = 1.0f - norm_y;
        hy = norm_x;
        break;
    case 0:  // Standard Portrait
    default:
        hx = norm_x;
        hy = norm_y;
        break;
    }

    out_x = static_cast<uint16_t>(std::clamp(std::round(hx * 65535.0f), 0.0f, 65535.0f));
    out_y = static_cast<uint16_t>(std::clamp(std::round(hy * 65535.0f), 0.0f, 65535.0f));
    return true;
}

} // namespace duwn::wired
