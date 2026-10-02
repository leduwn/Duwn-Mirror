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

    // Broadcast wake up to all waiting consumers
    if (m_frame_event) {
        ::SetEvent(m_frame_event);
    }
    return true;
}

bool CaptureServer::PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                                 uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept {
    HANDLE handles[1] = { shared_handle };
    LUID dummy_luid{0, 0};
    return PublishFrame(handles, 1, 0, width, height, dxgi_format, 1, dummy_luid, frame_index, timestamp_qpc);
}

} // namespace duwn::capture
