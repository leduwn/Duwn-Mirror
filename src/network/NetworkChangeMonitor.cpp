#include "NetworkChangeMonitor.h"
#include "common/logging/Logger.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <format>

#pragma comment(lib, "iphlpapi.lib")

namespace duwn::network {

NetworkChangeMonitor::NetworkChangeMonitor(
    NetworkChangeCallback callback,
    std::chrono::milliseconds debounce_delay) noexcept
    : m_callback(std::move(callback))
    , m_debounce_delay(debounce_delay)
{
}

NetworkChangeMonitor::~NetworkChangeMonitor() {
    Stop();
}

bool NetworkChangeMonitor::Start(const NetworkEnvironmentInfo* initial_env) noexcept {
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    // Capture baseline environment (reuse existing probe if supplied to eliminate ~328 ms startup regression)
    {
        std::lock_guard lock(m_env_mutex);
        if (initial_env) {
            m_last_env = *initial_env;
        } else {
            m_last_env = NetworkEnvironmentInfo::Probe();
        }
    }

    // Register with Windows IP Helper for interface state/address notifications
    HANDLE handle = nullptr;
    DWORD status = ::NotifyIpInterfaceChange(
        AF_UNSPEC,
        reinterpret_cast<PIPINTERFACE_CHANGE_CALLBACK>(&NetworkChangeMonitor::OnInterfaceChanged),
        this,
        FALSE,
        &handle
    );

    if (status != NO_ERROR) {
        DUWN_LOG_WARNF("Network", "NotifyIpInterfaceChange failed (status={}); falling back to periodic evaluation", status);
        m_notification_handle = nullptr;
    } else {
        m_notification_handle = handle;
        DUWN_LOG_INFO("Network", "Native Windows IP change notification registered successfully");
    }

    // Launch worker supervision thread
    m_worker_thread = std::jthread([this](std::stop_token st) {
        WorkerLoop(st);
    });

    return true;
}

void NetworkChangeMonitor::Stop() noexcept {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (m_notification_handle) {
        ::CancelMibChangeNotify2(static_cast<HANDLE>(m_notification_handle));
        m_notification_handle = nullptr;
    }

    if (m_worker_thread.joinable()) {
        m_worker_thread.request_stop();
        m_cv.notify_all();
        m_worker_thread.join();
    }

    DUWN_LOG_INFO("Network", "NetworkChangeMonitor stopped");
}

void NetworkChangeMonitor::TriggerEvaluation() noexcept {
    m_last_event_tick_ms.store(static_cast<int64_t>(::GetTickCount64()), std::memory_order_release);
    m_pending_event.store(true, std::memory_order_release);
    m_cv.notify_all();
}

void __stdcall NetworkChangeMonitor::OnInterfaceChanged(
    void* caller_context,
    void* /*row*/,
    int /*notification_type*/) noexcept
{
    auto* self = static_cast<NetworkChangeMonitor*>(caller_context);
    if (!self || !self->m_running.load(std::memory_order_relaxed)) return;

    self->m_last_event_tick_ms.store(static_cast<int64_t>(::GetTickCount64()), std::memory_order_release);
    self->m_pending_event.store(true, std::memory_order_release);
    self->m_cv.notify_all();
}

bool NetworkChangeMonitor::HasTopologyChanged(const NetworkEnvironmentInfo& a, const NetworkEnvironmentInfo& b) noexcept {
    if (a.best_adapter_ip != b.best_adapter_ip) return true;
    if (a.best_adapter_name != b.best_adapter_name) return true;
    if (a.best_adapter_class != b.best_adapter_class) return true;
    if (a.primary_profile != b.primary_profile) return true;
    if (a.is_public_profile != b.is_public_profile) return true;
    if (a.active_physical_count != b.active_physical_count) return true;
    if (a.active_vpn_count != b.active_vpn_count) return true;
    if (a.network_isolation_suspected != b.network_isolation_suspected) return true;
    return false;
}

void NetworkChangeMonitor::WorkerLoop(std::stop_token stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        std::unique_lock lock(m_mutex);

        // Wait for an event or stop signal
        m_cv.wait(lock, stop_token, [this] {
            return m_pending_event.load(std::memory_order_acquire);
        });

        if (stop_token.stop_requested()) break;

        // Debounce loop: wait until no events have fired for at least m_debounce_delay
        while (!stop_token.stop_requested()) {
            int64_t now_ms = static_cast<int64_t>(::GetTickCount64());
            int64_t last_ms = m_last_event_tick_ms.load(std::memory_order_acquire);
            int64_t elapsed = now_ms - last_ms;

            if (elapsed >= m_debounce_delay.count()) {
                // Debounce quiet period satisfied
                m_pending_event.store(false, std::memory_order_release);
                break;
            }

            int64_t remain_ms = m_debounce_delay.count() - elapsed;
            if (remain_ms <= 0) remain_ms = 50;

            m_cv.wait_for(lock, stop_token, std::chrono::milliseconds(remain_ms), [] {
                return false;
            });
        }

        if (stop_token.stop_requested()) break;

        // Run full network environment probe
        auto new_env = NetworkEnvironmentInfo::Probe();

        bool changed = false;
        {
            std::lock_guard env_lock(m_env_mutex);
            if (HasTopologyChanged(m_last_env, new_env)) {
                changed = true;
                DUWN_LOG_INFOF("Network",
                    "Network topology transition detected: [Adapter: '{}' ({}) IP: '{}' Profile: {}] -> [Adapter: '{}' ({}) IP: '{}' Profile: {}]",
                    m_last_env.best_adapter_ip.empty() ? "None" : m_last_env.best_adapter_ip,
                    AdapterClassificationToString(m_last_env.best_adapter_class),
                    m_last_env.best_adapter_ip,
                    static_cast<int>(m_last_env.primary_profile),
                    new_env.best_adapter_ip.empty() ? "None" : new_env.best_adapter_ip,
                    AdapterClassificationToString(new_env.best_adapter_class),
                    new_env.best_adapter_ip,
                    static_cast<int>(new_env.primary_profile));
                m_last_env = new_env;
            }
        }

        if (changed && m_callback) {
            m_callback(new_env);
        }
    }
}

} // namespace duwn::network
