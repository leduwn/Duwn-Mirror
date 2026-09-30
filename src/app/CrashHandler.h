#pragma once

#include <windows.h>
#include <string>

namespace duwn::app {

struct CrashContext {
    std::wstring active_mode = L"None";
    std::wstring decoder_name = L"None";
    HWND output_hwnd = nullptr;
    std::wstring gpu_name = L"Unknown";
};

class CrashHandler {
public:
    static void Install();
    static void UpdateContext(const CrashContext& ctx);
    static std::wstring GetCrashDirectory();
    static bool HasPreviousCrash(std::wstring* out_last_crash_file = nullptr);
    static void ClearCrashFlag();

private:
    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exception_info);
    static void WriteCrashReport(EXCEPTION_POINTERS* exception_info, const std::wstring& dmp_path, const std::wstring& txt_path);
};

} // namespace duwn::app
