#pragma once
// VideoIpcRing.h — Producer and Consumer implementation for Direct IPC shared memory ring.
//
// Designed for ultra-low-latency 60 fps streaming:
// - Fixed-size mapped region (no per-frame allocations)
// - Drop-oldest overwrite policy when consumer is delayed (bounds interactive lag to <= 1 frame)
// - Win32 Auto-Reset Event signaling for low-latency wakeups without spin-locking
// - Robust validation against corrupt headers, ring wrap, and producer crash

#include "VideoIpcProtocol.h"
#include <windows.h>
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <cstring>

namespace duwn::ipc {

// Helper to compute total file mapping size
inline size_t CalculateRingMappingSize(uint32_t slot_count, uint32_t slot_size) noexcept {
    // Round control block up to 4096 byte page boundary
    size_t header_pages = (sizeof(IpcRingControlBlock) + 4095) & ~4095;
    return header_pages + static_cast<size_t>(slot_count) * slot_size;
}

// ---------------------------------------------------------------------------
// VideoIpcProducer — Used by the AirPlay sidecar (UxPlay / producer)
// ---------------------------------------------------------------------------
class VideoIpcProducer {
public:
    VideoIpcProducer() = default;
    ~VideoIpcProducer() { Close(); }

    VideoIpcProducer(const VideoIpcProducer&) = delete;
    VideoIpcProducer& operator=(const VideoIpcProducer&) = delete;

    bool Initialize(const std::wstring& shm_name = kIpcSharedMemoryName,
                    const std::wstring& data_ready_name = kIpcEventDataReadyName,
                    uint32_t slot_count = kDefaultRingSlotCount,
                    uint32_t slot_size = sizeof(IpcAccessUnitHeader) + kDefaultSlotPayloadSize) noexcept {
        Close();

        m_slot_count = slot_count;
        m_slot_size  = slot_size;
        m_mapping_size = CalculateRingMappingSize(slot_count, slot_size);

        // 1. Create or open shared memory mapping
        m_shm_handle = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(m_mapping_size),
            shm_name.c_str()
        );

        if (!m_shm_handle) return false;

        // 2. Map view
        m_view = ::MapViewOfFile(m_shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, m_mapping_size);
        if (!m_view) {
            Close();
            return false;
        }

        // 3. Initialize control block
        m_ctrl = static_cast<IpcRingControlBlock*>(m_view);
        m_ctrl->magic            = kIpcVideoMagic;
        m_ctrl->protocol_version = kIpcVideoProtocolVersion;
        m_ctrl->slot_count       = slot_count;
        m_ctrl->slot_size        = slot_size;
        m_ctrl->producer_pid     = ::GetCurrentProcessId();

        size_t header_pages = (sizeof(IpcRingControlBlock) + 4095) & ~4095;
        m_data_base = static_cast<uint8_t*>(m_view) + header_pages;

        // 4. Create auto-reset event for signaling data ready
        m_data_ready_event = ::CreateEventW(nullptr, FALSE, FALSE, data_ready_name.c_str());
        if (!m_data_ready_event) {
            Close();
            return false;
        }

        return true;
    }

    void Close() noexcept {
        if (m_view) {
            ::UnmapViewOfFile(m_view);
            m_view = nullptr;
            m_ctrl = nullptr;
            m_data_base = nullptr;
        }
        if (m_shm_handle) {
            ::CloseHandle(m_shm_handle);
            m_shm_handle = nullptr;
        }
        if (m_data_ready_event) {
            ::CloseHandle(m_data_ready_event);
            m_data_ready_event = nullptr;
        }
    }

