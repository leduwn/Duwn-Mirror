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

    std::memset(m_header, 0, sizeof(CaptureMemoryHeader));
    std::memcpy(m_header->magic, "DUWNCAP", 8);
    m_header->version = 1;
    const char kDefName[] = "Duwn Mirror Clean Output";
    std::memcpy(m_header->source_name, kDefName, sizeof(kDefName));

    m_frame_event = ::CreateEventW(nullptr, FALSE, FALSE, event_name.c_str());
    if (!m_frame_event) {
        DUWN_LOG_ERRORF("CaptureServer", "CreateEvent failed ({:#010x})", ::GetLastError());
        Stop();
        return false;
    }

    m_running = true;
    DUWN_LOG_INFOF("CaptureServer", "Export server active: ipc_name={}", WideToUtf8(m_session_name));
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

bool CaptureServer::PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                                  uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept {
    if (!m_running || !m_header) return false;

    m_header->width         = width;
    m_header->height        = height;
    m_header->dxgi_format   = dxgi_format;
    m_header->shared_handle = reinterpret_cast<uint64_t>(shared_handle);
    m_header->frame_index   = frame_index;
    m_header->timestamp_qpc = timestamp_qpc;

    if (m_frame_event) {
        ::SetEvent(m_frame_event);
    }
    return true;
}

} // namespace duwn::capture
