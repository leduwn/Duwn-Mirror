#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include "AirPlayProcess.h"
#include "SidecarVerificationCache.h"
#include "AppleModelDatabase.h"
#include "common/logging/Logger.h"
#include "common/metrics/Metrics.h"
#include "common/clock/MonotonicClock.h"
#include "common/telemetry/ConnectionTelemetry.h"
#include "common/telemetry/ConnectionTimeline.h"
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <format>
#include <cstdio>
#include <filesystem>
#include <iterator>

namespace duwn::airplay {

static std::string WideToUtf8(std::wstring_view w) noexcept {
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string s(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(std::string_view s) noexcept {
    if (s.empty()) return {};
    int size = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring w(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
    return w;
}

static bool IsProcessListeningOnPort(DWORD pid, uint16_t port1, uint16_t port2) noexcept {
    if (pid == 0) return false;
    DWORD size = 0;
    if (::GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<BYTE> buffer(size);
        if (::GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& entry = table->table[i];
                if (entry.dwOwningPid == pid && entry.dwState == MIB_TCP_STATE_LISTEN) {
                    uint16_t local_port = ::ntohs(static_cast<uint16_t>(entry.dwLocalPort));
                    if (local_port == port1 || local_port == port2) {
                        return true;
                    }
                }
            }
        }
    }

    DWORD size6 = 0;
    if (::GetExtendedTcpTable(nullptr, &size6, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<BYTE> buffer6(size6);
        if (::GetExtendedTcpTable(buffer6.data(), &size6, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            const auto* table6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer6.data());
            for (DWORD i = 0; i < table6->dwNumEntries; ++i) {
                const auto& entry = table6->table[i];
                if (entry.dwOwningPid == pid && entry.dwState == MIB_TCP_STATE_LISTEN) {
                    uint16_t local_port = ::ntohs(static_cast<uint16_t>(entry.dwLocalPort));
                    if (local_port == port1 || local_port == port2) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

enum class LatencyProfile {
    CurrentSafe,
    LiveNoVsync,
    LiveSinkAsync,
    LiveFullLowLatency,
};

static LatencyProfile GetLatencyProfile() noexcept {
    wchar_t value[64]{};
    DWORD size = ::GetEnvironmentVariableW(L"DUWN_MEDIA_LATENCY_PROFILE", value,
                                            static_cast<DWORD>(std::size(value)));
    if (size == 0 || size >= std::size(value)) return LatencyProfile::CurrentSafe;
    if (_wcsicmp(value, L"LiveNoVsync") == 0) return LatencyProfile::LiveNoVsync;
    if (_wcsicmp(value, L"LiveSinkAsync") == 0) return LatencyProfile::LiveSinkAsync;
    if (_wcsicmp(value, L"LiveFullLowLatency") == 0) return LatencyProfile::LiveFullLowLatency;
    return LatencyProfile::CurrentSafe;
}

static bool IsWiredH265Enabled() noexcept {
    wchar_t value[16]{};
    DWORD size = ::GetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", value,
                                            static_cast<DWORD>(std::size(value)));
    if (size > 0 && size < std::size(value)) {
        if (_wcsicmp(value, L"0") == 0 || _wcsicmp(value, L"false") == 0) return false;
    }
    // Production default: true for Wired
    return true;
}

static bool IsWirelessH265ProbeEnabled() noexcept {
    wchar_t value[16]{};
    DWORD size = ::GetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", value,
                                            static_cast<DWORD>(std::size(value)));
    return (size > 0 && size < std::size(value) &&
            (_wcsicmp(value, L"1") == 0 || _wcsicmp(value, L"true") == 0));
}

static const char* LatencyProfileName(LatencyProfile profile) noexcept {
    switch (profile) {
    case LatencyProfile::LiveNoVsync:       return "LiveNoVsync";
    case LatencyProfile::LiveSinkAsync:     return "LiveSinkAsync";
    case LatencyProfile::LiveFullLowLatency:return "LiveFullLowLatency";
    default:                                return "CurrentSafe";
    }
}

static DWORD GetProcessIntegrityLevel(HANDLE hProcess) noexcept {
    if (!hProcess || (hProcess != ::GetCurrentProcess() && hProcess == INVALID_HANDLE_VALUE)) return 0xFFFFFFFF;
    HANDLE token = nullptr;
    if (!::OpenProcessToken(hProcess, TOKEN_QUERY, &token)) {
        return 0xFFFFFFFF;
    }
    DWORD len = 0;
    ::SetLastError(ERROR_SUCCESS);
    ::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &len);
    if (len == 0) {
        ::CloseHandle(token);
        return 0xFFFFFFFF;
    }
    std::vector<BYTE> buf(len);
    DWORD il = 0xFFFFFFFF;
    if (::GetTokenInformation(token, TokenIntegrityLevel, buf.data(), len, &len)) {
        auto* p_til = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf.data());
        if (p_til->Label.Sid) {
            il = *::GetSidSubAuthority(p_til->Label.Sid,
                static_cast<DWORD>(*::GetSidSubAuthorityCount(p_til->Label.Sid) - 1));
        }
    }
    ::CloseHandle(token);
    return il;
}

static const char* IntegrityLevelToString(DWORD il) noexcept {
    switch (il) {
    case 0x0000:     return "Untrusted";
    case 0x1000:     return "Low";
    case 0x2000:     return "Medium";
    case 0x3000:     return "High";
    case 0x4000:     return "System";
    case 0xFFFFFFFF: return "QueryFailed";
    default:         return "Unknown";
    }
}

static std::vector<wchar_t> BuildIsolatedEnvironment(const std::wstring& sidecar_dir) noexcept {
    std::vector<std::pair<std::wstring, std::wstring>> vars;

    auto get_env = [](const wchar_t* name) -> std::wstring {
        DWORD len = ::GetEnvironmentVariableW(name, nullptr, 0);
        if (len == 0) return {};
        std::wstring val(len, L'\0');
        ::GetEnvironmentVariableW(name, val.data(), len);
        val.pop_back();
        return val;
    };

    std::wstring sys_root = get_env(L"SystemRoot");
    if (sys_root.empty()) sys_root = L"C:\\Windows";
    std::wstring sys32 = sys_root + L"\\System32";

    std::wstring local_app_data = get_env(L"LOCALAPPDATA");
    std::wstring private_registry;
    if (!local_app_data.empty()) {
        std::wstring cache_dir = local_app_data + L"\\Duwn Mirror\\cache";
        std::error_code ec;
        std::filesystem::create_directories(cache_dir, ec);
        private_registry = cache_dir + L"\\gstreamer-1.0.bin";
    }

    // Isolated PATH: sidecar, plugins, tools, libexec, and system32 only (zero MSYS2 or developer tool dependencies)
    std::wstring private_path = std::format(L"{};{};{};{};{};{}",
        sidecar_dir,
        sidecar_dir + L"\\plugins",
        sidecar_dir + L"\\tools",
        sidecar_dir + L"\\libexec",
        sys32,
        sys_root);

    vars.push_back({L"SystemRoot", sys_root});
    vars.push_back({L"PATH", private_path});
    vars.push_back({L"GST_PLUGIN_PATH", sidecar_dir + L"\\plugins"});
    vars.push_back({L"GST_PLUGIN_SYSTEM_PATH", sidecar_dir + L"\\plugins"});
    vars.push_back({L"GST_PLUGIN_SCANNER", sidecar_dir + L"\\libexec\\gst-plugin-scanner.exe"});
    if (!private_registry.empty()) {
        vars.push_back({L"GST_REGISTRY", private_registry});
    }

    for (const wchar_t* k : {L"TEMP", L"TMP", L"LOCALAPPDATA", L"USERPROFILE", L"APPDATA", L"HOMEDRIVE", L"HOMEPATH", L"ComSpec", L"PATHEXT"}) {
        auto val = get_env(k);
        if (!val.empty()) {
            vars.push_back({k, val});
        }
    }

    std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
    });

    std::vector<wchar_t> block;
    for (const auto& [k, v] : vars) {
        std::wstring line = k + L"=" + v;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool AirPlayProcess::IsPortBlockAvailable(uint16_t base) noexcept {
    if (base == 0 || base > 65533) return false;

    WSADATA wsa_data;
    int wsa_err = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_err != 0) return false;

    bool available = true;
    for (uint16_t offset = 0; offset < 3; ++offset) {
        uint16_t port = static_cast<uint16_t>(base + offset);

        // Test TCP
        SOCKET s_tcp = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s_tcp == INVALID_SOCKET) {
            available = false;
            break;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(s_tcp, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::closesocket(s_tcp);
            available = false;
            break;
        }
        ::closesocket(s_tcp);

        // Test UDP
        SOCKET s_udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s_udp == INVALID_SOCKET) {
            available = false;
            break;
        }
        if (::bind(s_udp, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::closesocket(s_udp);
            available = false;
            break;
        }
        ::closesocket(s_udp);
    }

    ::WSACleanup();
    return available;
}

uint16_t AirPlayProcess::SelectDeterministicAirPlayPort(uint16_t preferred_base) noexcept {
    const uint16_t base = preferred_base > 0 ? preferred_base : 7000;
    const uint16_t candidates[] = { base, 7100, 7200 };
    for (uint16_t cand : candidates) {
        if (IsPortBlockAvailable(cand)) {
            DUWN_LOG_INFOF("AirPlayProcess",
                "Deterministic AirPlay port block [{}-{}] verified available",
                cand, cand + 2);
            return cand;
        }
        DUWN_LOG_WARNF("AirPlayProcess",
            "Deterministic AirPlay port block [{}-{}] is busy or blocked; evaluating fallback candidate",
            cand, cand + 2);
    }
    DUWN_LOG_WARNF("AirPlayProcess",
        "All candidate AirPlay port blocks ({}, 7100, 7200) tested busy; proceeding with default {}",
        base, base);
    return base;
}

AirPlayProcess::AirPlayProcess(AirPlayProcessConfig config,
                               SessionState&        state,
                               MetadataCallback     on_metadata) noexcept
    : m_config(std::move(config))
    , m_active_port_base(m_config.airplay_port_base > 0 ? m_config.airplay_port_base : 7000)
    , m_generation(m_config.config_generation)
    , m_quality_name(m_config.receiver_quality_name)
    , m_state(state)
    , m_on_metadata(std::move(on_metadata))
    , m_control_server(std::make_unique<ControlIpcServer>(m_config.control_pipe_name)) {
    m_socket_ready_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_control_server->SetMessageCallback([this](const ControlMessage& msg) {
        HandleControlMessage(msg);
    });
}

AirPlayProcess::~AirPlayProcess() {
    Stop();
    if (m_socket_ready_event && m_socket_ready_event != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_socket_ready_event);
        m_socket_ready_event = nullptr;
    }
    if (m_job_object && m_job_object != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_job_object);
        m_job_object = nullptr;
    }
}

bool AirPlayProcess::Start() noexcept {
    m_control_server->Start();
    m_running.store(true, std::memory_order_release);
    m_supervision_thread = std::jthread([this](std::stop_token st) {
        SupervisionLoop(std::move(st));
    });
    return true;
}

void AirPlayProcess::Stop() noexcept {
    m_running.store(false, std::memory_order_release);
    m_control_server->Stop();
    m_supervision_thread.request_stop();
    m_stdout_thread.request_stop();
    m_stderr_thread.request_stop();
    // The supervision thread owns the process and pipe handles. It observes the
    // stop request within its 250 ms wait and closes them before it exits.
    if (m_supervision_thread.joinable()) m_supervision_thread.join();
    if (m_stdout_thread.joinable())      m_stdout_thread.join();
    if (m_stderr_thread.joinable())      m_stderr_thread.join();

    KillProcess("StopRequested");
}

void AirPlayProcess::HandleControlMessage(const ControlMessage& msg) noexcept {
    int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();
    m_state.RecordControlActivity(now_ns);

    switch (msg.type) {
    case ControlMessageType::SessionStart: {
        m_current_meta = StreamMetadata{};
        {
            std::lock_guard lock(m_codec_mutex);
            m_detected_video_codec = "UNKNOWN";
            m_codec_evidence.clear();
        }
        AirPlayClientInfo info;
        info.device_name          = Utf8ToWide(msg.device_name);
        info.model                = Utf8ToWide(msg.model);
        auto resolution           = AppleModelDatabase::Resolve(info.model);
        info.model_marketing_name = resolution.marketing_name;
        info.is_exact_model_match = (resolution.match_type == ModelDbMatch::Exact);
        info.os_name              = !msg.os_name.empty() ? Utf8ToWide(msg.os_name) : resolution.platform_family;
        info.os_version           = Utf8ToWide(msg.os_version);
        info.source_version       = Utf8ToWide(msg.source_version);
        info.device_id            = Utf8ToWide(msg.device_id);
        info.user_agent           = Utf8ToWide(msg.user_agent);
        info.peer_address         = Utf8ToWide(msg.client_ip);
        info.peer_port            = msg.client_port;

        m_current_client = info;
        m_state.SetClientInfo(info);
        m_state.TransitionState(AirPlaySessionState::Connected);
        DUWN_LOG_INFOF("AirPlayProcess",
            "Control IPC SESSION_START: Device='{}' Model='{}' ({}) IP='{}'",
            msg.device_name, msg.model, WideToUtf8(info.model_marketing_name), msg.client_ip);
        break;
    }

    case ControlMessageType::SessionStop:
        DUWN_LOG_INFOF("AirPlayProcess", "Control IPC SESSION_STOP: reason='{}'", msg.stop_reason);
        m_current_client.Clear();
        m_state.ClearClientInfo();
        m_current_meta = StreamMetadata{};
        {
            std::lock_guard lock(m_codec_mutex);
            m_detected_video_codec = "UNKNOWN";
            m_codec_evidence.clear();
        }
        m_state.TransitionState(AirPlaySessionState::Disconnecting);
        break;

    case ControlMessageType::StreamMetadata: {
        StreamMetadata meta = m_current_meta;
        meta.video_width  = msg.width;
        meta.video_height = msg.height;
        meta.video_fps    = msg.fps;
        meta.video_codec  = (msg.codec == "h265" || msg.codec == "H265")
                                ? VideoCodec::H265 : VideoCodec::H264;
        if (msg.audio_sample_rate > 0) meta.audio_sample_rate = msg.audio_sample_rate;
        if (msg.audio_channels > 0)    meta.audio_channels = msg.audio_channels;
        meta.audio_rtp_clock_rate = meta.audio_sample_rate;

        m_current_meta = meta;
        if (m_on_metadata) m_on_metadata(meta);
        m_state.TransitionState(AirPlaySessionState::Streaming);
        break;
    }

    case ControlMessageType::Heartbeat:
        if (msg.client_fps > 0.0) {
            m_state.RecordClientFps(now_ns);
            GlobalMetrics().client_fps.store(msg.client_fps, std::memory_order_relaxed);
        }
        if (msg.video_packets > 0) {
            m_state.RecordVideoPacket(now_ns);
        }
        if (msg.audio_packets > 0) {
            m_state.RecordAudioPacket(now_ns);
        }
        break;

    case ControlMessageType::Error:
        DUWN_LOG_ERRORF("AirPlayProcess", "Control IPC ERROR: code={} msg='{}'",
            msg.error_code, msg.error_message);
        m_state.TransitionState(AirPlaySessionState::Error);
        break;

    default:
        break;
    }
}

void AirPlayProcess::SetLastRuntimeError(std::string_view err) noexcept {
    std::lock_guard<std::mutex> lock(m_error_mutex);
    m_last_stderr_error = std::string(err);
}

std::string AirPlayProcess::GetLastRuntimeError() const noexcept {
    std::lock_guard<std::mutex> lock(m_error_mutex);
    return m_last_stderr_error;
}

bool AirPlayProcess::PreflightRuntimeValidation() noexcept {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exe_p(m_config.uxplay_exe_path);
    if (!fs::exists(exe_p, ec) || !fs::is_regular_file(exe_p, ec)) {
        std::string err = std::format("UxPlay binary not found: {}", WideToUtf8(exe_p.wstring()));
        DUWN_LOG_ERROR("AirPlayProcess", err.c_str());
        SetLastRuntimeError(err);
        m_state.Transition(SessionPhase::SidecarMissing);
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    fs::path sidecar_dir = exe_p.parent_path();
    fs::path scanner = sidecar_dir / L"libexec" / L"gst-plugin-scanner.exe";
    if (!fs::exists(scanner, ec)) {
        std::string err = std::format("GStreamer plugin scanner missing: {}", WideToUtf8(scanner.wstring()));
        DUWN_LOG_ERROR("AirPlayProcess", err.c_str());
        SetLastRuntimeError(err);
        m_state.Transition(SessionPhase::SidecarMissing);
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    fs::path plugins_dir = sidecar_dir / L"plugins";
    if (!fs::exists(plugins_dir, ec) || !fs::is_directory(plugins_dir, ec)) {
        std::string err = std::format("GStreamer plugins directory missing: {}", WideToUtf8(plugins_dir.wstring()));
        DUWN_LOG_ERROR("AirPlayProcess", err.c_str());
        SetLastRuntimeError(err);
        m_state.Transition(SessionPhase::SidecarMissing);
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    // Verify key plugin DLLs
    for (const wchar_t* p : {L"libgstapp.dll", L"libgstlibav.dll", L"libgstplayback.dll",
                             L"libgstautodetect.dll", L"libgstvideoparsersbad.dll",
                             L"libgstrtp.dll", L"libgstudp.dll"}) {
        fs::path ppath = plugins_dir / p;
        if (!fs::exists(ppath, ec)) {
            std::string err = std::format("Required GStreamer plugin '{}' not found in {}",
                                          WideToUtf8(p), WideToUtf8(plugins_dir.wstring()));
            DUWN_LOG_ERROR("AirPlayProcess", err.c_str());
            SetLastRuntimeError(err);
            m_state.Transition(SessionPhase::AdvertisingFailed);
            m_state.TransitionState(AirPlaySessionState::Error);
            return false;
        }
    }

    DUWN_LOG_INFOF("AirPlayProcess",
        "Runtime Preflight Verified:\n  sidecar_exe: {}\n  sidecar_dir: {}\n  gst_plugin_path: {}\n  gst_plugin_system_path: {}\n  gst_plugin_scanner: {}",
        WideToUtf8(exe_p.wstring()),
        WideToUtf8(sidecar_dir.wstring()),
        WideToUtf8(plugins_dir.wstring()),
        WideToUtf8(plugins_dir.wstring()),
        WideToUtf8(scanner.wstring()));

    return true;
}

bool AirPlayProcess::VerifySidecar() noexcept {
    if (!PreflightRuntimeValidation()) {
        return false;
    }

    namespace fs = std::filesystem;
    fs::path p(m_config.uxplay_exe_path);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
        DUWN_LOG_ERRORF("AirPlayProcess",
            "UxPlay sidecar not found: {}",
            WideToUtf8(m_config.uxplay_exe_path));
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    auto start_t = std::chrono::steady_clock::now();
    const bool require_bind_flags = !m_config.bind_ipv4.empty();
    SidecarCachedVerification cached;
    if (SidecarVerificationCache::CheckCache(m_config.uxplay_exe_path, require_bind_flags, cached)) {
        auto end_t = std::chrono::steady_clock::now();
        double verification_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();
        DUWN_LOG_INFOF("SidecarVerification",
            "[SidecarVerification] sha256={} cache_hit=true cached_version={}.{} process_probe_required=false verification_ms={:.2f}",
            cached.sha256, cached.major, cached.minor, verification_ms);
        DUWN_LOG_INFOF("AirPlayProcess",
            "UxPlay version {}.{} verified from cache (>= 1.73)", cached.major, cached.minor);
        return true;
    }

    std::string sha256_hash = SidecarVerificationCache::ComputeFileSha256(m_config.uxplay_exe_path);

    // Run uxplay.exe -h to check version and -vrtp/-artp support
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE h_read = INVALID_HANDLE_VALUE, h_write = INVALID_HANDLE_VALUE;
    if (!::CreatePipe(&h_read, &h_write, &sa, 0)) {
        DUWN_LOG_ERROR("AirPlayProcess", "CreatePipe for version check failed");
        return true; // allow attempt if pipe creation fails
    }
    ::SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = h_write;
    si.hStdError  = h_write;
    si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring check_cmd = std::format(L"\"{}\" -h", m_config.uxplay_exe_path);

    fs::path exe_p(m_config.uxplay_exe_path);
    std::wstring sidecar_dir = exe_p.parent_path().wstring();
    auto env_block = BuildIsolatedEnvironment(sidecar_dir);

    BOOL ok = ::CreateProcessW(
        nullptr, check_cmd.data(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env_block.data(),
        sidecar_dir.c_str(),
        &si, &pi
    );
    ::CloseHandle(h_write);

    if (!ok) {
        DWORD err = ::GetLastError();
        DUWN_LOG_ERRORF("AirPlayProcess",
            "Failed to execute UxPlay for version check, error={}", err);
        ::CloseHandle(h_read);
        SidecarVerificationCache::Invalidate();
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    std::string output;
    char buf[512];
    DWORD read_bytes = 0;
    while (::ReadFile(h_read, buf, sizeof(buf) - 1, &read_bytes, nullptr) && read_bytes > 0) {
        buf[read_bytes] = '\0';
        output.append(buf, read_bytes);
    }
    ::CloseHandle(h_read);

    ::WaitForSingleObject(pi.hProcess, 3000);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    // Look for UxPlay version: e.g. "UxPlay 1.74" or "UxPlay version 1.74"
    int major = 0, minor = 0;
    auto pos = output.find("UxPlay");
    bool version_parsed = false;
    if (pos != std::string::npos) {
        const char* s = output.c_str() + pos;
        if (::sscanf_s(s, "UxPlay %d.%d", &major, &minor) == 2 ||
            ::sscanf_s(s, "UxPlay version %d.%d", &major, &minor) == 2) {
            version_parsed = true;
        }
    }

    bool has_vrtp = (output.find("-vrtp") != std::string::npos);
    bool has_bind_ip = (output.find("-bind-ip") != std::string::npos);
    bool has_bind_prefix = (output.find("-bind-prefix") != std::string::npos);

    auto end_t = std::chrono::steady_clock::now();
    double verification_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();
    DUWN_LOG_INFOF("SidecarVerification",
        "[SidecarVerification] sha256={} cache_hit=false cached_version={}.{} process_probe_required=true verification_ms={:.2f}",
        sha256_hash, major, minor, verification_ms);

    if (!m_config.bind_ipv4.empty() && (!has_bind_ip || !has_bind_prefix)) {
        DUWN_LOG_ERROR("AirPlayProcess", "UxPlay sidecar lacks strict USB binding support");
        SidecarVerificationCache::Invalidate();
        m_state.TransitionState(AirPlaySessionState::Error);
        return false;
    }

    if (version_parsed) {
        if (major > 1 || (major == 1 && minor >= 73)) {
            DUWN_LOG_INFOF("AirPlayProcess",
                "UxPlay version {}.{} verified (>= 1.73)", major, minor);
            SidecarCachedVerification record;
            record.sha256 = sha256_hash;
            record.major = major;
            record.minor = minor;
            record.has_vrtp = has_vrtp;
            record.has_bind_ip = has_bind_ip;
            record.has_bind_prefix = has_bind_prefix;
            record.min_required_major = SidecarVerificationCache::kMinRequiredMajor;
            record.min_required_minor = SidecarVerificationCache::kMinRequiredMinor;
            SidecarVerificationCache::SaveCache(record);
            return true;
        } else {
            DUWN_LOG_ERRORF("AirPlayProcess",
                "UxPlay version {}.{} is unsupported; requires >= 1.73 for -vrtp/-artp",
                major, minor);
            SidecarVerificationCache::Invalidate();
            m_state.TransitionState(AirPlaySessionState::Error);
            return false;
        }
    }

    if (has_vrtp) {
        DUWN_LOG_INFO("AirPlayProcess", "UxPlay -vrtp/-artp support confirmed from help text");
        SidecarCachedVerification record;
        record.sha256 = sha256_hash;
        record.major = 1;
        record.minor = 73;
        record.has_vrtp = true;
        record.has_bind_ip = has_bind_ip;
        record.has_bind_prefix = has_bind_prefix;
        record.min_required_major = SidecarVerificationCache::kMinRequiredMajor;
        record.min_required_minor = SidecarVerificationCache::kMinRequiredMinor;
        SidecarVerificationCache::SaveCache(record);
        return true;
    }

    DUWN_LOG_ERROR("AirPlayProcess",
        "Could not verify UxPlay version >= 1.73 (-vrtp option not found in help text)");
    SidecarVerificationCache::Invalidate();
    m_state.TransitionState(AirPlaySessionState::Error);
    return false;
}

AirPlayEnvelope ComputeSquareAirPlayEnvelope(
    uint32_t width,
    uint32_t height,
    uint32_t fps) noexcept
{
    uint32_t edge = std::max(width, height);
    if (edge == 0) {
        edge = 1920;
    } else if (edge < 720) {
        edge = 1280;
    }
    uint32_t effective_fps = fps > 0 ? fps : 60;
    return {edge, edge, effective_fps};
}

std::wstring AirPlayProcess::BuildCommandLine() const noexcept {
    std::wstring name_utf16 = m_config.receiver_name.empty()
                              ? L"DuwnMirror"
                              : m_config.receiver_name;

    // UxPlay flags:
    //   -n <name>      : AirPlay receiver name
    //   -nh            : Do not add "@hostname" at the end of AirPlay server name
    //   -s WxH@fps -fps <fps> : Exact display resolution and framerate ceiling (orientation-neutral square envelope)
    //   -FPSdata       : Show video streaming performance reports sent by client
    //   -vrtp <pipe>   : GStreamer video RTP pipeline to forward decrypted H.264
    //   -artp <pipe>   : GStreamer audio RTP pipeline to forward decoded L16 audio
    //   -nc            : maintain connection / do not close receiver on client quit
    AirPlayEnvelope env = ComputeSquareAirPlayEnvelope(
        m_config.receiver_width,
        m_config.receiver_height,
        m_config.max_fps
    );
    const LatencyProfile profile = GetLatencyProfile();
    const bool no_vsync = profile == LatencyProfile::LiveNoVsync ||
                          profile == LatencyProfile::LiveFullLowLatency;
    const bool sink_sync_off = profile == LatencyProfile::LiveSinkAsync ||
                               profile == LatencyProfile::LiveFullLowLatency;
    const wchar_t* sink_options = sink_sync_off ? L" sync=false" : L"";
    std::wstring cmd = std::format(
        L"\"{}\" -p {} -n \"{}\" -nh -s {}x{}@{} -fps {}{}{} -vrtp \"config-interval=1 ! udpsink host=127.0.0.1 port={}{}\" -artp \"pt=96 ! udpsink host=127.0.0.1 port={}{}\" -nc",
        m_config.uxplay_exe_path,
        m_active_port_base,
        name_utf16,
        env.width,
        env.height,
        env.fps,
        env.fps,
        m_config.enable_fps_data ? L" -FPSdata" : L"",
        no_vsync ? L" -vsync no" : L"",
        m_config.video_rtp_port,
        sink_options,
        m_config.audio_rtp_port,
        sink_options
    );

    if (m_config.debug_log) cmd += L" -d";
    if (!m_config.bind_ipv4.empty()) {
        cmd += std::format(L" -bind-ip {} -bind-prefix {}", m_config.bind_ipv4, m_config.bind_prefix);
        if (IsWiredH265Enabled()) {
            cmd += L" -h265";
        }
    } else {
        if (IsWirelessH265ProbeEnabled()) {
            cmd += L" -h265";
        }
    }

    return cmd;
}

bool AirPlayProcess::SpawnProcess() noexcept {
    // Create pipes for UxPlay stdout and stderr
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdout_write = INVALID_HANDLE_VALUE;
    if (!::CreatePipe(&m_stdout_read, &stdout_write, &sa, 0)) {
        DUWN_LOG_ERROR("AirPlayProcess", "CreatePipe for stdout failed");
        return false;
    }
    ::SetHandleInformation(m_stdout_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE stderr_write = INVALID_HANDLE_VALUE;
    if (!::CreatePipe(&m_stderr_read, &stderr_write, &sa, 0)) {
        DUWN_LOG_ERROR("AirPlayProcess", "CreatePipe for stderr failed");
        ::CloseHandle(m_stdout_read);
        ::CloseHandle(stdout_write);
        m_stdout_read = INVALID_HANDLE_VALUE;
        return false;
    }
    ::SetHandleInformation(m_stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write;
    si.hStdError  = stderr_write;
    si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

    uint32_t req_fps = m_config.max_fps > 0 ? m_config.max_fps : 60;
    DUWN_LOG_INFOF("AirPlayConfig",
        "[ReceiverPreset]\nname={}\ncli=-s {}x{}@{} -fps {}",
        m_quality_name,
        m_config.receiver_width,
        m_config.receiver_height,
        req_fps,
        req_fps);

    m_active_port_base = SelectDeterministicAirPlayPort(m_config.airplay_port_base > 0 ? m_config.airplay_port_base : 7000);
    std::wstring cmd = BuildCommandLine();
    const LatencyProfile latency_profile = GetLatencyProfile();
    const bool sink_sync_off = latency_profile == LatencyProfile::LiveSinkAsync ||
                               latency_profile == LatencyProfile::LiveFullLowLatency;
    DUWN_LOG_INFOF("AirPlayProcess", "[LatencyProfile] {} (development environment only)",
                   LatencyProfileName(latency_profile));
    DUWN_LOG_INFOF("AirPlayProcess", "Spawning: {}", WideToUtf8(cmd));
    DUWN_LOG_INFOF("UxPlayPipeline",
        "Video=appsrc name=video_source ! queue ! h264parse ! rtph264pay config-interval=1 ! udpsink host=127.0.0.1 port={} (sink sync={})",
        m_config.video_rtp_port, sink_sync_off ? "false" : "default(true)");
    m_active_audio_pipeline_logged = false;

    namespace fs = std::filesystem;
    fs::path exe_p(m_config.uxplay_exe_path);
    std::wstring sidecar_dir = exe_p.parent_path().wstring();
    auto env_block = BuildIsolatedEnvironment(sidecar_dir);

    // Initialize Job Object with KILL_ON_JOB_CLOSE limit if not already created
    if (!m_job_object) {
        m_job_object = ::CreateJobObjectW(nullptr, nullptr);
        if (m_job_object) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!::SetInformationJobObject(m_job_object, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
                DWORD err = ::GetLastError();
                DUWN_LOG_ERRORF("AirPlayProcess", "SetInformationJobObject failed, error={}", err);
            } else {
                DUWN_LOG_INFO("AirPlayProcess", "Job Object initialized with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE");
            }
        } else {
            DWORD err = ::GetLastError();
            DUWN_LOG_ERRORF("AirPlayProcess", "CreateJobObjectW failed, error={}", err);
        }
    }

    BOOL ok = ::CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr, nullptr,
        TRUE,                                                           // inherit handles (stdout and stderr pipes)
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, // suspended so job assignment is atomic
        env_block.data(),                                               // isolated environment block
        sidecar_dir.c_str(),                                            // working directory
        &si,
        &m_proc_info
    );

    ::CloseHandle(stdout_write);
    ::CloseHandle(stderr_write);

    if (!ok) {
        DWORD err = ::GetLastError();
        DUWN_LOG_ERRORF("AirPlayProcess", "CreateProcessW failed, error={}", err);
        ::CloseHandle(m_stdout_read);
        ::CloseHandle(m_stderr_read);
        m_stdout_read = INVALID_HANDLE_VALUE;
        m_stderr_read = INVALID_HANDLE_VALUE;
        return false;
    }

    // Assign to Job Object before resuming
    bool job_assigned = false;
    if (m_job_object && m_proc_info.hProcess) {
        if (::AssignProcessToJobObject(m_job_object, m_proc_info.hProcess)) {
            job_assigned = true;
        } else {
            DWORD err = ::GetLastError();
            DUWN_LOG_ERRORF("AirPlayProcess",
                "AssignProcessToJobObject failed for PID={}: error={}; terminating child to prevent orphan",
                m_proc_info.dwProcessId, err);
            ::TerminateProcess(m_proc_info.hProcess, 1);
            ::CloseHandle(m_proc_info.hProcess);
            ::CloseHandle(m_proc_info.hThread);
            m_proc_info = {};
            ::CloseHandle(m_stdout_read);
            ::CloseHandle(m_stderr_read);
            m_stdout_read = INVALID_HANDLE_VALUE;
            m_stderr_read = INVALID_HANDLE_VALUE;
            return false;
        }
    }

    // Resume suspended process
    m_sockets_ready.store(false, std::memory_order_release);
    if (m_socket_ready_event && m_socket_ready_event != INVALID_HANDLE_VALUE) {
        ::ResetEvent(m_socket_ready_event);
    }
    ::ResumeThread(m_proc_info.hThread);

    DWORD parent_pid = ::GetCurrentProcessId();
    DWORD child_pid = m_proc_info.dwProcessId;
    DWORD parent_il = GetProcessIntegrityLevel(::GetCurrentProcess());
    DWORD child_il = GetProcessIntegrityLevel(m_proc_info.hProcess);

    DUWN_LOG_INFOF("AirPlayProcess",
        "[ProcessDiagnostics] ParentPID={} (IL={:#x} {}) ChildPID={} (IL={:#x} {}) HandleValid={} JobAssigned={}",
        parent_pid, parent_il, IntegrityLevelToString(parent_il),
        child_pid, child_il, IntegrityLevelToString(child_il),
        (m_proc_info.hProcess != nullptr && m_proc_info.hProcess != INVALID_HANDLE_VALUE),
        job_assigned);

    DWORD old_pid = m_last_pid;
    m_last_pid = m_proc_info.dwProcessId;
    DUWN_LOG_INFOF("AirPlayProcess",
        "[SidecarLifecycle] old_pid={} new_pid={} config_generation={} preset={}",
        old_pid, m_last_pid, m_generation, m_quality_name);
    ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::SidecarStarted, WideToUtf8(cmd));
    duwn::telemetry::ConnectionTimeline::Get().Record(
        duwn::telemetry::ConnectionMilestone::C3_UxPlaySpawned,
        std::format("PID={}", m_proc_info.dwProcessId));

    // Start stdout and stderr reader threads
    m_stdout_thread = std::jthread([this](std::stop_token st) {
        ReadStdout(std::move(st));
    });
    m_stderr_thread = std::jthread([this](std::stop_token st) {
        ReadStderr(std::move(st));
    });

    return true;
}

void AirPlayProcess::KillProcess(std::string_view reason) noexcept {
    if (m_proc_info.hProcess && m_proc_info.hProcess != INVALID_HANDLE_VALUE) {
        DWORD pid = m_proc_info.dwProcessId;
        DWORD parent_pid = ::GetCurrentProcessId();
        DWORD parent_il = GetProcessIntegrityLevel(::GetCurrentProcess());
        DWORD child_il = GetProcessIntegrityLevel(m_proc_info.hProcess);

        DUWN_LOG_INFOF("AirPlayProcess",
            "[ShutdownDiagnostics] Initiating shutdown of UxPlay PID={} (ParentPID={} ParentIL={:#x} ChildIL={:#x} Reason='{}')",
            pid, parent_pid, parent_il, child_il, reason);

        // 1. Graceful shutdown attempt via control IPC if connected
        bool graceful_exit = false;
        if (m_control_server && m_control_server->IsClientConnected()) {
            ControlMessage stop_msg{};
            stop_msg.type = ControlMessageType::SessionStop;
            stop_msg.stop_reason = std::string(reason);
            m_control_server->SendMessage(stop_msg);

            DWORD wait_graceful = ::WaitForSingleObject(m_proc_info.hProcess, 500);
            if (wait_graceful == WAIT_OBJECT_0) {
                graceful_exit = true;
                DUWN_LOG_INFOF("AirPlayProcess", "UxPlay PID={} exited gracefully via IPC", pid);
            }
        }

        // 2. Bounded termination fallback if still alive
        if (!graceful_exit) {
            DWORD still_active_code = 0;
            if (::GetExitCodeProcess(m_proc_info.hProcess, &still_active_code) && still_active_code == STILL_ACTIVE) {
                ::TerminateProcess(m_proc_info.hProcess, 0);
                DWORD wait_res = ::WaitForSingleObject(m_proc_info.hProcess, 1500);
                if (wait_res == WAIT_OBJECT_0) {
                    DUWN_LOG_INFOF("AirPlayProcess", "UxPlay PID={} terminated cleanly", pid);
                } else {
                    DUWN_LOG_WARNF("AirPlayProcess", "UxPlay PID={} wait returned {}", pid, wait_res);
                }
            }
        }

        DWORD exit_code = 0;
        ::GetExitCodeProcess(m_proc_info.hProcess, &exit_code);
        DUWN_LOG_INFOF("AirPlayProcess",
            "[ShutdownDiagnostics] UxPlay PID={} terminated with ExitCode={} (Reason='{}')",
            pid, exit_code, reason);

        ::CloseHandle(m_proc_info.hProcess);
        ::CloseHandle(m_proc_info.hThread);
        m_proc_info = {};
    }
    if (m_stdout_read != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_stdout_read);
        m_stdout_read = INVALID_HANDLE_VALUE;
    }
    if (m_stderr_read != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_stderr_read);
        m_stderr_read = INVALID_HANDLE_VALUE;
    }
}

void AirPlayProcess::TripCircuitBreaker() noexcept {
    m_circuit_breaker_tripped.store(true, std::memory_order_release);
    std::string err_msg = GetLastRuntimeError();
    if (err_msg.empty()) {
        err_msg = "AirPlay sidecar exited repeatedly during startup validation";
    }
    DUWN_LOG_ERRORF("AirPlayProcess",
        "*** CRASH CIRCUIT BREAKER TRIPPED ***: 3 consecutive startup failures. Auto-restart stopped. Error: {}",
        err_msg);

    m_state.Transition(SessionPhase::AdvertisingFailed);
    m_state.TransitionState(AirPlaySessionState::Error);
}

void AirPlayProcess::ManualRetry() noexcept {
    DUWN_LOG_INFO("AirPlayProcess", "Manual retry requested: clearing circuit breaker and private cache");

    auto get_env = [](const wchar_t* name) -> std::wstring {
        DWORD sz = ::GetEnvironmentVariableW(name, nullptr, 0);
        if (sz == 0) return L"";
        std::wstring buf(sz, L'\0');
        ::GetEnvironmentVariableW(name, buf.data(), sz);
        while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
        return buf;
    };
    std::wstring local_app_data = get_env(L"LOCALAPPDATA");
    if (!local_app_data.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path cache_file = fs::path(local_app_data) / L"Duwn Mirror" / L"cache" / L"gstreamer-1.0.bin";
        fs::remove(cache_file, ec);
    }

    {
        std::lock_guard<std::mutex> lock(m_error_mutex);
        m_last_stderr_error.clear();
    }
    m_circuit_breaker_tripped.store(false, std::memory_order_release);
}

void AirPlayProcess::SupervisionLoop(std::stop_token stop) noexcept {
    using namespace std::chrono_literals;

    if (!VerifySidecar()) {
        return; // Sidecar missing, unsupported, or missing plugins
    }

    int consecutive_failures = 0;
    while (!stop.stop_requested() && m_running.load(std::memory_order_acquire)) {
        if (m_circuit_breaker_tripped.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(250ms);
            continue;
        }

        auto start_time = std::chrono::steady_clock::now();

        // During startup: show "Starting AirPlay…" (Connecting state)
        m_state.Transition(SessionPhase::Connecting);
        m_state.TransitionState(AirPlaySessionState::Connecting);

        if (!SpawnProcess()) {
            consecutive_failures++;
            DUWN_LOG_WARNF("AirPlayProcess", "SpawnProcess failed (attempt {}/3)", consecutive_failures);
            if (consecutive_failures >= 3) {
                TripCircuitBreaker();
                continue;
            }
            int backoff_sec = (1 << consecutive_failures); // 2s, 4s, 8s
            for (int s = 0; s < backoff_sec * 10 && !stop.stop_requested(); ++s) {
                std::this_thread::sleep_for(100ms);
            }
            continue;
        }

        start_time = std::chrono::steady_clock::now();
        bool initialized_advertised = false;
        while (!stop.stop_requested()) {
            if (!initialized_advertised) {
                HANDLE wait_handles[2] = { m_proc_info.hProcess, m_socket_ready_event };
                DWORD wait = ::WaitForMultipleObjects(2, wait_handles, FALSE, 25);
                if (wait == WAIT_OBJECT_0) {
                    // Process exited prematurely
                    break;
                }

                bool sockets_ready = m_sockets_ready.load(std::memory_order_acquire);
                if (!sockets_ready && IsProcessListeningOnPort(m_proc_info.dwProcessId, m_active_port_base, m_active_port_base + 1)) {
                    if (!m_sockets_ready.exchange(true, std::memory_order_acq_rel)) {
                        duwn::telemetry::ConnectionTimeline::Get().Record(
                            duwn::telemetry::ConnectionMilestone::C4_UxPlaySocketsInitialized,
                            std::format("TCP port {} listening", m_active_port_base + 1));
                        duwn::telemetry::ConnectionTimeline::Get().Record(
                            duwn::telemetry::ConnectionMilestone::C4A_MdnsPublicationInitiated,
                            "mDNS publication active (UDP 5353)");
                        duwn::telemetry::ConnectionTimeline::Get().Record(
                            duwn::telemetry::ConnectionMilestone::C4B_AdvertisementActiveInternal,
                            "Internal sockets and advertisement ready");
                        if (m_socket_ready_event && m_socket_ready_event != INVALID_HANDLE_VALUE) {
                            ::SetEvent(m_socket_ready_event);
                        }
                    }
                    sockets_ready = true;
                }

                auto elapsed = std::chrono::steady_clock::now() - start_time;
                bool fallback_timeout = (elapsed >= std::chrono::milliseconds(1500));

                if (sockets_ready || fallback_timeout) {
                    DWORD code = 0;
                    if (::GetExitCodeProcess(m_proc_info.hProcess, &code) && code == STILL_ACTIVE) {
                        initialized_advertised = true;
                        // Sidecar validated: sockets bound and process alive without early crash!
                        // Transition to Advertising / Ready to connect
                        m_state.Transition(SessionPhase::Advertising);
                        m_state.TransitionState(AirPlaySessionState::Idle);
                        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::MdnsAdvertised, "AirPlay mDNS service active");
                        duwn::telemetry::ConnectionTimeline::Get().Record(
                            duwn::telemetry::ConnectionMilestone::C5_MediaSessionReady, "Application ready for client media session");
                        DUWN_LOG_INFOF("AirPlayProcess",
                            "UxPlay startup validation passed (reason={}, elapsed={:.2f}ms); AirPlay service active",
                            sockets_ready ? "event_driven_sockets_ready" : "fallback_timeout",
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count());
                    }
                }
            } else {
                DWORD wait = ::WaitForSingleObject(m_proc_info.hProcess, 250);
                if (wait == WAIT_OBJECT_0) break; // process exited

                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= std::chrono::seconds(10)) {
                    consecutive_failures = 0; // Running stably, reset failure counter
                }
            }
        }

        if (stop.stop_requested()) {
            KillProcess("SupervisionStopRequested");
            if (m_stdout_thread.joinable()) m_stdout_thread.join();
            if (m_stderr_thread.joinable()) m_stderr_thread.join();
            break;
        }

        DWORD exit_code = 0;
        ::GetExitCodeProcess(m_proc_info.hProcess, &exit_code);
        auto duration = std::chrono::steady_clock::now() - start_time;

        KillProcess("SupervisionProcessExited");
        if (m_stdout_thread.joinable()) m_stdout_thread.join();
        if (m_stderr_thread.joinable()) m_stderr_thread.join();

        m_state.TransitionState(AirPlaySessionState::Disconnecting);
        m_current_client.Clear();
        m_state.ClearClientInfo();
        duwn::telemetry::ConnectionTimeline::Get().ResetSession();

        if (duration < std::chrono::seconds(5)) {
            consecutive_failures++;
            DUWN_LOG_WARNF("AirPlayProcess",
                "UxPlay fast startup crash with code {} after {}ms (failure {}/3)",
                exit_code,
                std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(),
                consecutive_failures);

            if (consecutive_failures >= 3) {
                TripCircuitBreaker();
                continue;
            }
            int backoff_sec = (1 << consecutive_failures); // 2s, 4s, 8s
            DUWN_LOG_WARNF("AirPlayProcess", "Backing off for {}s before restart", backoff_sec);
            for (int s = 0; s < backoff_sec * 10 && !stop.stop_requested(); ++s) {
                std::this_thread::sleep_for(100ms);
            }
        } else {
            consecutive_failures = 0;
            DUWN_LOG_INFOF("AirPlayProcess", "UxPlay session ended normally after {}s; restarting in 2s",
                std::chrono::duration_cast<std::chrono::seconds>(duration).count());
            for (int s = 0; s < 20 && !stop.stop_requested(); ++s) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }

    m_state.TransitionState(AirPlaySessionState::Idle);
}

void AirPlayProcess::ReadStdout(std::stop_token /*stop*/) noexcept {
    std::string line_buf;
    char chunk[256];
    DWORD bytes_read = 0;

    while (m_stdout_read != INVALID_HANDLE_VALUE &&
           ::ReadFile(m_stdout_read, chunk, sizeof(chunk) - 1, &bytes_read, nullptr) &&
           bytes_read > 0) {
        chunk[bytes_read] = '\0';
        line_buf += chunk;

        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            line_buf.erase(0, pos + 1);
            DUWN_LOG_INFOF("AirPlayProcess", "[UxPlay stdout] {}", line);
            ParseLine(line);
        }
    }
}

void AirPlayProcess::ReadStderr(std::stop_token /*stop*/) noexcept {
    std::string line_buf;
    char chunk[256];
    DWORD bytes_read = 0;

    while (m_stderr_read != INVALID_HANDLE_VALUE &&
           ::ReadFile(m_stderr_read, chunk, sizeof(chunk) - 1, &bytes_read, nullptr) &&
           bytes_read > 0) {
        chunk[bytes_read] = '\0';
        line_buf += chunk;

        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            line_buf.erase(0, pos + 1);
            DUWN_LOG_WARNF("AirPlayProcess", "[UxPlay stderr] {}", line);
            ParseLine(line);

            if (line.find("not found") != std::string::npos ||
                line.find("ERROR") != std::string::npos ||
                line.find("failed") != std::string::npos ||
                line.find("cannot") != std::string::npos) {
                SetLastRuntimeError(line);
            }
        }
    }
}

void AirPlayProcess::ApplyFpsKeyValue(std::string_view key, std::string_view val_str) noexcept {
    double val = 0.0;
    try {
        val = std::stod(std::string(val_str));
    } catch (...) {
        return;
    }

    int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();
    m_state.RecordControlActivity(now_ns);

    if (key == "encoderCurrentFPS" || key == "fps" || key == "currentFPS") {
        m_state.RecordClientFps(now_ns);
        GlobalMetrics().client_fps.store(val, std::memory_order_relaxed);
        DUWN_LOG_INFOF("UxPlay", "[UxPlay] config_gen={} encoderCurrentFPS={:.2f}", m_generation, val);
    } else if (key == "droppedFrames") {
        GlobalMetrics().client_dropped_frames.store(static_cast<uint64_t>(val), std::memory_order_relaxed);
    } else if (key == "totalFrames") {
        GlobalMetrics().client_total_frames.store(static_cast<uint64_t>(val), std::memory_order_relaxed);
    } else if (key == "bitrate" || key == "networkBitrate") {
        GlobalMetrics().client_bitrate_kbps.store(val, std::memory_order_relaxed);
    }
}

void AirPlayProcess::ParseLine(std::string_view line) noexcept {
    DUWN_LOG_DEBUG("UxPlay", std::string(line));

    if (line.find("Initialized server socket") != std::string_view::npos ||
        line.find("server socket") != std::string_view::npos ||
        line.find("Listening") != std::string_view::npos ||
        line.find("listening") != std::string_view::npos ||
        line.find("advertised AirPlay service") != std::string_view::npos ||
        line.find("register_dnssd") != std::string_view::npos) {
        if (!m_sockets_ready.exchange(true, std::memory_order_acq_rel)) {
            duwn::telemetry::ConnectionTimeline::Get().Record(
                duwn::telemetry::ConnectionMilestone::C4_UxPlaySocketsInitialized, line);
            duwn::telemetry::ConnectionTimeline::Get().Record(
                duwn::telemetry::ConnectionMilestone::C4A_MdnsPublicationInitiated, "mDNS publication initiated");
            duwn::telemetry::ConnectionTimeline::Get().Record(
                duwn::telemetry::ConnectionMilestone::C4B_AdvertisementActiveInternal, "Internal sockets and advertisement ready");
            if (m_socket_ready_event && m_socket_ready_event != INVALID_HANDLE_VALUE) {
                ::SetEvent(m_socket_ready_event);
            }
        }
    }

    if (line.find("mDNS") != std::string_view::npos ||
        line.find("DNS-SD") != std::string_view::npos ||
        line.find("Bonjour") != std::string_view::npos) {
        duwn::telemetry::ConnectionTimeline::Get().Record(
            duwn::telemetry::ConnectionMilestone::C4A_MdnsPublicationInitiated, line);
        duwn::telemetry::ConnectionTimeline::Get().Record(
            duwn::telemetry::ConnectionMilestone::C4B_AdvertisementActiveInternal, line);
    }

    int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();

    // UxPlay constructs one pipeline for each supported audio content type.
    // Log the selected chain only after the sender announces its active type.
    const size_t ct_pos = line.find("ct=");
    if (!m_active_audio_pipeline_logged && ct_pos != std::string_view::npos) {
        unsigned ct = 0;
        const std::string ct_text(line.substr(ct_pos));
        if (::sscanf_s(ct_text.c_str(), "ct=%u", &ct) == 1) {
            const char* decoder = nullptr;
            const char* format = nullptr;
            switch (ct) {
            case 8: decoder = "avdec_aac ! ";  format = "AAC-ELD"; break;
            case 2: decoder = "avdec_alac ! "; format = "ALAC";    break;
            case 4: decoder = "avdec_aac ! ";  format = "AAC-LC";  break;
            case 1: decoder = "";               format = "PCM";     break;
            default: break;
            }
            if (decoder) {
                const LatencyProfile profile = GetLatencyProfile();
                const bool sink_sync_off = profile == LatencyProfile::LiveSinkAsync ||
                                           profile == LatencyProfile::LiveFullLowLatency;
                DUWN_LOG_INFOF("UxPlayPipeline",
                    "Audio(active={},ct={})=appsrc name=audio_source ! queue ! {}audioconvert ! audioresample quality=10 ! volume name=volume ! audioconvert ! audio/x-raw,format=S16BE,rate=44100,channels=2 ! rtpL16pay pt=96 ! udpsink host=127.0.0.1 port={} (sink sync={})",
                    format, ct, decoder, m_config.audio_rtp_port,
                    sink_sync_off ? "false" : "default(true)");
                m_active_audio_pipeline_logged = true;
            }
        }
    }

    // Video pipeline and codec detection (Hierarchy A: UxPlayPipeline, Hierarchy B: GstCaps)
    if (line.find("rtph265pay") != std::string_view::npos) {
        std::lock_guard lock(m_codec_mutex);
        m_detected_video_codec = "H265";
        m_codec_evidence = "UxPlayPipeline (rtph265pay)";
        DUWN_LOG_INFOF("WiredCodecProbe", "Detected video codec via pipeline: H265 (evidence={})", m_codec_evidence);
    } else if (line.find("rtph264pay") != std::string_view::npos) {
        std::lock_guard lock(m_codec_mutex);
        m_detected_video_codec = "H264";
        m_codec_evidence = "UxPlayPipeline (rtph264pay)";
        DUWN_LOG_INFOF("WiredCodecProbe", "Detected video codec via pipeline: H264 (evidence={})", m_codec_evidence);
    }

    if (line.find("encoding-name=(string)H265") != std::string_view::npos ||
        line.find("encoding-name=H265") != std::string_view::npos) {
        std::lock_guard lock(m_codec_mutex);
        if (m_detected_video_codec == "UNKNOWN") {
            m_detected_video_codec = "H265";
            m_codec_evidence = "GstCaps (encoding-name=H265)";
            DUWN_LOG_INFOF("WiredCodecProbe", "Detected video codec via caps: H265 (evidence={})", m_codec_evidence);
        }
    } else if (line.find("encoding-name=(string)H264") != std::string_view::npos ||
               line.find("encoding-name=H264") != std::string_view::npos) {
        std::lock_guard lock(m_codec_mutex);
        if (m_detected_video_codec == "UNKNOWN") {
            m_detected_video_codec = "H264";
            m_codec_evidence = "GstCaps (encoding-name=H264)";
            DUWN_LOG_INFOF("WiredCodecProbe", "Detected video codec via caps: H264 (evidence={})", m_codec_evidence);
        }
    }

    // 1. Client connection request with Device Name and Model
    // Format: "connection request from <name> (<model>) with deviceID = <deviceid>"
    if (line.find("connection request from ") != std::string::npos) {
        m_state.RecordControlActivity(now_ns);
        m_current_meta = StreamMetadata{};
        {
            std::lock_guard lock(m_codec_mutex);
            m_detected_video_codec = "UNKNOWN";
            m_codec_evidence.clear();
        }
        size_t name_start = line.find("connection request from ") + 24;
        size_t model_start = line.find(" (", name_start);
        if (model_start != std::string_view::npos) {
            std::string name_str(line.substr(name_start, model_start - name_start));
            size_t model_end = line.find(") with deviceID = ", model_start + 2);
            if (model_end != std::string_view::npos) {
                std::string model_str(line.substr(model_start + 2, model_end - (model_start + 2)));
                std::string devid_str(line.substr(model_end + 18));
                // trim any trailing whitespace or newline
                while (!devid_str.empty() && (devid_str.back() == '\r' || devid_str.back() == ' ')) {
                    devid_str.pop_back();
                }

                m_current_client.device_name          = Utf8ToWide(name_str);
                m_current_client.model                = Utf8ToWide(model_str);
                auto resolution                       = AppleModelDatabase::Resolve(m_current_client.model);
                m_current_client.model_marketing_name = resolution.marketing_name;
                m_current_client.is_exact_model_match = (resolution.match_type == ModelDbMatch::Exact);
                m_current_client.device_id            = Utf8ToWide(devid_str);
                m_current_client.os_name              = AppleModelDatabase::DetectOsName(m_current_client.model, m_current_client.user_agent);

                m_state.SetClientInfo(m_current_client);
                m_state.TransitionState(AirPlaySessionState::Connected);
                duwn::telemetry::ConnectionTimeline::Get().Record(
                    duwn::telemetry::ConnectionMilestone::C6_ControlConnectionAccepted, line);
                DUWN_LOG_INFOF("AirPlayProcess",
                    "Client metadata parsed: Name='{}' Model='{}' ({}) DeviceID='{}'",
                    name_str, model_str, WideToUtf8(m_current_client.model_marketing_name), devid_str);
                return;
            }
        }
    }

    // 2. Client IP address: "connection from client <IP>" or "connection from <IP>"
    if (line.find("connection from ") != std::string::npos && line.find("connection request from") == std::string_view::npos) {
        m_state.RecordControlActivity(now_ns);
        size_t ip_start = line.find("client ");
        if (ip_start != std::string_view::npos) {
            ip_start += 7;
        } else {
            ip_start = line.find("connection from ") + 16;
        }
        std::string ip_str(line.substr(ip_start));
        while (!ip_str.empty() && (ip_str.back() == '\r' || ip_str.back() == ' ' || ip_str.back() == '\n')) {
            ip_str.pop_back();
        }
        if (!ip_str.empty()) {
            m_current_client.peer_address = Utf8ToWide(ip_str);
            m_state.SetClientInfo(m_current_client);
            ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::TcpClientConnected, ip_str);
            duwn::telemetry::ConnectionTimeline::Get().Record(
                duwn::telemetry::ConnectionMilestone::C6_ControlConnectionAccepted, ip_str);
        }
        m_state.TransitionState(AirPlaySessionState::Connecting);
        DUWN_LOG_INFOF("AirPlayProcess", "Client connecting from IP: {}", ip_str);
        return;
    }