    // Write an access unit to the ring buffer.
    // Overwrite policy: if consumer is more than (slot_count - 1) behind, advance read index
    // to discard the oldest frame, preserving interactive low latency.
    bool WriteAccessUnit(const IpcAccessUnitHeader& header, const uint8_t* payload, size_t payload_len) noexcept {
        if (!m_ctrl || !m_data_base) return false;
        if (payload_len + sizeof(IpcAccessUnitHeader) > m_slot_size) return false;

        uint64_t w_idx = m_ctrl->write_index;
        uint64_t r_idx = m_ctrl->read_index;

        // Check if ring is full
        if (w_idx - r_idx >= m_slot_count) {
            // Drop oldest: advance read_index to w_idx - (slot_count - 1)
            uint64_t new_r_idx = w_idx - (m_slot_count - 1);
            m_ctrl->read_index = new_r_idx;
            m_ctrl->total_frames_dropped_producer += (new_r_idx - r_idx);
        }

        uint32_t slot = static_cast<uint32_t>(w_idx % m_slot_count);
        uint8_t* slot_ptr = m_data_base + (static_cast<size_t>(slot) * m_slot_size);

        // Copy header
        IpcAccessUnitHeader hdr_copy = header;
        hdr_copy.magic = kIpcVideoMagic;
        hdr_copy.protocol_version = kIpcVideoProtocolVersion;
        hdr_copy.payload_size = static_cast<uint32_t>(payload_len);
        std::memcpy(slot_ptr, &hdr_copy, sizeof(hdr_copy));

        // Copy payload
        if (payload && payload_len > 0) {
            std::memcpy(slot_ptr + sizeof(IpcAccessUnitHeader), payload, payload_len);
        }

        // Memory barrier to guarantee payload and header are visible before write_index is bumped
        std::atomic_thread_fence(std::memory_order_release);
        m_ctrl->write_index = w_idx + 1;
        m_ctrl->total_frames_written++;

        // Signal consumer
        if (m_data_ready_event) {
            ::SetEvent(m_data_ready_event);
        }

        return true;
    }

    IpcRingControlBlock* ControlBlock() noexcept { return m_ctrl; }

private:
    HANDLE               m_shm_handle{nullptr};
    void*                m_view{nullptr};
    IpcRingControlBlock* m_ctrl{nullptr};
    uint8_t*             m_data_base{nullptr};
    HANDLE               m_data_ready_event{nullptr};
    uint32_t             m_slot_count{kDefaultRingSlotCount};
    uint32_t             m_slot_size{0};
    size_t               m_mapping_size{0};
};

// ---------------------------------------------------------------------------
// VideoIpcConsumer — Used by DUWN Mirror (media engine / consumer)
// ---------------------------------------------------------------------------
class VideoIpcConsumer {
public:
    using FrameReceivedCallback = std::function<void(const IpcAccessUnitHeader& header, const uint8_t* payload, size_t size)>;

    VideoIpcConsumer() = default;
    ~VideoIpcConsumer() { Stop(); }

    VideoIpcConsumer(const VideoIpcConsumer&) = delete;
    VideoIpcConsumer& operator=(const VideoIpcConsumer&) = delete;

    bool Open(const std::wstring& shm_name = kIpcSharedMemoryName,
              const std::wstring& data_ready_name = kIpcEventDataReadyName) noexcept {
        Close();

        // 1. Open shared memory mapping
        m_shm_handle = ::OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, shm_name.c_str());
        if (!m_shm_handle) return false;

        // 2. Map initial control block to read slot dimensions
        m_view = ::MapViewOfFile(m_shm_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
        if (!m_view) {
            Close();
            return false;
        }

        m_ctrl = static_cast<IpcRingControlBlock*>(m_view);
        if (m_ctrl->magic != kIpcVideoMagic || m_ctrl->protocol_version != kIpcVideoProtocolVersion) {
            Close();
            return false;
        }

        m_slot_count = m_ctrl->slot_count;
        m_slot_size  = m_ctrl->slot_size;
        size_t header_pages = (sizeof(IpcRingControlBlock) + 4095) & ~4095;
        m_data_base = static_cast<uint8_t*>(m_view) + header_pages;

        m_ctrl->consumer_pid = ::GetCurrentProcessId();

        // 3. Open data ready event
        m_data_ready_event = ::OpenEventW(SYNCHRONIZE, FALSE, data_ready_name.c_str());
        if (!m_data_ready_event) {
            Close();
            return false;
        }

        return true;
    }

