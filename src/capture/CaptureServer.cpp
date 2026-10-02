#include "CaptureServer.h"
#include "common/logging/Logger.h"
#include <cstring>
#include <string>

namespace duwn::capture {

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr, nullptr);
    return out;
}

CaptureServer::CaptureServer() noexcept = default;

bool CaptureServer::Start(std::wstring_view session_name) noexcept {
    Stop();

    m_session_name = session_name.empty() ? L"DUWN_MIRROR_CAPTURE" : std::wstring(session_name);
    std::wstring map_name = L"Local\\" + m_session_name;
    std::wstring event_name = L"Local\\" + m_session_name + L"_FRAME_READY";

    m_file_mapping = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(CaptureMemoryHeader), map_name.c_str());
    if (!m_file_mapping) {
        DUWN_LOG_ERRORF("CaptureServer", "CreateFileMapping failed ({:#010x})", ::GetLastError());
        return false;
    }

    m_header = static_cast<CaptureMemoryHeader*>(
        ::MapViewOfFile(m_file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CaptureMemoryHeader)));
    if (!m_header) {
        DUWN_LOG_ERRORF("CaptureServer", "MapViewOfFile failed ({:#010x})", ::GetLastError());
        Stop();
        return false;
    }

    ++m_generation;
    std::memset(m_header, 0, sizeof(CaptureMemoryHeader));
    std::memcpy(m_header->magic, "DUWNCAP", 8);
    m_header->protocol_version = 2;
    m_header->generation = m_generation;
    m_header->producer_reserved_slot = 0xFFFFFFFF;
    const char kDefName[] = "Duwn Mirror Video";
    std::memcpy(m_header->source_name, kDefName, sizeof(kDefName));

    // Manual-reset event ensures broadcast notification to ALL waiting consumers
    m_frame_event = ::CreateEventW(nullptr, TRUE, FALSE, event_name.c_str());
    if (!m_frame_event) {
        DUWN_LOG_ERRORF("CaptureServer", "CreateEvent failed ({:#010x})", ::GetLastError());
        Stop();
        return false;
    }

    m_running = true;
    DUWN_LOG_INFOF("CaptureServer", "Export server active: ipc_name={} (gen={})",
                   WideToUtf8(m_session_name), m_generation);
    return true;
}

void CaptureServer::Stop() noexcept {
    m_running = false;
    for (int i = 0; i < 4; ++i) {
        if (m_cached_client_events[i]) {
            ::CloseHandle(m_cached_client_events[i]);
            m_cached_client_events[i] = nullptr;
        }
        m_cached_client_pids[i] = 0;
    }
    if (m_header) {
        ::UnmapViewOfFile(m_header);
        m_header = nullptr;
    }
    if (m_file_mapping) {
        ::CloseHandle(m_file_mapping);
        m_file_mapping = nullptr;
    }
    if (m_frame_event) {
        ::CloseHandle(m_frame_event);
        m_frame_event = nullptr;
    }
    m_session_name.clear();
}

bool CaptureServer::PublishFrame(const HANDLE* ring_handles, uint32_t ring_count, uint32_t active_index,
                                 uint32_t width, uint32_t height, uint32_t dxgi_format,
                                 uint32_t resource_gen, LUID adapter_luid,
                                 uint64_t frame_index, int64_t timestamp_qpc) noexcept {
    if (!m_running || !m_header) return false;

    // Reset event before updating header
    if (m_frame_event) {
        ::ResetEvent(m_frame_event);
    }

    // Seqlock write start: odd indicates write in progress
    ::InterlockedIncrement(&m_header->seqlock);
    ::MemoryBarrier();

    m_header->resource_generation = resource_gen;
    m_header->adapter_luid_low    = adapter_luid.LowPart;
    m_header->adapter_luid_high   = adapter_luid.HighPart;
    m_header->width               = width;
    m_header->height              = height;
    m_header->dxgi_format         = dxgi_format;
    m_header->ring_buffer_count   = ring_count;
    m_header->active_buffer_index = active_index;

    for (uint32_t i = 0; i < ring_count && i < 4; ++i) {
        m_header->shared_handles[i] = reinterpret_cast<uint64_t>(ring_handles[i]);
    }

    m_header->frame_index         = frame_index;
    m_header->timestamp_qpc       = timestamp_qpc;

    // Seqlock write finish: even indicates consistent snapshot
    ::MemoryBarrier();
    ::InterlockedIncrement(&m_header->seqlock);

    // Frame published: release producer write reservation
    ::InterlockedExchange(&m_header->producer_reserved_slot, 0xFFFFFFFF);
    ::MemoryBarrier();

    // Broadcast wake up to private registered consumers (auto-reset events)
    for (uint32_t i = 0; i < 4; ++i) {
        if (m_header->consumers[i].active == kConsumerStateActive && m_header->consumers[i].event_name[0] != L'\0') {
            uint32_t pid = m_header->consumers[i].process_id;
            if (m_cached_client_events[i] && m_cached_client_pids[i] != pid) {
                ::CloseHandle(m_cached_client_events[i]);
                m_cached_client_events[i] = nullptr;
                m_cached_client_pids[i] = 0;
            }
            if (!m_cached_client_events[i]) {
                m_cached_client_events[i] = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, m_header->consumers[i].event_name);
                m_cached_client_pids[i] = pid;
            }
            if (m_cached_client_events[i]) {
                ::SetEvent(m_cached_client_events[i]);
            }
        } else if (m_cached_client_events[i]) {
            ::CloseHandle(m_cached_client_events[i]);
            m_cached_client_events[i] = nullptr;
            m_cached_client_pids[i] = 0;
        }
    }

    // Broadcast wake up to legacy/unregistered consumers
    if (m_frame_event) {
        ::SetEvent(m_frame_event);
    }
    return true;
}

