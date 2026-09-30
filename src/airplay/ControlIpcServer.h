#pragma once
// ControlIpcServer.h — Named pipe IPC server hosting the dedicated low-rate control channel.
// Reads 4-byte LE length-prefixed UTF-8 JSON messages and dispatches them to callbacks.

#include "ControlIpc.h"
#include <functional>
#include <atomic>
#include <thread>
#include <string>
#include <windows.h>

namespace duwn::airplay {

using ControlMessageCallback = std::function<void(const ControlMessage&)>;

class ControlIpcServer {
public:
    explicit ControlIpcServer(std::wstring pipe_name = L"\\\\.\\pipe\\duwn-mirror-control") noexcept;
    ~ControlIpcServer();

    ControlIpcServer(const ControlIpcServer&) = delete;
    ControlIpcServer& operator=(const ControlIpcServer&) = delete;

    // Sets callback for received control messages
    void SetMessageCallback(ControlMessageCallback cb) noexcept {
        m_callback = std::move(cb);
    }

    // Starts background pipe listener thread
    bool Start() noexcept;

    // Stops pipe server and disconnects client
    void Stop() noexcept;

    // Sends control message to connected sidecar
    bool SendMessage(const ControlMessage& msg) noexcept;

    bool IsClientConnected() const noexcept {
        return m_connected.load(std::memory_order_acquire);
    }

    const std::wstring& PipeName() const noexcept { return m_pipe_name; }

private:
    void ServerLoop(std::stop_token stop) noexcept;

    std::wstring           m_pipe_name;
    ControlMessageCallback m_callback;
    HANDLE                 m_pipe_handle{INVALID_HANDLE_VALUE};
    std::atomic<bool>      m_running{false};
    std::atomic<bool>      m_connected{false};
    std::jthread           m_thread;
};

} // namespace duwn::airplay
