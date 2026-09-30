#pragma once
// VideoIpcProtocol.h — Binary contract for direct shared-memory video access unit transfer.
//
// Process Boundary:
//   Producer: AirPlay sidecar (UxPlay or standalone IPC producer process)
//   Consumer: DUWN Mirror media engine
//
// Zero GPL contamination:
//   This file contains pure binary layout declarations, POD types, and protocol constants.
//   It is shared across the process boundary via standard Win32 shared memory and event primitives.

#include <cstdint>
#include <cstddef>

namespace duwn::ipc {

// Magic identifier: 'D' 'W' 'A' 'U' (0x55415744 in Little Endian)
constexpr uint32_t kIpcVideoMagic = 0x55415744;

// Protocol Version
constexpr uint32_t kIpcVideoProtocolVersion = 1;

// Codec enumeration
enum class IpcVideoCodec : uint16_t {
    Unknown = 0,
    H264    = 1,
    H265    = 2,
};

// Access Unit Flag bits
enum IpcVideoFlags : uint16_t {
    None         = 0,
    Keyframe     = 1 << 0, // IDR / contains SPS/PPS/VPS
    HasSps       = 1 << 1,
    HasPps       = 1 << 2,
    HasVps       = 1 << 3,
    Discontinuity= 1 << 4,
};

// Binary header preceding each access unit in the ring payload area
#pragma pack(push, 1)
struct IpcAccessUnitHeader {
    uint32_t      magic{kIpcVideoMagic};             // 'DWAU'
    uint32_t      protocol_version{kIpcVideoProtocolVersion};
    uint64_t      sequence_number{0};                // Monotonically increasing AU sequence
    uint16_t      codec{static_cast<uint16_t>(IpcVideoCodec::H264)};
    uint16_t      flags{IpcVideoFlags::None};
    uint32_t      format_generation{0};              // Format generation for geometry changes
    uint64_t      pts_90khz{0};                      // 90 kHz clock PTS (AirPlay / RTP standard)
    int64_t       pts_ns{0};                         // Monotonic clock PTS in nanoseconds
    int64_t       dts_ns{0};                         // Monotonic clock DTS in nanoseconds
    int64_t       producer_send_ns{0};               // QPC timestamp when producer wrote this AU
    uint32_t      width{0};                          // Visible/coded width hint (0 if unknown)
    uint32_t      height{0};                         // Visible/coded height hint (0 if unknown)
    uint32_t      payload_size{0};                   // Size of the raw NAL payload in bytes
    uint32_t      checksum{0};                       // 32-bit FNV-1a or CRC32 of payload (0 if unchecksummed)
};
#pragma pack(pop)

static_assert(sizeof(IpcAccessUnitHeader) == 72, "IpcAccessUnitHeader must be exactly 72 bytes");

// Shared Memory Ring Layout:
// [ IpcRingControlBlock (64 bytes cache-aligned) ]
// [ Slot Descriptors / Ring Buffer Data Area ]

// Ring sizing defaults
constexpr uint32_t kDefaultRingSlotCount = 8;
constexpr uint32_t kDefaultSlotPayloadSize = 1024 * 1024; // 1 MiB max per AU (1080p60 H264 I-frame safe)

#pragma pack(push, 8)
struct IpcRingControlBlock {
    // Header & Versioning
    uint32_t magic{kIpcVideoMagic};
    uint32_t protocol_version{kIpcVideoProtocolVersion};
    uint32_t slot_count{kDefaultRingSlotCount};
    uint32_t slot_size{sizeof(IpcAccessUnitHeader) + kDefaultSlotPayloadSize};

    // Synchronization indices (Single Producer, Single Consumer)
    // Producer advances write_index after committing slot data.
    // Consumer advances read_index after consuming slot data.
    // Alignment to separate 64-byte cache lines avoids false sharing.
    alignas(64) uint64_t write_index{0};
    alignas(64) uint64_t read_index{0};

    // Health & State Flags
    alignas(64) uint32_t producer_pid{0};
    uint32_t             consumer_pid{0};
    uint64_t             total_frames_written{0};
    uint64_t             total_frames_dropped_producer{0}; // Overwritten because consumer was slow
    uint64_t             total_frames_consumed{0};
};
#pragma pack(pop)

// IPC Shared Resources Naming
inline constexpr const wchar_t* kIpcSharedMemoryName = L"Local\\DuwnMirror_VideoRing_v1";
inline constexpr const wchar_t* kIpcEventDataReadyName = L"Local\\DuwnMirror_VideoDataReady_v1";
inline constexpr const wchar_t* kIpcEventConsumerAckName = L"Local\\DuwnMirror_VideoConsumerAck_v1";

} // namespace duwn::ipc
