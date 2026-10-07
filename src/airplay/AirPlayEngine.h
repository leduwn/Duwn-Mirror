#pragma once
// AirPlayEngine — top-level coordinator for the AirPlay subsystem.
// Owns: AirPlayProcess, video/audio RtpReceivers, JitterBuffers.
// Provides callbacks to the media engine for decoded stream data.

#include "AirPlayProcess.h"
#include "SessionState.h"
#include "StreamMetadata.h"
#include "network/RtpReceiver.h"
#include "network/JitterBuffer.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace duwn::airplay {

// Callback: raw H.264/H.265 NAL unit data + RTP timestamp + marker + seq.
// Called on the video RTP recv thread. MUST NOT block.
using VideoDataCallback = std::function<void(
    const uint8_t* data, size_t size, uint32_t rtp_ts, int64_t arrival_ns,
    bool marker, uint16_t seq)>;

// Callback: raw compressed audio data + RTP timestamp.
using AudioDataCallback = std::function<void(
    const uint8_t* data, size_t size, uint32_t rtp_ts, int64_t arrival_ns)>;

struct AirPlayEngineConfig {
    std::wstring uxplay_exe_path;
    std::wstring receiver_name{L"Duwn Mirror"};
    std::wstring bind_ipv4;
    uint8_t bind_prefix{0};
    uint16_t     airplay_port_base{7000};
    uint32_t     max_fps{60};
    uint32_t     receiver_width{1920};
    uint32_t     receiver_height{1080};
    uint64_t     config_generation{1};
    std::string  receiver_quality_name{"Auto"};
    bool         enable_fps_data{true};
    bool         debug_log{false};
    bool         is_wired{false};
};

class AirPlayEngine {
public:
    explicit AirPlayEngine(AirPlayEngineConfig config) noexcept;
    ~AirPlayEngine();

    AirPlayEngine(const AirPlayEngine&) = delete;
    AirPlayEngine& operator=(const AirPlayEngine&) = delete;

    void SetVideoCallback(VideoDataCallback cb) noexcept;
    void SetAudioCallback(AudioDataCallback cb) noexcept;
    void SetPhaseCallback(PhaseCallback cb) noexcept;
    void SetStateCallback(StateCallback cb) noexcept;
    void SetClientInfoCallback(ClientInfoCallback cb) noexcept;
    void SetMetadataCallback(MetadataCallback cb) noexcept;

    bool Start() noexcept;
    void Stop() noexcept;
    bool SetReceiverQuality(uint32_t width, uint32_t height, uint32_t fps) noexcept;
    void ConfigureReceiverQuality(uint32_t width, uint32_t height, uint32_t fps,
                                  uint64_t generation = 1,
                                  std::string_view quality_name = "Auto") noexcept {
        m_config.receiver_width = width;
        m_config.receiver_height = height;
        m_config.max_fps = fps;
        m_config.config_generation = generation;
        m_config.receiver_quality_name = std::string(quality_name);
    }
    void SetBindIpv4(std::wstring address, uint8_t prefix = 0) noexcept {
        m_config.bind_ipv4 = std::move(address);
        m_config.bind_prefix = prefix;
    }
    void SetIsWired(bool wired) noexcept { m_config.is_wired = wired; }
    bool IsWired() const noexcept { return m_config.is_wired; }

    uint64_t GetSidecarGeneration() const noexcept { return m_process ? m_process->Generation() : 0; }
    DWORD GetSidecarPid() const noexcept { return m_process ? m_process->GetPid() : 0; }

    SessionPhase        CurrentPhase() const noexcept { return m_state.Current(); }
    AirPlaySessionState CurrentSessionState() const noexcept { return m_state.CurrentState(); }
    AirPlayClientInfo   CurrentClientInfo() const noexcept { return m_state.GetClientInfo(); }
    void EvaluateSessionLiveness(int64_t now_ns) noexcept { m_state.EvaluateSessionLiveness(now_ns); }
    StreamMetadata CurrentMetadata() const noexcept;
    bool IsSidecarAlive() const noexcept { return m_process && m_process->IsAlive(); }
    std::string GetLastRuntimeError() const noexcept { return m_process ? m_process->GetLastRuntimeError() : ""; }
    void ManualRetry() noexcept { if (m_process) m_process->ManualRetry(); }
    std::string GetDetectedVideoCodec() const noexcept { return m_process ? m_process->GetDetectedVideoCodec() : "UNKNOWN"; }
    std::string GetVideoCodecEvidence() const noexcept { return m_process ? m_process->GetVideoCodecEvidence() : ""; }

private:
    void OnVideoRtp(const network::RtpPacket& pkt) noexcept;
    void OnAudioRtp(const network::RtpPacket& pkt) noexcept;
    void OnMetadata(const StreamMetadata& meta) noexcept;

    AirPlayEngineConfig m_config;
    SessionState        m_state;
    StreamMetadata      m_metadata;

    std::unique_ptr<network::RtpReceiver> m_video_recv;
    std::unique_ptr<network::RtpReceiver> m_audio_recv;
    std::unique_ptr<AirPlayProcess>       m_process;

    VideoDataCallback   m_video_cb;
    AudioDataCallback   m_audio_cb;
    MetadataCallback    m_meta_cb;

    mutable std::mutex  m_metadata_mutex;
};

} // namespace duwn::airplay
