#pragma once
#include <cstdint>

namespace duwn::video {

enum class PipelineTier {
    FullPerformance, // Tier 1: Hardware H.264 decode + D3D11 texture zero-copy
    Compatibility,   // Tier 2: Software H.264 decode + upload to D3D11
    Emergency,       // Tier 3: WARP software rasterizer
};

constexpr const char* PipelineTierName(PipelineTier tier) noexcept {
    switch (tier) {
    case PipelineTier::FullPerformance: return "Tier 1 — FULL PERFORMANCE";
    case PipelineTier::Compatibility:   return "Tier 2 — COMPATIBILITY";
    case PipelineTier::Emergency:       return "Tier 3 — EMERGENCY";
    }
    return "UNKNOWN";
}

constexpr const char* VendorName(uint32_t vendor_id) noexcept {
    switch (vendor_id) {
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";
    case 0x5143: return "Qualcomm";
    default:     return "Generic/Unknown";
    }
}

} // namespace duwn::video
