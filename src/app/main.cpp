// DUWN Mirror — entry point
// DPI awareness: set by manifest (PerMonitorV2). The API call below is a
// belt-and-suspenders fallback in case the manifest is stripped or mis-embedded.
// Must happen before any HWND is created.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "ws2_32.lib")

#include "App.h"
#include "CrashHandler.h"
#include "network/NetworkEnvironment.h"
#include "common/telemetry/ConnectionTimeline.h"
#include "common/clock/MonotonicClock.h"
#include "common/logging/Logger.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Initialize monotonic clock and logger first
    duwn::clock::MonotonicClock::Initialize();
    duwn::Logger::Initialize();

    // Record C0: application process start
    duwn::telemetry::ConnectionTimeline::Get().Record(duwn::telemetry::ConnectionMilestone::C0_ProcessStart);

    // Install unhandled exception filter first
    duwn::app::CrashHandler::Install();

    // Check for fixed firewall helper command before creating any application state
    int argc = 0;
    bool test_motion = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 3 && (_wcsicmp(argv[1], L"--firewall") == 0 || _wcsicmp(argv[1], L"-firewall") == 0)) {
            std::wstring action = argv[2];
            ::LocalFree(argv);
            return duwn::network::ExecuteFirewallCliCommand(action);
        }
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--test-motion") == 0 || _wcsicmp(argv[i], L"-test-motion") == 0) {
                test_motion = true;
            }
        }
        ::LocalFree(argv);
    }

    // Manifest declares PerMonitorV2; this call handles the rare case where
    // the manifest embedding fails (e.g. resource-only DLL injection, test harness).
    // Silently ignored if the manifest already applied the setting.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Global Winsock initialization to prevent provider DLL unload/reload thrashing during startup
    WSADATA wsa_data{};
    int wsa_res = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);

    duwn::app::App app;
    int ret = app.Run(test_motion);

    if (wsa_res == 0) {
        ::WSACleanup();
    }
    return ret;
}
