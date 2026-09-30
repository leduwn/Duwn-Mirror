// test_cpu_capabilities.cpp — Unit tests for universal x64 CPU detection.

#include "system/CpuCapabilities.h"
#include <string>

using namespace duwn::common;

// ---------------------------------------------------------------------------
// 1. Singleton returns non-null valid data
// ---------------------------------------------------------------------------
DUWN_TEST(CpuCapabilities_SingletonValid) {
    const auto& cpu = CpuCapabilities::Get();

    // Vendor string must be non-empty and at least 3 chars (e.g. GenuineIntel, AuthenticAMD)
    DUWN_ASSERT(!cpu.vendor_string.empty());
    DUWN_ASSERT(cpu.vendor_string.length() >= 3);

    // Brand string must be non-empty
    DUWN_ASSERT(!cpu.brand_string.empty());

    // Logical processor count must be at least 1
    DUWN_ASSERT(cpu.logical_cores >= 1);
}

// ---------------------------------------------------------------------------
// 2. x86-64 architecture baseline: SSE2 must always be true
// ---------------------------------------------------------------------------
DUWN_TEST(CpuCapabilities_X64Baseline_SSE2) {
    const auto& cpu = CpuCapabilities::Get();

    // All x86-64 CPUs support SSE2 by architectural specification
    DUWN_ASSERT(cpu.sse2 == true);
}

// ---------------------------------------------------------------------------
// 3. Vendor mapping
// ---------------------------------------------------------------------------
DUWN_TEST(CpuCapabilities_VendorIdentified) {
    const auto& cpu = CpuCapabilities::Get();

    if (cpu.vendor_string == "GenuineIntel") {
        DUWN_ASSERT(cpu.vendor == CpuVendor::Intel);
    } else if (cpu.vendor_string == "AuthenticAMD") {
        DUWN_ASSERT(cpu.vendor == CpuVendor::Amd);
    } else {
        DUWN_ASSERT(cpu.vendor == CpuVendor::Other);
    }
}
