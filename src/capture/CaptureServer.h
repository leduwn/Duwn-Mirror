#pragma once
// CaptureServer — Shared Memory and IPC synchronization for OBS / TikTok Live Studio export.
// Provides lockless seqlock reads, broadcast notification, adapter identity, and ring-buffer tracking.

#include <windows.h>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>

namespace duwn::capture {

#pragma pack(push, 1)
struct CaptureMemoryHeader {
    char     magic[8];              // "DUWNCAP\0"
    uint32_t protocol_version;      // 2
    uint32_t generation;            // Incremented on server Start() / restart
    uint32_t resource_generation;   // Incremented whenever textures are recreated (resize/format change)

    // Adapter identity
    uint32_t adapter_luid_low;      // DXGI Adapter LUID
    int32_t  adapter_luid_high;

    // Dimensions and format
    uint32_t width;                 // 1920
    uint32_t height;                // 1080
    uint32_t dxgi_format;           // DXGI_FORMAT_B8G8R8A8_UNORM (87)

    // Ring buffer handles (triple buffering)
    uint32_t ring_buffer_count;     // 3
    uint32_t active_buffer_index;   // 0, 1, 2
    uint64_t shared_handles[4];     // Legacy DXGI shared handles

    // Frame sequence and timing
    uint64_t frame_index;           // Monotonically increasing sequence
    int64_t  timestamp_qpc;         // QPC ticks

    // Seqlock protection: odd = write in progress, even = stable
    volatile uint32_t seqlock;      // Incremented at start and end of publish
    uint32_t          reserved_seq; // Padding / alignment

    char     source_name[64];       // "Duwn Mirror Video"
};
#pragma pack(pop)

// Lockless consistent header read using Seqlock pattern
inline bool ReadHeaderConsistent(const CaptureMemoryHeader* src, CaptureMemoryHeader& out, int max_spins = 100) noexcept {
    if (!src) return false;
    for (int i = 0; i < max_spins; ++i) {
        uint32_t s1 = src->seqlock;
        if (s1 & 1) { YieldProcessor(); continue; } // Write in progress
        std::memcpy(&out, src, sizeof(CaptureMemoryHeader));
        ::MemoryBarrier();
        uint32_t s2 = src->seqlock;
        if (s1 == s2 && !(s1 & 1)) {
            return true;
        }
        YieldProcessor();
    }
    return false;
}

class CaptureServer {
public:
    CaptureServer() noexcept = default;
    ~CaptureServer() { Stop(); }

    CaptureServer(const CaptureServer&) = delete;
    CaptureServer& operator=(const CaptureServer&) = delete;

    bool Start(std::wstring_view session_name = L"DUWN_MIRROR_CAPTURE") noexcept;
    void Stop() noexcept;

    // Multi-buffer ring publish with adapter identity and seqlock protection
    bool PublishFrame(const HANDLE* ring_handles, uint32_t ring_count, uint32_t active_index,
                      uint32_t width, uint32_t height, uint32_t dxgi_format,
                      uint32_t resource_gen, LUID adapter_luid,
                      uint64_t frame_index, int64_t timestamp_qpc) noexcept;

    // Single-handle backward compatibility
    bool PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                      uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept;

    bool IsRunning() const noexcept { return m_running; }
    std::wstring SessionName() const noexcept { return m_session_name; }
    uint32_t Generation() const noexcept { return m_generation; }

private:
    std::wstring          m_session_name;
    HANDLE                m_file_mapping{nullptr};
    CaptureMemoryHeader*  m_header{nullptr};
    HANDLE                m_frame_event{nullptr};
    uint32_t              m_generation{0};
    bool                  m_running{false};
};

} // namespace duwn::capture