uint32_t CaptureServer::SelectNextAvailableSlot(uint32_t ring_count, uint32_t last_slot,
                                                int64_t now_qpc, int64_t lease_ticks,
                                                uint32_t* out_retired_slot) noexcept {
    if (out_retired_slot) *out_retired_slot = 0xFFFFFFFF;
    if (!m_header || ring_count == 0) return 0;
    if (ring_count == 1) return 0;

    // Detect crashed/dead consumers and retire their held texture resources immediately
    for (uint32_t c = 0; c < 4; ++c) {
        uint32_t state = m_header->consumers[c].active;
        if (state == kConsumerStateFree) continue;

        HANDLE hProc = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_header->consumers[c].process_id);
        bool process_dead = false;
        if (!hProc) {
            process_dead = true;
        } else {
            DWORD exit_code = 0;
            if (::GetExitCodeProcess(hProc, &exit_code) && exit_code != STILL_ACTIVE) {
                process_dead = true;
            }
            ::CloseHandle(hProc);
        }

        if (process_dead) {
            uint32_t dead_held = m_header->consumers[c].held_ring_index;
            ::InterlockedExchange(&m_header->consumers[c].held_ring_index, 0xFFFFFFFF);
            ::MemoryBarrier();
            m_header->consumers[c].active = kConsumerStateFree;
            ::MemoryBarrier();

            if (dead_held < ring_count && out_retired_slot && *out_retired_slot == 0xFFFFFFFF) {
                *out_retired_slot = dead_held;
            }
        }
    }

    // Check candidate slots in sequence starting from (last_slot + 1)
    for (uint32_t step = 1; step <= ring_count; ++step) {
        uint32_t cand = (last_slot + step) % ring_count;

        // Dekker reservation: announce intent to write to slot 'cand'
        ::InterlockedExchange(&m_header->producer_reserved_slot, cand);
        ::MemoryBarrier();

        bool locked = false;
        for (uint32_t c = 0; c < 4; ++c) {
            if (m_header->consumers[c].active == kConsumerStateActive) {
                if (m_header->consumers[c].held_ring_index == cand) {
                    locked = true;
                    break;
                }
            }
        }

        if (locked) {
            // Contested by active consumer: clear reservation and check next slot
            ::InterlockedExchange(&m_header->producer_reserved_slot, 0xFFFFFFFF);
            continue;
        }

        // Slot cand successfully reserved for Producer write!
        return cand;
    }

    // All slots locked by slow consumers: clear reservation and drop export frame
    ::InterlockedExchange(&m_header->producer_reserved_slot, 0xFFFFFFFF);
    ::InterlockedIncrement(&m_header->dropped_exports);
    return 0xFFFFFFFF;
}

void CaptureServer::ClearReservation() noexcept {
    if (m_header) {
        ::InterlockedExchange(&m_header->producer_reserved_slot, 0xFFFFFFFF);
        ::MemoryBarrier();
    }
}

void CaptureServer::UpdateSharedHandle(uint32_t ring_index, HANDLE new_handle, uint32_t new_resource_gen) noexcept {
    if (!m_header || ring_index >= 4) return;
    ::InterlockedIncrement(&m_header->seqlock);
    ::MemoryBarrier();
    m_header->shared_handles[ring_index] = reinterpret_cast<uint64_t>(new_handle);
    m_header->resource_generation = new_resource_gen;
    ::MemoryBarrier();
    ::InterlockedIncrement(&m_header->seqlock);
}

bool CaptureServer::PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                                 uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept {
    HANDLE handles[1] = { shared_handle };
    LUID dummy_luid{0, 0};
    return PublishFrame(handles, 1, 0, width, height, dxgi_format, 1, dummy_luid, frame_index, timestamp_qpc);
}

} // namespace duwn::capture