    // 3. User-Agent identification: "Client identified as User-Agent: <ua>"
    if (line.find("Client identified as User-Agent: ") != std::string::npos) {
        m_state.RecordControlActivity(now_ns);
        std::string ua(line.substr(line.find("User-Agent: ") + 12));
        while (!ua.empty() && (ua.back() == '\r' || ua.back() == ' ' || ua.back() == '\n')) {
            ua.pop_back();
        }
        m_current_client.user_agent = Utf8ToWide(ua);

        // Parse AirPlay source version e.g. "AirPlay/660.8.1"
        auto pos = ua.find("AirPlay/");
        if (pos != std::string::npos) {
            std::string src_ver = ua.substr(pos + 8);
            auto space = src_ver.find(' ');
            if (space != std::string::npos) src_ver = src_ver.substr(0, space);
            m_current_client.source_version = Utf8ToWide(src_ver);
        }

        m_current_client.os_name = AppleModelDatabase::DetectOsName(m_current_client.model, m_current_client.user_agent);
        m_state.SetClientInfo(m_current_client);
        DUWN_LOG_INFOF("AirPlayProcess", "Client User-Agent: {}", ua);
        return;
    }

    // RTSP protocol and Pairing activity detection for connection telemetry
    if (line.find("pair-setup") != std::string_view::npos ||
        line.find("pair-verify") != std::string_view::npos ||
        line.find("Pair-Setup") != std::string_view::npos ||
        line.find("Pair-Verify") != std::string_view::npos) {
        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::PairingStarted, line);
    }
    if (line.find("pair-verify: successful") != std::string_view::npos ||
        line.find("Pair-Verify successful") != std::string_view::npos ||
        line.find("Pairing verified") != std::string_view::npos ||
        line.find("pairing verified") != std::string_view::npos) {
        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::PairingComplete, line);
    }
    if (line.find("RTSP") != std::string_view::npos ||
        line.find("ANNOUNCE") != std::string_view::npos ||
        line.find("OPTIONS") != std::string_view::npos ||
        line.find("SETUP") != std::string_view::npos ||
        line.find("RECORD") != std::string_view::npos) {
        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::RtspStarted, line);
    }

    // 4. Video stream started: "video: <W>x<H>@<FPS> <codec>" or "*** video format is h265 ... video <W>x<H>"
    const bool is_video_colon = (line.find("video:") != std::string::npos);
    const bool is_video_format = (line.find("video format is") != std::string::npos);

    if (is_video_colon || is_video_format) {
        m_state.RecordControlActivity(now_ns);
        m_state.RecordVideoPacket(now_ns);

        StreamMetadata meta = m_current_meta;
        unsigned w = 0, h = 0;
        float fps = 0;
        if (is_video_colon) {
            if (::sscanf_s(line.data() + line.find("video:") + 6,
                            " %ux%u@%f", &w, &h, &fps) >= 2) {
                meta.video_width  = w;
                meta.video_height = h;
                meta.video_fps    = static_cast<double>(fps > 0 ? fps : 0);
                meta.video_codec  = line.find("h265") != std::string::npos
                                        ? VideoCodec::H265 : VideoCodec::H264;
            }
        } else if (is_video_format) {
            size_t vpos = line.rfind("video ");
            if (vpos != std::string::npos) {
                ::sscanf_s(line.data() + vpos + 6, "%ux%u", &w, &h);
            }
            if (w > 0 && h > 0) {
                meta.video_width  = w;
                meta.video_height = h;
                meta.video_fps    = m_config.max_fps > 0 ? static_cast<double>(m_config.max_fps) : 60.0;
                meta.video_codec  = (line.find("h265") != std::string::npos || line.find("H265") != std::string::npos)
                                        ? VideoCodec::H265 : VideoCodec::H264;
            }
        }
        m_current_meta = meta;
        {
            std::lock_guard lock(m_codec_mutex);
            if (m_detected_video_codec == "UNKNOWN") {
                if (meta.video_codec == VideoCodec::H265) {
                    m_detected_video_codec = "H265";
                    m_codec_evidence = "UxPlayVideoLine (H.265)";
                } else {
                    m_detected_video_codec = "H264";
                    m_codec_evidence = "UxPlayVideoLine (H.264)";
                }
                DUWN_LOG_INFOF("WiredCodecProbe", "Detected video codec via video line: {} (evidence={})",
                    m_detected_video_codec, m_codec_evidence);
            }
        }
        DUWN_LOG_INFOF("AirPlayProcess",
            "Video stream: {}x{}@{:.2f} {}",
            meta.video_width, meta.video_height, meta.video_fps,
            meta.video_codec == VideoCodec::H265 ? "H.265" : "H.264");
        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::VideoSetupReceived,
            std::format("{}x{}@{:.2f} {}", meta.video_width, meta.video_height, meta.video_fps,
                        meta.video_codec == VideoCodec::H265 ? "H.265" : "H.264"));
        duwn::telemetry::ConnectionTimeline::Get().Record(
            duwn::telemetry::ConnectionMilestone::C7_SessionSetupComplete,
            std::format("{}x{}@{:.2f} {}", meta.video_width, meta.video_height, meta.video_fps,
                        meta.video_codec == VideoCodec::H265 ? "H.265" : "H.264"));
        if (m_on_metadata) m_on_metadata(meta);
        m_state.TransitionState(AirPlaySessionState::Streaming);
        return;
    }

    // 5. Audio format
    if (line.find("audio:") != std::string::npos) {
        m_state.RecordControlActivity(now_ns);
        StreamMetadata meta = m_current_meta;
        unsigned rate = 0; unsigned ch = 0;
        ::sscanf_s(line.data() + line.find("audio:") + 6,
                   " %u Hz %u ch", &rate, &ch);
        if (rate) meta.audio_sample_rate = rate;
        if (ch)   meta.audio_channels    = static_cast<uint8_t>(ch);

        if (line.find("AAC-ELD") != std::string::npos)
            meta.audio_codec = AudioCodec::AAC_ELD;
        else if (line.find("AAC") != std::string::npos)
            meta.audio_codec = AudioCodec::AAC;
        else if (line.find("ALAC") != std::string::npos)
            meta.audio_codec = AudioCodec::ALAC;
        else if (line.find("PCM") != std::string::npos)
            meta.audio_codec = AudioCodec::PCM;

        meta.audio_rtp_clock_rate = meta.audio_sample_rate;
        m_current_meta = meta;
        ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::AudioSetupReceived,
            std::format("{} Hz {} ch", meta.audio_sample_rate, meta.audio_channels));
        if (m_on_metadata) m_on_metadata(meta);
        return;
    }

    // 6. Client FPSdata / performance report parsing from UxPlay -FPSdata
    if (!m_pending_fps_key.empty()) {
        auto v_real_start = line.find("<real>");
        auto v_real_end   = line.find("</real>");
        auto v_int_start  = line.find("<integer>");
        auto v_int_end    = line.find("</integer>");
        if (v_real_start != std::string_view::npos && v_real_end != std::string_view::npos) {
            std::string val_str(line.substr(v_real_start + 6, v_real_end - (v_real_start + 6)));
            ApplyFpsKeyValue(m_pending_fps_key, val_str);
            m_pending_fps_key.clear();
            return;
        } else if (v_int_start != std::string_view::npos && v_int_end != std::string_view::npos) {
            std::string val_str(line.substr(v_int_start + 9, v_int_end - (v_int_start + 9)));
            ApplyFpsKeyValue(m_pending_fps_key, val_str);
            m_pending_fps_key.clear();
            return;
        } else {
            char* end_ptr = nullptr;
            std::string s(line);
            std::strtod(s.c_str(), &end_ptr);
            if (end_ptr != s.c_str()) {
                ApplyFpsKeyValue(m_pending_fps_key, s);
            }
            m_pending_fps_key.clear();
        }
    }

    auto k_start = line.find("<key>");
    auto k_end   = line.find("</key>");
    if (k_start != std::string_view::npos && k_end != std::string_view::npos && k_end > k_start + 5) {
        m_state.RecordControlActivity(now_ns);
        std::string_view key = line.substr(k_start + 5, k_end - (k_start + 5));

        std::string_view rest = line.substr(k_end + 6);
        auto v_real_start = rest.find("<real>");
        auto v_real_end   = rest.find("</real>");
        auto v_int_start  = rest.find("<integer>");
        auto v_int_end    = rest.find("</integer>");
        if (v_real_start != std::string_view::npos && v_real_end != std::string_view::npos) {
            std::string val_str(rest.substr(v_real_start + 6, v_real_end - (v_real_start + 6)));
            ApplyFpsKeyValue(key, val_str);
            m_pending_fps_key.clear();
        } else if (v_int_start != std::string_view::npos && v_int_end != std::string_view::npos) {
            std::string val_str(rest.substr(v_int_start + 9, v_int_end - (v_int_start + 9)));
            ApplyFpsKeyValue(key, val_str);
            m_pending_fps_key.clear();
        } else {
            m_pending_fps_key = std::string(key);
        }
        return;
    }

    if (line.find("FPS") != std::string::npos || line.find("fps") != std::string::npos) {
        float client_fps_val = 0.0f;
        if (::sscanf_s(line.data(), " client fps: %f", &client_fps_val) == 1 ||
            ::sscanf_s(line.data(), " FPS: %f", &client_fps_val) == 1) {
            m_state.RecordClientFps(now_ns);
            GlobalMetrics().client_fps.store(client_fps_val, std::memory_order_relaxed);
            DUWN_LOG_INFOF("UxPlay", "[UxPlay] config_gen={} encoderCurrentFPS={:.2f}", m_generation, client_fps_val);
        }
    }

    // 7. Disconnect / Teardown
    if (line.find("disconnect") != std::string::npos
        || line.find("TEARDOWN") != std::string::npos) {
        DUWN_LOG_INFO("AirPlayProcess", "Client disconnected");
        m_current_client.Clear();
        m_state.ClearClientInfo();
        m_current_meta = StreamMetadata{};
        {
            std::lock_guard lock(m_codec_mutex);
            m_detected_video_codec = "UNKNOWN";
            m_codec_evidence.clear();
        }
        m_state.TransitionState(AirPlaySessionState::Disconnecting);
        return;
    }
}

} // namespace duwn::airplay