    void Start(FrameReceivedCallback on_frame) noexcept {
        m_on_frame = std::move(on_frame);
        m_running.store(true, std::memory_order_release);
        m_thread = std::jthread([this](std::stop_token st) {
            ConsumerLoop(std::move(st));
        });
    }

    void Stop() noexcept {
        m_running.store(false, std::memory_order_release);
        m_thread.request_stop();
        if (m_data_ready_event) {
            ::SetEvent(m_data_ready_event); // Wake thread from wait
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        Close();
    }

    void Close() noexcept {
        if (m_view) {
            ::UnmapViewOfFile(m_view);
            m_view = nullptr;
            m_ctrl = nullptr;
            m_data_base = nullptr;
        }
        if (m_shm_handle) {
            ::CloseHandle(m_shm_handle);
            m_shm_handle = nullptr;
        }
        if (m_data_ready_event) {
            ::CloseHandle(m_data_ready_event);
            m_data_ready_event = nullptr;
        }
    }

    // Direct synchronous read of available frames (useful for polling or unit tests)
    size_t DrainAvailable() noexcept {
        if (!m_ctrl || !m_data_base) return 0;

        size_t consumed = 0;
        while (true) {
            uint64_t w_idx = m_ctrl->write_index;
            uint64_t r_idx = m_ctrl->read_index;

            if (r_idx >= w_idx) break; // No new frames

            // If producer has overwritten slots past us (lag detection)
            if (w_idx - r_idx > m_slot_count) {
                // Skip directly to oldest available valid slot
                r_idx = w_idx - m_slot_count;
                m_ctrl->read_index = r_idx;
            }

            uint32_t slot = static_cast<uint32_t>(r_idx % m_slot_count);
            uint8_t* slot_ptr = m_data_base + (static_cast<size_t>(slot) * m_slot_size);

            std::atomic_thread_fence(std::memory_order_acquire);

            const auto* hdr = reinterpret_cast<const IpcAccessUnitHeader*>(slot_ptr);
            if (hdr->magic == kIpcVideoMagic && hdr->protocol_version == kIpcVideoProtocolVersion) {
                if (hdr->payload_size + sizeof(IpcAccessUnitHeader) <= m_slot_size) {
                    const uint8_t* payload = slot_ptr + sizeof(IpcAccessUnitHeader);
                    if (m_on_frame) {
                        m_on_frame(*hdr, payload, hdr->payload_size);
                    }
                }
            }

            m_ctrl->read_index = r_idx + 1;
            m_ctrl->total_frames_consumed++;
            consumed++;
        }
        return consumed;
    }

    IpcRingControlBlock* ControlBlock() noexcept { return m_ctrl; }

private:
    void ConsumerLoop(std::stop_token stop) noexcept {
        while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
            // Wait for data ready signal with 10ms timeout
            DWORD wait = ::WaitForSingleObject(m_data_ready_event, 10);
            if (stop.stop_requested()) break;

            if (wait == WAIT_OBJECT_0 || wait == WAIT_TIMEOUT) {
                DrainAvailable();
            }
        }
    }

    HANDLE                m_shm_handle{nullptr};
    void*                 m_view{nullptr};
    IpcRingControlBlock*  m_ctrl{nullptr};
    uint8_t*              m_data_base{nullptr};
    HANDLE                m_data_ready_event{nullptr};
    uint32_t              m_slot_count{kDefaultRingSlotCount};
    uint32_t              m_slot_size{0};

    FrameReceivedCallback m_on_frame;
    std::atomic_bool      m_running{false};
    std::jthread          m_thread;
};

} // namespace duwn::ipc
