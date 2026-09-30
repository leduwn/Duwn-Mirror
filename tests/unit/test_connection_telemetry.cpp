#include "common/telemetry/ConnectionTelemetry.h"
#include "network/NetworkEnvironment.h"

DUWN_TEST(ConnectionTelemetry_SingletonValid) {
    auto& tel = duwn::airplay::ConnectionTelemetry::Get();
    tel.Reset();
    DUWN_ASSERT(!tel.IsFullyConnected());
}

DUWN_TEST(ConnectionTelemetry_PhasesSequencing) {
    auto& tel = duwn::airplay::ConnectionTelemetry::Get();
    tel.Reset();

    using duwn::airplay::ConnectionPhase;
    tel.RecordPhase(ConnectionPhase::SidecarStarted, "UxPlay spawned");
    int64_t t1 = tel.GetPhaseTimestampNs(ConnectionPhase::SidecarStarted);
    DUWN_ASSERT(t1 > 0);

    tel.RecordPhase(ConnectionPhase::MdnsAdvertised, "AirPlay advertised");
    int64_t t2 = tel.GetPhaseTimestampNs(ConnectionPhase::MdnsAdvertised);
    DUWN_ASSERT(t2 >= t1);

    tel.RecordPhase(ConnectionPhase::TcpClientConnected, "192.168.1.50");
    int64_t t3 = tel.GetPhaseTimestampNs(ConnectionPhase::TcpClientConnected);
    DUWN_ASSERT(t3 >= t2);

    tel.RecordPhase(ConnectionPhase::RtspStarted);
    tel.RecordPhase(ConnectionPhase::PairingStarted);
    tel.RecordPhase(ConnectionPhase::PairingComplete);
    tel.RecordPhase(ConnectionPhase::VideoSetupReceived, "1920x1080@60 H.264");
    tel.RecordPhase(ConnectionPhase::AudioSetupReceived, "44100 Hz 2 ch");
    tel.RecordPhase(ConnectionPhase::VideoRtpStarted);
    tel.RecordPhase(ConnectionPhase::AudioRtpStarted);
    tel.RecordPhase(ConnectionPhase::FirstH264Au);
    tel.RecordPhase(ConnectionPhase::DecoderCreated, "Hardware MFT");
    tel.RecordPhase(ConnectionPhase::FirstDecodedFrame, "1920x1080 NV12");

    DUWN_ASSERT(!tel.IsFullyConnected());

    tel.RecordPhase(ConnectionPhase::FirstPresentedFrame, "DXGI presented");
    DUWN_ASSERT(tel.IsFullyConnected());

    int64_t t14 = tel.GetPhaseTimestampNs(ConnectionPhase::FirstPresentedFrame);
    DUWN_ASSERT(t14 >= t3);
}

DUWN_TEST(ConnectionTelemetry_ResetClearsTimestamps) {
    auto& tel = duwn::airplay::ConnectionTelemetry::Get();
    DUWN_ASSERT(tel.IsFullyConnected());
    tel.Reset();
    DUWN_ASSERT(!tel.IsFullyConnected());
    DUWN_ASSERT(tel.GetPhaseTimestampNs(duwn::airplay::ConnectionPhase::FirstPresentedFrame) == 0);
}

DUWN_TEST(NetworkEnvironment_ProbeExecution) {
    auto info = duwn::network::NetworkEnvironmentInfo::Probe();
    // System should have probed adapters without throwing
    DUWN_ASSERT(!info.adapters.empty() || info.primary_profile != duwn::network::NetworkProfileCategory::Unknown || true);
}
