#include "AirPlayEngine.h"
#include "common/logging/Logger.h"
#include "common/telemetry/ConnectionTelemetry.h"
#include <format>
#include <mutex>

namespace duwn::airplay {

AirPlayEngine::AirPlayEngine(AirPlayEngineConfig config) noexcept
    : m_config(std::move(config)) {}

AirPlayEngine::~AirPlayEngine() {
    Stop();
}

void AirPlayEngine::SetVideoCallback(VideoDataCallback cb) noexcept {
    m_video_cb = std::move(cb);
}
void AirPlayEngine::SetAudioCallback(AudioDataCallback cb) noexcept {
    m_audio_cb = std::move(cb);
}
void AirPlayEngine::SetPhaseCallback(PhaseCallback cb) noexcept {
    m_state.SetCallback(std::move(cb));
}
void AirPlayEngine::SetStateCallback(StateCallback cb) noexcept {
    m_state.SetStateCallback(std::move(cb));
}
void AirPlayEngine::SetClientInfoCallback(ClientInfoCallback cb) noexcept {
    m_state.SetClientInfoCallback(std::move(cb));
}
void AirPlayEngine::SetMetadataCallback(MetadataCallback cb) noexcept {
    m_meta_cb = std::move(cb);
}

StreamMetadata AirPlayEngine::CurrentMetadata() const noexcept {
    std::lock_guard lock{m_metadata_mutex};
    return m_metadata;
}

bool AirPlayEngine::Start() noexcept {
    // 1. Bind video + audio RTP receivers on dynamic loopback ports.
    m_video_recv = std::make_unique<network::RtpReceiver>(
        [this](const network::RtpPacket& pkt) { OnVideoRtp(pkt); });
    m_audio_recv = std::make_unique<network::RtpReceiver>(
        [this](const network::RtpPacket& pkt) { OnAudioRtp(pkt); });

    uint16_t vport = m_video_recv->Start();
    uint16_t aport = m_audio_recv->Start();

    if (!vport || !aport) {
        DUWN_LOG_ERROR("AirPlayEngine", "Failed to bind RTP receivers");
        return false;
    }

    DUWN_LOG_INFOF("AirPlayEngine",
        "Video RTP port={} Audio RTP port={}", vport, aport);

    // 2. Start UxPlay sidecar pointed at those ports.
    AirPlayProcessConfig proc_cfg;
    proc_cfg.uxplay_exe_path        = m_config.uxplay_exe_path;
    proc_cfg.receiver_name          = m_config.receiver_name;
    proc_cfg.bind_ipv4              = m_config.bind_ipv4;
    proc_cfg.bind_prefix            = m_config.bind_prefix;
    proc_cfg.video_rtp_port         = vport;
    proc_cfg.audio_rtp_port         = aport;
    proc_cfg.airplay_port_base      = m_config.airplay_port_base;
    proc_cfg.max_fps                = m_config.max_fps;
    proc_cfg.receiver_width         = m_config.receiver_width;
    proc_cfg.receiver_height        = m_config.receiver_height;
    proc_cfg.config_generation      = m_config.config_generation;
    proc_cfg.receiver_quality_name  = m_config.receiver_quality_name;
    proc_cfg.enable_fps_data        = m_config.enable_fps_data;
    proc_cfg.debug_log              = m_config.debug_log;
    proc_cfg.is_wired               = m_config.is_wired;

    m_process = std::make_unique<AirPlayProcess>(
        std::move(proc_cfg),
        m_state,
        [this](const StreamMetadata& m) { OnMetadata(m); }
    );

    return m_process->Start();
}

void AirPlayEngine::Stop() noexcept {
    if (m_process)     m_process->Stop();
    if (m_video_recv)  m_video_recv->Stop();
    if (m_audio_recv)  m_audio_recv->Stop();
    m_state.Transition(SessionPhase::Idle);
}

bool AirPlayEngine::SetReceiverQuality(uint32_t width, uint32_t height, uint32_t fps) noexcept {
    Stop();
    ConfigureReceiverQuality(width, height, fps);
    return Start();
}

void AirPlayEngine::OnVideoRtp(const network::RtpPacket& pkt) noexcept {
    ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::VideoRtpStarted,
        std::format("seq={} size={}", pkt.sequence, pkt.payload.size()));
    m_state.RecordVideoPacket(pkt.arrival_ns);
    if (m_video_cb) {
        m_video_cb(pkt.payload.data(), pkt.payload.size(),
                   pkt.timestamp, pkt.arrival_ns,
                   pkt.marker, pkt.sequence);
    }
}

void AirPlayEngine::OnAudioRtp(const network::RtpPacket& pkt) noexcept {
    ConnectionTelemetry::Get().RecordPhase(ConnectionPhase::AudioRtpStarted,
        std::format("ts={} size={}", pkt.timestamp, pkt.payload.size()));
    m_state.RecordAudioPacket(pkt.arrival_ns);
    if (m_audio_cb) {
        m_audio_cb(pkt.payload.data(), pkt.payload.size(),
                   pkt.timestamp, pkt.arrival_ns);
    }
}

void AirPlayEngine::OnMetadata(const StreamMetadata& meta) noexcept {
    {
        std::lock_guard lock{m_metadata_mutex};
        m_metadata = meta;
    }
    if (m_meta_cb) m_meta_cb(meta);
}

} // namespace duwn::airplay
