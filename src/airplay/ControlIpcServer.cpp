#include "ControlIpcServer.h"
#include "common/logging/Logger.h"
#include <vector>

namespace duwn::airplay {

ControlIpcServer::ControlIpcServer(std::wstring pipe_name) noexcept
    : m_pipe_name(std::move(pipe_name)) {}

ControlIpcServer::~ControlIpcServer() {
    Stop();
}

bool ControlIpcServer::Start() noexcept {
    if (m_running.load(std::memory_order_acquire)) return true;
    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this](std::stop_token st) {
        ServerLoop(std::move(st));
    });
    return true;
}

void ControlIpcServer::Stop() noexcept {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    m_thread.request_stop();

    // Cancel pending IO or unblock pipe
    if (m_pipe_handle != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(m_pipe_handle, nullptr);
        ::DisconnectNamedPipe(m_pipe_handle);
        ::CloseHandle(m_pipe_handle);
        m_pipe_handle = INVALID_HANDLE_VALUE;
    }

    m_connected.store(false, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ControlIpcServer::SendMessage(const ControlMessage& msg) noexcept {
    if (!m_connected.load(std::memory_order_acquire) || m_pipe_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<uint8_t> frame = ControlIpcCodec::EncodeFrame(msg);
    DWORD written = 0;
    BOOL ok = ::WriteFile(m_pipe_handle, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr);
    return ok && written == frame.size();
}

void ControlIpcServer::ServerLoop(std::stop_token stop) noexcept {
    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
        // Create named pipe instance
        m_pipe_handle = ::CreateNamedPipeW(
            m_pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,                  // 1 max instance
            8192,               // out buffer
            8192,               // in buffer
            1000,               // default timeout ms
            nullptr
        );

        if (m_pipe_handle == INVALID_HANDLE_VALUE) {
            DWORD err = ::GetLastError();
            DUWN_LOG_ERRORF("ControlIpcServer", "CreateNamedPipeW failed: error {}", err);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        OVERLAPPED connect_ov{};
        connect_ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ::ConnectNamedPipe(m_pipe_handle, &connect_ov);
        if (!connected) {
            DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING) {
                while (!stop.stop_requested()) {
                    DWORD wait = ::WaitForSingleObject(connect_ov.hEvent, 250);
                    if (wait == WAIT_OBJECT_0) {
                        connected = TRUE;
                        break;
                    }
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }
        ::CloseHandle(connect_ov.hEvent);

        if (stop.stop_requested()) {
            ::CloseHandle(m_pipe_handle);
            m_pipe_handle = INVALID_HANDLE_VALUE;
            break;
        }

        if (!connected) {
            ::CloseHandle(m_pipe_handle);
            m_pipe_handle = INVALID_HANDLE_VALUE;
            continue;
        }

        m_connected.store(true, std::memory_order_release);
        DUWN_LOG_INFO("ControlIpcServer", "Sidecar connected to control pipe");

        // Read loop for messages
        OVERLAPPED read_ov{};
        read_ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

        while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
            // Read 4-byte LE length
            uint8_t len_buf[4]{};
            DWORD bytes_read = 0;
            ::ResetEvent(read_ov.hEvent);

            BOOL ok = ::ReadFile(m_pipe_handle, len_buf, 4, &bytes_read, &read_ov);
            if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
                while (!stop.stop_requested()) {
                    DWORD wait = ::WaitForSingleObject(read_ov.hEvent, 250);
                    if (wait == WAIT_OBJECT_0) {
                        ok = ::GetOverlappedResult(m_pipe_handle, &read_ov, &bytes_read, FALSE);
                        break;
                    }
                }
            }

            if (stop.stop_requested() || !ok || bytes_read < 4) {
                break; // Disconnect or error
            }

            uint32_t payload_len = static_cast<uint32_t>(len_buf[0]) |
                                   (static_cast<uint32_t>(len_buf[1]) << 8) |
                                   (static_cast<uint32_t>(len_buf[2]) << 16) |
                                   (static_cast<uint32_t>(len_buf[3]) << 24);

            if (payload_len == 0 || payload_len > 65536) {
                DUWN_LOG_WARNF("ControlIpcServer", "Invalid control payload length: {}", payload_len);
                break;
            }

            std::vector<uint8_t> payload(payload_len);
            uint32_t total_received = 0;

            while (total_received < payload_len && !stop.stop_requested()) {
                ::ResetEvent(read_ov.hEvent);
                DWORD chunk_read = 0;
                ok = ::ReadFile(m_pipe_handle, payload.data() + total_received,
                                payload_len - total_received, &chunk_read, &read_ov);
                if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
                    while (!stop.stop_requested()) {
                        DWORD wait = ::WaitForSingleObject(read_ov.hEvent, 250);
                        if (wait == WAIT_OBJECT_0) {
                            ok = ::GetOverlappedResult(m_pipe_handle, &read_ov, &chunk_read, FALSE);
                            break;
                        }
                    }
                }
                if (!ok || chunk_read == 0) break;
                total_received += chunk_read;
            }

            if (total_received == payload_len) {
                ControlMessage msg{};
                if (ControlIpcCodec::DecodeFrame(payload.data(), payload.size(), msg)) {
                    if (m_callback) {
                        m_callback(msg);
                    }
                }
            } else {
                break;
            }
        }

        ::CloseHandle(read_ov.hEvent);
        m_connected.store(false, std::memory_order_release);
        DUWN_LOG_INFO("ControlIpcServer", "Sidecar disconnected from control pipe");

        ::DisconnectNamedPipe(m_pipe_handle);
        ::CloseHandle(m_pipe_handle);
        m_pipe_handle = INVALID_HANDLE_VALUE;
    }
}

} // namespace duwn::airplay
