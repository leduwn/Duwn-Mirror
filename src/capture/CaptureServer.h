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
struct ConsumerSlot {
    volatile uint32_t process_id;      // Windows Process ID of consumer
    volatile uint32_t active;          // 1 if active/registered, 0 if free
    volatile uint32_t held_ring_index; // 0xFFFFFFFF if not holding any ring slot, else 0..3
    volatile int64_t  acquire_qpc;     // QPC timestamp when ring slot was acquired
    wchar_t           event_name[64];  // Dedicated auto-reset event name: "Local\DUWN_FRAME_EVENT_<PID>"
};

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

    // Producer slot reuse protection and registered consumers
    ConsumerSlot      consumers[4];    // Support up to 4 concurrent consumers (OBS, TikTok, Harnesses)
    volatile uint32_t dropped_exports; // Counter of dropped export frames when all slots locked
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

// Consumer registration and ring slot lease helpers
inline int32_t RegisterConsumer(CaptureMemoryHeader* header, uint32_t pid, const wchar_t* event_name) noexcept {
    if (!header) return -1;
    for (int i = 0; i < 4; ++i) {
        // If slot inactive or process died, reclaim
        if (header->consumers[i].active) {
            HANDLE hProc = ::OpenProcess(SYNCHRONIZE, FALSE, header->consumers[i].process_id);
            if (!hProc) {
                header->consumers[i].active = 0;
                header->consumers[i].held_ring_index = 0xFFFFFFFF;
            } else {
                DWORD exit_code = 0;
                if (::GetExitCodeProcess(hProc, &exit_code) && exit_code != STILL_ACTIVE) {
                    header->consumers[i].active = 0;
                    header->consumers[i].held_ring_index = 0xFFFFFFFF;
                }
                ::CloseHandle(hProc);
            }
        }

        if (header->consumers[i].active == 0) {
            header->consumers[i].process_id = pid;
            header->consumers[i].held_ring_index = 0xFFFFFFFF;
            header->consumers[i].acquire_qpc = 0;
            if (event_name) {
                wcsncpy_s(const_cast<wchar_t*>(header->consumers[i].event_name), 64, event_name, _TRUNCATE);
            } else {
                header->consumers[i].event_name[0] = L'\0';
            }
            ::MemoryBarrier();
            header->consumers[i].active = 1;
            return i;
        }
    }
    return -1;
}

inline void UnregisterConsumer(CaptureMemoryHeader* header, int32_t slot_idx) noexcept {
    if (!header || slot_idx < 0 || slot_idx >= 4) return;
    header->consumers[slot_idx].held_ring_index = 0xFFFFFFFF;
    header->consumers[slot_idx].active = 0;
    ::MemoryBarrier();
}

inline void AcquireRingSlot(CaptureMemoryHeader* header, int32_t slot_idx, uint32_t ring_idx, int64_t now_qpc) noexcept {
    if (!header || slot_idx < 0 || slot_idx >= 4) return;
    header->consumers[slot_idx].acquire_qpc = now_qpc;
    header->consumers[slot_idx].held_ring_index = ring_idx;
    ::MemoryBarrier();
}

inline void ReleaseRingSlot(CaptureMemoryHeader* header, int32_t slot_idx) noexcept {
    if (!header || slot_idx < 0 || slot_idx >= 4) return;
    header->consumers[slot_idx].held_ring_index = 0xFFFFFFFF;
    ::MemoryBarrier();
}

class CaptureServer {
public:
    CaptureServer() noexcept = default;
    ~CaptureServer() { Stop(); }

    CaptureServer(const CaptureServer&) = delete;
    CaptureServer& operator=(const CaptureServer&) = delete;

    bool Start(std::wstring_view session_name = L"DUWN_MIRROR_CAPTURE") noexcept;
    void Stop() noexcept;

    // Multi-buffer ring publish with adapter identity, seqlock protection, and private auto-reset client signaling
    bool PublishFrame(const HANDLE* ring_handles, uint32_t ring_count, uint32_t active_index,
                      uint32_t width, uint32_t height, uint32_t dxgi_format,
                      uint32_t resource_gen, LUID adapter_luid,
                      uint64_t frame_index, int64_t timestamp_qpc) noexcept;

    // Backward compatibility single-handle publish
    bool PublishFrame(HANDLE shared_handle, uint32_t width, uint32_t height,
                      uint32_t dxgi_format, uint64_t frame_index, int64_t timestamp_qpc) noexcept;

    // Select next ring slot that is not locked by any active consumer. Returns 0xFFFFFFFF if all locked.
    uint32_t SelectNextAvailableSlot(uint32_t ring_count, uint32_t last_slot,
                                     int64_t now_qpc, int64_t lease_ticks) noexcept;

    bool IsRunning() const noexcept { return m_running; }
    std::wstring SessionName() const noexcept { return m_session_name; }
    uint32_t Generation() const noexcept { return m_generation; }
    CaptureMemoryHeader* Header() const noexcept { return m_header; }

private:
    std::wstring          m_session_name;
    HANDLE                m_file_mapping{nullptr};
    CaptureMemoryHeader*  m_header{nullptr};
    HANDLE                m_frame_event{nullptr};
    uint32_t              m_generation{0};
    bool                  m_running{false};
};

} // namespace duwn::capture
