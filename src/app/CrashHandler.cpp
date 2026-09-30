#include "CrashHandler.h"
#include "common/Version.h"
#include <dbghelp.h>
#include <shlobj.h>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <mutex>
#include <filesystem>

#pragma comment(lib, "dbghelp.lib")

namespace duwn::app {

namespace {
std::mutex g_context_mutex;
CrashContext g_context;
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;

std::wstring GetLocalAppDataDuwn() {
    PWSTR path = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::wstring res(path);
        ::CoTaskMemFree(path);
        return res + L"\\Duwn Mirror";
    }
    return L".";
}

std::wstring FormatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);
    return std::format(L"{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
}
} // namespace

std::wstring CrashHandler::GetCrashDirectory() {
    std::wstring dir = GetLocalAppDataDuwn() + L"\\Crashes";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void CrashHandler::Install() {
    GetCrashDirectory(); // ensure directory exists
    g_prev_filter = ::SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}

void CrashHandler::UpdateContext(const CrashContext& ctx) {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    g_context = ctx;
}

bool CrashHandler::HasPreviousCrash(std::wstring* out_last_crash_file) {
    std::wstring marker = GetCrashDirectory() + L"\\last_crash.marker";
    if (std::filesystem::exists(marker)) {
        if (out_last_crash_file) {
            std::wifstream f(marker);
            if (f.is_open()) {
                std::getline(f, *out_last_crash_file);
            }
        }
        return true;
    }
    return false;
}

void CrashHandler::ClearCrashFlag() {
    std::wstring marker = GetCrashDirectory() + L"\\last_crash.marker";
    std::error_code ec;
    std::filesystem::remove(marker, ec);
}

LONG WINAPI CrashHandler::UnhandledExceptionFilter(EXCEPTION_POINTERS* exception_info) {
    std::wstring crash_dir = GetCrashDirectory();
    std::wstring ts = FormatTimestamp();
    std::wstring dmp_path = crash_dir + L"\\crash_" + ts + L".dmp";
    std::wstring txt_path = crash_dir + L"\\crash_" + ts + L".txt";

    // Write minidump
    HANDLE file = ::CreateFileW(
        dmp_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = ::GetCurrentThreadId();
        mei.ExceptionPointers = exception_info;
        mei.ClientPointers = FALSE;

        ::MiniDumpWriteDump(
            ::GetCurrentProcess(),
            ::GetCurrentProcessId(),
            file,
            MiniDumpNormal,
            &mei,
            nullptr,
            nullptr
        );
        ::CloseHandle(file);
    }

    WriteCrashReport(exception_info, dmp_path, txt_path);

    // Save marker for crash banner on next launch
    std::wofstream marker(crash_dir + L"\\last_crash.marker");
    if (marker.is_open()) {
        marker << txt_path << std::endl;
    }

    if (g_prev_filter) {
        return g_prev_filter(exception_info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::WriteCrashReport(EXCEPTION_POINTERS* exception_info, const std::wstring& dmp_path, const std::wstring& txt_path) {
    std::wofstream f(txt_path);
    if (!f.is_open()) return;

    CrashContext ctx;
    {
        std::lock_guard<std::mutex> lock(g_context_mutex);
        ctx = g_context;
    }

    f << L"===========================================================" << std::endl;
    f << L" Duwn Mirror Crash Report" << std::endl;
    f << L"===========================================================" << std::endl;
    f << L"Product:      " << DUWN_PRODUCT_NAME_W << std::endl;
    f << L"Version:      " << DUWN_VERSION_STRING_W << std::endl;
    f << L"Platform:     " << DUWN_BUILD_PLATFORM_W << std::endl;
    f << L"Build Commit: " << DUWN_GIT_COMMIT_SHORT << std::endl;
    f << L"Build Time:   " << DUWN_BUILD_TIMESTAMP_W << std::endl;

    // OS Info
    OSVERSIONINFOEXW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOEXW);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(::GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) fn(&osvi);
    }
    f << L"OS:           Windows " << osvi.dwMajorVersion << L"." << osvi.dwMinorVersion
      << L" (Build " << osvi.dwBuildNumber << L")" << std::endl;

    // Hardware
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    f << L"CPU Cores:    " << si.dwNumberOfProcessors << std::endl;
    f << L"GPU:          " << ctx.gpu_name << std::endl;

    // Runtime state
    f << L"Active Mode:  " << ctx.active_mode << std::endl;
    f << L"Decoder:      " << ctx.decoder_name << std::endl;
    f << L"Output HWND:  " << std::format(L"0x{:08X}", reinterpret_cast<uintptr_t>(ctx.output_hwnd)) << std::endl;

    // Exception Info
    if (exception_info && exception_info->ExceptionRecord) {
        DWORD code = exception_info->ExceptionRecord->ExceptionCode;
        void* addr = exception_info->ExceptionRecord->ExceptionAddress;
        f << L"Exception:    " << std::format(L"0x{:08X}", code) << L" at " << addr << std::endl;
    }

    // Stack Backtrace
    f << std::endl << L"Stack Backtrace:" << std::endl;
    void* stack[64];
    USHORT frames = ::CaptureStackBackTrace(0, 64, stack, nullptr);
    for (USHORT i = 0; i < frames; ++i) {
        f << std::format(L"  [{:02d}] 0x{:p}", i, stack[i]) << std::endl;
    }

    f << std::endl;
    f << L"Minidump:     " << dmp_path << std::endl;
    f << L"===========================================================" << std::endl;
}

} // namespace duwn::app
