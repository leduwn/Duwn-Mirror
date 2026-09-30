#pragma once
// CpuCapabilities.h — Universal x64 CPU detection and capability probing.
// Uses __cpuid/__cpuidex for runtime instruction set feature detection.
// All SIMD code paths must be guarded by runtime checks.

#include <string>
#include <cstdint>

namespace duwn::common {

enum class CpuVendor {
    Intel,
    Amd,
    Other
};

struct CpuCapabilities {
    CpuVendor   vendor{CpuVendor::Other};
    std::string vendor_string;
    std::string brand_string;

    bool sse2{false};
    bool sse3{false};
    bool ssse3{false};
    bool sse4_1{false};
    bool sse4_2{false};
    bool avx{false};
    bool avx2{false};
    bool fma{false};
    bool aes{false};
    bool pclmul{false};
    bool bmi1{false};
    bool bmi2{false};

    uint32_t logical_cores{1};

    // Meyers' singleton — probed once on first access.
    static const CpuCapabilities& Get() noexcept;

    // Emits structured log at startup.
    void LogCapabilities() const noexcept;
};

} // namespace duwn::common
