#include "system/CpuCapabilities.h"
#include "logging/Logger.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <format>

namespace duwn::common {

static std::string Trim(std::string s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

const CpuCapabilities& CpuCapabilities::Get() noexcept {
    static CpuCapabilities caps = []() {
        CpuCapabilities c;

        int cpuInfo[4] = {0};
        __cpuid(cpuInfo, 0);
        int nIds = cpuInfo[0];

        // 1. Vendor string
        char vendor[13] = {0};
        std::memcpy(vendor,     &cpuInfo[1], 4);
        std::memcpy(vendor + 4, &cpuInfo[3], 4);
        std::memcpy(vendor + 8, &cpuInfo[2], 4);
        c.vendor_string = vendor;

        if (c.vendor_string == "GenuineIntel") {
            c.vendor = CpuVendor::Intel;
        } else if (c.vendor_string == "AuthenticAMD") {
            c.vendor = CpuVendor::Amd;
        } else {
            c.vendor = CpuVendor::Other;
        }

        // 2. Feature flags (Function 1)
        if (nIds >= 1) {
            __cpuid(cpuInfo, 1);
            int ecx = cpuInfo[2];
            int edx = cpuInfo[3];

            c.sse2   = (edx & (1 << 26)) != 0;
            c.sse3   = (ecx & (1 << 0))  != 0;
            c.ssse3  = (ecx & (1 << 9))  != 0;
            c.fma    = (ecx & (1 << 12)) != 0;
            c.sse4_1 = (ecx & (1 << 19)) != 0;
            c.sse4_2 = (ecx & (1 << 20)) != 0;
            c.aes    = (ecx & (1 << 25)) != 0;
            c.pclmul = (ecx & (1 << 1))  != 0;

            // Check OSXSAVE and YMM register enablement before checking AVX
            bool osxsave = (ecx & (1 << 27)) != 0;
            bool ymm_supported = false;
            if (osxsave) {
                unsigned long long xcrFeatureMask = _xgetbv(0);
                ymm_supported = (xcrFeatureMask & 0x06) == 0x06; // XMM and YMM state enabled by OS
            }
            c.avx = ((ecx & (1 << 28)) != 0) && ymm_supported;

            // 3. Extended features (Function 7, Subfunction 0)
            if (nIds >= 7) {
                int cpuInfo7[4] = {0};
                __cpuidex(cpuInfo7, 7, 0);
                int ebx7 = cpuInfo7[1];

                c.bmi1 = (ebx7 & (1 << 3)) != 0;
                c.bmi2 = (ebx7 & (1 << 8)) != 0;
                c.avx2 = ((ebx7 & (1 << 5)) != 0) && ymm_supported;
            }
        }

        // 4. Brand string (Functions 0x80000002 - 0x80000004)
        __cpuid(cpuInfo, static_cast<int>(0x80000000));
        unsigned int nExIds = static_cast<unsigned int>(cpuInfo[0]);
        if (nExIds >= 0x80000004) {
            char brand[49] = {0};
            __cpuid(reinterpret_cast<int*>(brand),      static_cast<int>(0x80000002));
            __cpuid(reinterpret_cast<int*>(brand + 16), static_cast<int>(0x80000003));
            __cpuid(reinterpret_cast<int*>(brand + 32), static_cast<int>(0x80000004));
            c.brand_string = Trim(brand);
        } else {
            c.brand_string = c.vendor_string;
        }

        // 5. Logical core count
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        c.logical_cores = si.dwNumberOfProcessors;

        return c;
    }();

    return caps;
}

void CpuCapabilities::LogCapabilities() const noexcept {
    DUWN_LOG_INFOF("CPU", "Vendor: {} | Brand: \"{}\" | Logical Cores: {}",
                   vendor_string, brand_string, logical_cores);
    DUWN_LOG_INFOF("CPU", "Features: SSE2:{} SSE4.1:{} SSE4.2:{} AVX:{} AVX2:{} FMA:{} AES:{} PCLMUL:{} BMI1:{} BMI2:{}",
                   sse2   ? "YES" : "NO",
                   sse4_1 ? "YES" : "NO",
                   sse4_2 ? "YES" : "NO",
                   avx    ? "YES" : "NO",
                   avx2   ? "YES" : "NO",
                   fma    ? "YES" : "NO",
                   aes    ? "YES" : "NO",
                   pclmul ? "YES" : "NO",
                   bmi1   ? "YES" : "NO",
                   bmi2   ? "YES" : "NO");
}

} // namespace duwn::common
