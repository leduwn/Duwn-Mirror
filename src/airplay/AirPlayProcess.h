#pragma once
// AirPlayProcess — manages the UxPlay sidecar process.
//
// Responsibilities:
//   - Spawn uxplay.exe with -vrtp/-artp flags pointing to loopback ports.
//   - Monitor process health; restart on crash.
//   - Host dedicated ControlIpcServer for low-rate structured control messages.
//   - Parse UxPlay stdout for session events (connected, metadata, disconnect).
//   - Feed StreamMetadata, AirPlayClientInfo, and SessionPhase transitions to the engine.

#include "SessionState.h"
#include "StreamMetadata.h"
#include "AirPlayClientInfo.h"
#include "ControlIpcServer.h"
#include <functional>
#include <atomic>
#include <thread>
#include <string>
#include <memory>
#include <cstdint>
#include <windows.h>

namespace duwn::airplay {

struct AirPlayProcessConfig {
    std::wstring uxplay_exe_path; // Full path to uxplay.exe
    std::wstring receiver_name;   // Advertised AirPlay name (UTF-16)
    std::wstring bind_ipv4;       // Empty in Wireless mode; required Apple USB IPv4 in Wired mode
    uint8_t     bind_prefix{0};
    uint16_t     video_rtp_port;  // Local port DUWN is listening on for video RTP
    uint16_t     audio_rtp_port;  // Local port DUWN is listening on for audio RTP
    uint16_t     airplay_port_base{7000}; // Deterministic AirPlay port set base (-p base, base+1, base+2)
    uint32_t     max_fps{60};     // Advertised max FPS (e.g. 60)
    uint32_t     receiver_width{1920};
    uint32_t     receiver_height{1920};
    uint64_t     config_generation{1};
    std::string  receiver_quality_name{"Auto"};
    bool         enable_fps_data{true}; // Enable -FPSdata for client streaming reports
    bool         debug_log{false};
    std::wstring control_pipe_name{L"\\\\.\\pipe\\duwn-mirror-control"};
};

struct AirPlayEnvelope {
    uint32_t width{1920};
    uint32_t height{1920};
    uint32_t fps{60};
};

AirPlayEnvelope ComputeSquareAirPlayEnvelope(
    uint32_t width,
    uint32_t height,
    uint32_t fps) noexcept;

using MetadataCallback = std::function<void(const StreamMetadata&)>;

class AirPlayProcess {
public:
    AirPlayProcess(AirPlayProcessConfig config,
                   SessionState&        state,
                   MetadataCallback     on_metadata) noexcept;
    ~AirPlayProcess();

    AirPlayProcess(const AirPlayProcess&) = delete;
    AirPlayProcess& operator=(const AirPlayProcess&) = delete;

    // Start the sidecar and supervision loop.
    bool Start() noexcept;

    // Gracefully stop sidecar and supervision thread.
    void Stop() noexcept;

    // Reset crash circuit breaker and retry starting sidecar.
    void ManualRetry() noexcept;

    // Returns the last meaningful error captured from sidecar stderr or preflight.
    std::string GetLastRuntimeError() const noexcept;

    bool IsAlive() const noexcept {
        if (!m_proc_info.hProcess) return false;
        DWORD code = 0;
        return ::GetExitCodeProcess(m_proc_info.hProcess, &code) && (code == STILL_ACTIVE);
    }

    uint64_t Generation() const noexcept { return m_generation; }
    DWORD GetPid() const noexcept { return m_proc_info.dwProcessId ? m_proc_info.dwProcessId : m_last_pid; }

    uint16_t ActiveAirPlayPortBase() const noexcept { return m_active_port_base; }
    static bool IsPortBlockAvailable(uint16_t base) noexcept;
    static uint16_t SelectDeterministicAirPlayPort(uint16_t preferred_base) noexcept;

    std::wstring BuildCommandLine() const noexcept;
    HANDLE JobObjectHandle() const noexcept { return m_job_object; }
    HANDLE ProcessHandle() const noexcept { return m_proc_info.hProcess; }

    std::string GetDetectedVideoCodec() const noexcept {
        std::lock_guard lock(m_codec_mutex);
        return m_detected_video_codec;
    }
    std::string GetVideoCodecEvidence() const noexcept {
        std::lock_guard lock(m_codec_mutex);
        return m_codec_evidence;
    }

private:
    bool PreflightRuntimeValidation() noexcept;
    bool VerifySidecar() noexcept;
    bool SpawnProcess() noexcept;
    void KillProcess(std::string_view reason = "Unknown") noexcept;
    void TripCircuitBreaker() noexcept;
    void SupervisionLoop(std::stop_token stop) noexcept;
    void ReadStdout(std::stop_token stop) noexcept;
    void ReadStderr(std::stop_token stop) noexcept;
    void ParseLine(std::string_view line) noexcept;
    void ApplyFpsKeyValue(std::string_view key, std::string_view val_str) noexcept;
    void HandleControlMessage(const ControlMessage& msg) noexcept;
    void SetLastRuntimeError(std::string_view err) noexcept;

    AirPlayProcessConfig              m_config;
    uint16_t                          m_active_port_base{7000};
    uint64_t                          m_generation{1};
    std::string                       m_quality_name{"Auto"};
    DWORD                             m_last_pid{0};
    SessionState&                     m_state;
    MetadataCallback                  m_on_metadata;
    StreamMetadata                    m_current_meta;
    AirPlayClientInfo                 m_current_client;
    std::string                       m_pending_fps_key;
    bool                              m_active_audio_pipeline_logged{false};
    std::unique_ptr<ControlIpcServer> m_control_server;

    PROCESS_INFORMATION               m_proc_info{};
    HANDLE                            m_stdout_read{INVALID_HANDLE_VALUE};
    HANDLE                            m_stderr_read{INVALID_HANDLE_VALUE};
    HANDLE                            m_job_object{nullptr};
    std::atomic<bool>                 m_sockets_ready{false};
    HANDLE                            m_socket_ready_event{nullptr};
    std::atomic_bool                  m_running{false};
    std::atomic_bool                  m_circuit_breaker_tripped{false};
    mutable std::mutex                m_error_mutex;
    std::string                       m_last_stderr_error;
    mutable std::mutex                m_codec_mutex;
    std::string                       m_detected_video_codec{"UNKNOWN"};
    std::string                       m_codec_evidence;

    std::jthread                      m_supervision_thread;
    std::jthread                      m_stdout_thread;
    std::jthread                      m_stderr_thread;
};

} // namespace duwn::airplay
