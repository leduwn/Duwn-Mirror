#pragma once
// CaptureServer — Shared Memory and IPC synchronization for OBS / TikTok Live Studio export.

#include <windows.h>
#include <string>
#include <string_view>
#include <cstdint>

namespace duwn::capture {

#pragma pack(push, 1)
struct CaptureMemoryHeader {
    char     magic[8];       // "DUWNCAP\0"
    uint32_t version;        // 1
    uint32_t width;          // frame width (e.g. 1920)
    uint32_t height;         // frame height (e.g. 1080)
    uint32_t dxgi_format;    // DXGI_FORMAT_B8G8R8A8_UNORM (87)
    uint64_t shared_handle;  // NT / DXGI shared handle
    uint64_t frame_index;    // monotonically increasing sequence
    int64_t  timestamp_qpc;  // QPC timestamp
    char     source_name[64];// "Duwn Mirror Clean Output"
};
#pragma pack(pop)

class CaptureServer {
public:
    CaptureServer() noexcept = default;
    ~CaptureServer() { Stop(); }

    CaptureServer(const CaptureServer&) = delete;
    CaptureServer& operator=(const CaptureServer&) = delete;

    bool Start(std::wstring_view session_name = L"DUWN_MIRROR_CAPTURE") noexcept;
    void Stop() noexcept;

    bool PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                      uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept;

    bool IsRunning() const noexcept { return m_running; }
    std::wstring SessionName() const noexcept { return m_session_name; }

private:
    std::wstring          m_session_name;
    HANDLE                m_file_mapping{nullptr};
    CaptureMemoryHeader*  m_header{nullptr};
    HANDLE                m_frame_event{nullptr};
    bool                  m_running{false};
};

} // namespace duwn::capture
