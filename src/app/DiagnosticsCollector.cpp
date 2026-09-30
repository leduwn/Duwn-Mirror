#include "DiagnosticsCollector.h"
#include "common/Version.h"
#include "airplay/AppleModelDatabase.h"
#include <windows.h>
#include <format>
#include <sstream>
#include <regex>

namespace duwn::app {

static std::wstring RedactUdid(const std::wstring& in) {
    static const std::wregex udid40(LR"(\b([0-9a-fA-F]{36})([0-9a-fA-F]{4})\b)");
    std::wstring res = std::regex_replace(in, udid40, L"...$2");
    static const std::wregex udid25(LR"(\b([0-9a-fA-F]{8}-[0-9a-fA-F]{12})([0-9a-fA-F]{4})\b)");
    return std::regex_replace(res, udid25, L"...$2");
}

std::wstring DiagnosticsCollector::BuildReport(const ui::UiState& state, const Settings& settings) {
    std::wstringstream ss;
    ss << L"===========================================================\n";
    ss << L" Duwn Mirror Support Diagnostics\n";
    ss << L"===========================================================\n";
    ss << L"Product:          " << DUWN_PRODUCT_NAME_W << L"\n";
    ss << L"Version:          " << DUWN_VERSION_STRING_W << L"\n";
    ss << L"Platform:         " << DUWN_BUILD_PLATFORM_W << L"\n";
    ss << L"Git Commit:       " << DUWN_GIT_COMMIT_SHORT << L"\n";
    ss << L"Build Timestamp:  " << DUWN_BUILD_TIMESTAMP_W << L"\n";

    // OS info
    OSVERSIONINFOEXW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOEXW);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(::GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) fn(&osvi);
    }
    ss << L"OS Version:       Windows " << osvi.dwMajorVersion << L"." << osvi.dwMinorVersion
       << L" (Build " << osvi.dwBuildNumber << L")\n";

    // Hardware
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    ss << L"CPU Logical:      " << si.dwNumberOfProcessors << L" cores\n";
    ss << L"GPU:              " << (state.gpu_name.empty() ? L"—" : state.gpu_name) << L"\n";

    // Pipeline & Modes
    ss << L"\n--- Pipeline & Session ---\n";
    ss << L"Active Mode:      " << (state.connection_mode == 0 ? L"Wireless AirPlay" : L"Wired USB") << L"\n";
    ss << L"Session Status:   " << state.status_message << L"\n";
    ss << L"Device:           " << RedactUdid(state.device_name) << L"\n";
    ss << L"Device Model:     " << state.model_name << L"\n";
    ss << L"Product Type:     " << state.product_type << L"\n";
    ss << L"OS:               " << state.os_version << L"\n";
    ss << L"Model DB Match:   " << state.model_db_match << L"\n";
    ss << L"Model DB Rev:     " << airplay::AppleModelDatabase::kRevision << L"\n";
    ss << L"Decoder:          " << state.decoder_name << L"\n";
    ss << L"Renderer:         " << state.renderer_name << L"\n";
    ss << L"Output Window:    " << (state.output_window_visible ? L"Visible" : L"Hidden")
       << (state.output_fullscreen ? L" (Fullscreen)" : L" (Windowed)") << L"\n";

    // Stream & Performance
    ss << L"\n--- Metrics ---\n";
    ss << std::format(L"Source Stream:    {}x{} @ {:.1f} FPS (Nominal {:.1f})\n",
        state.coded_width > 0 ? state.coded_width : state.width,
        state.coded_height > 0 ? state.coded_height : state.height,
        state.source_fps, state.nominal_fps);
    ss << std::format(L"Decoded / Render: {:.1f} FPS / {:.1f} FPS\n", state.decoded_fps, state.render_fps);
    ss << std::format(L"Pipeline Latency: {:.1f} ms (Decode: {:.1f} ms)\n", state.pipeline_latency_ms, state.decode_time_ms);
    ss << std::format(L"Resolved Output:  {}x{}\n", state.output_width, state.output_height);
    ss << std::format(L"Frames Presented: {} (Dropped: {})\n", state.total_frames_presented, state.dropped_frames);

    // Audio
    ss << L"\n--- Audio ---\n";
    ss << L"Audio Output:     " << state.resolved_audio_device_name << L"\n";
    ss << L"Audio State:      " << (state.audio_muted ? L"Muted" : (state.audio_active ? L"Active" : L"Inactive")) << L"\n";
    ss << std::format(L"Audio Buffer:     {:.1f} ms (Underruns: {})\n", state.audio_buffer_ms, state.audio_underrun_count);

    // Network & Firewall
    ss << L"\n--- Network & Security ---\n";
    ss << L"Network Profile:  " << (state.is_public_network ? L"Public" : L"Private/Domain") << L"\n";
    ss << L"Public Allowed:   " << (settings.allow_public_networks ? L"Yes" : L"No") << L"\n";
    ss << L"Public Rules:     " << (state.public_rules_missing ? L"Missing" : (state.public_rules_unexpected ? L"Unexpected" : L"OK")) << L"\n";
    ss << L"Transport:        " << state.transport_type << L"\n";
    ss << L"===========================================================\n";

    return ss.str();
}

} // namespace duwn::app
