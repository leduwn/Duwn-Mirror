#pragma once
// NetworkChangeMonitor.h — Native Windows IP Helper network change monitoring
// with debounced evaluation to detect connects, disconnects, and DHCP/profile changes.

#include "NetworkEnvironment.h"
#include <functional>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace duwn::network {

using NetworkChangeCallback = std::function<void(const NetworkEnvironmentInfo& new_env)>;

class NetworkChangeMonitor {
public:
    explicit NetworkChangeMonitor(
        NetworkChangeCallback callback,
        std::chrono::milliseconds debounce_delay = std::chrono::milliseconds(1200)) noexcept;
    ~NetworkChangeMonitor();

    NetworkChangeMonitor(const NetworkChangeMonitor&) = delete;
    NetworkChangeMonitor& operator=(const NetworkChangeMonitor&) = delete;

    bool Start(const NetworkEnvironmentInfo* initial_env = nullptr) noexcept;
    void Stop() noexcept;
    bool IsRunning() const noexcept { return m_running.load(std::memory_order_relaxed); }

    // Manually trigger immediate debounced evaluation
    void TriggerEvaluation() noexcept;

    const NetworkEnvironmentInfo& CurrentEnvironment() const noexcept {
        std::lock_guard lock(m_env_mutex);
        return m_last_env;
    }

private:
    static void __stdcall OnInterfaceChanged(
        void* caller_context,
        void* row,
        int notification_type) noexcept;

    void WorkerLoop(std::stop_token stop_token) noexcept;
    static bool HasTopologyChanged(const NetworkEnvironmentInfo& a, const NetworkEnvironmentInfo& b) noexcept;

    NetworkChangeCallback       m_callback;
    std::chrono::milliseconds   m_debounce_delay{1200};

    std::atomic_bool            m_running{false};
    std::atomic_bool            m_pending_event{false};
    std::atomic<int64_t>        m_last_event_tick_ms{0};

    void*                       m_notification_handle{nullptr};
    std::jthread                m_worker_thread;
    mutable std::mutex          m_mutex;
    std::condition_variable_any m_cv;

    mutable std::mutex          m_env_mutex;
    NetworkEnvironmentInfo      m_last_env;
};

} // namespace duwn::network
