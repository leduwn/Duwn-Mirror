#include "common/telemetry/ConnectionTimeline.h"
#include "ui/UiState.h"
#include <string>

using namespace duwn::telemetry;

DUWN_TEST(ConnectionTimeline_MilestoneNamingAndCodes) {
    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        auto m = static_cast<ConnectionMilestone>(i);
        const char* code = ConnectionMilestoneCode(m);
        const char* name = ConnectionMilestoneName(m);
        DUWN_ASSERT(code != nullptr && code[0] == 'C');
        DUWN_ASSERT(name != nullptr && std::string(name) != "Unknown Milestone");
    }
}

DUWN_TEST(ConnectionTimeline_RecordAndMonotonicOrdering) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        auto m = static_cast<ConnectionMilestone>(i);
        DUWN_ASSERT(timeline.GetTimestampNs(m) == 0);
        timeline.Record(m, "test");
        int64_t ts = timeline.GetTimestampNs(m);
        DUWN_ASSERT(ts > 0);
        if (i > 0) {
            auto prev_m = static_cast<ConnectionMilestone>(i - 1);
            DUWN_ASSERT(ts >= timeline.GetTimestampNs(prev_m));
        }
    }
}

DUWN_TEST(ConnectionTimeline_IdempotentRecord) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    timeline.Record(ConnectionMilestone::C0_ProcessStart, "first");
    int64_t ts1 = timeline.GetTimestampNs(ConnectionMilestone::C0_ProcessStart);
    DUWN_ASSERT(ts1 > 0);

    timeline.Record(ConnectionMilestone::C0_ProcessStart, "second");
    int64_t ts2 = timeline.GetTimestampNs(ConnectionMilestone::C0_ProcessStart);
    DUWN_ASSERT(ts1 == ts2);
}

DUWN_TEST(ConnectionTimeline_ElapsedCalculations) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    DUWN_ASSERT(timeline.GetElapsedMs(ConnectionMilestone::C0_ProcessStart, ConnectionMilestone::C12_FirstPreviewPresent) < 0.0);

    timeline.Record(ConnectionMilestone::C0_ProcessStart);
    timeline.Record(ConnectionMilestone::C12_FirstPreviewPresent);

    double elapsed = timeline.GetElapsedMs(ConnectionMilestone::C0_ProcessStart, ConnectionMilestone::C12_FirstPreviewPresent);
    DUWN_ASSERT(elapsed >= 0.0);
}

DUWN_TEST(ConnectionTimeline_SessionReset) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        timeline.Record(static_cast<ConnectionMilestone>(i));
    }

    timeline.ResetSession();

    // C0..C5 preserved
    for (int i = 0; i <= static_cast<int>(ConnectionMilestone::C5_AdvertisingReady); ++i) {
        DUWN_ASSERT(timeline.GetTimestampNs(static_cast<ConnectionMilestone>(i)) > 0);
    }
    // C6..C15 cleared
    for (int i = static_cast<int>(ConnectionMilestone::C6_ControlConnectionAccepted);
         i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        DUWN_ASSERT(timeline.GetTimestampNs(static_cast<ConnectionMilestone>(i)) == 0);
    }
}

DUWN_TEST(ConnectionTimeline_ResetAll) {
    auto& timeline = ConnectionTimeline::Get();
    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        timeline.Record(static_cast<ConnectionMilestone>(i));
    }
    timeline.ResetAll();
    for (int i = 0; i < static_cast<int>(ConnectionMilestone::_COUNT); ++i) {
        DUWN_ASSERT(timeline.GetTimestampNs(static_cast<ConnectionMilestone>(i)) == 0);
    }
}

DUWN_TEST(ConnectionTimeline_ControlToFirstFrame) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    timeline.Record(ConnectionMilestone::C6_ControlConnectionAccepted);
    timeline.Record(ConnectionMilestone::C12_FirstPreviewPresent);

    double ctrl_to_preview = timeline.GetElapsedMs(
        ConnectionMilestone::C6_ControlConnectionAccepted,
        ConnectionMilestone::C12_FirstPreviewPresent);
    DUWN_ASSERT(ctrl_to_preview >= 0.0);
}

DUWN_TEST(ConnectionTimeline_FirstAudioLatency) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    timeline.Record(ConnectionMilestone::C14_FirstAudioRtp);
    timeline.Record(ConnectionMilestone::C15_FirstWasapiWrite);

    double audio_lat = timeline.GetElapsedMs(
        ConnectionMilestone::C14_FirstAudioRtp,
        ConnectionMilestone::C15_FirstWasapiWrite);
    DUWN_ASSERT(audio_lat >= 0.0);
}

DUWN_TEST(ConnectionTimeline_FormatReport) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();
    timeline.Record(ConnectionMilestone::C0_ProcessStart);
    timeline.Record(ConnectionMilestone::C1_NetworkDiscoveryComplete);

    std::string report = timeline.FormatReport();
    DUWN_ASSERT(report.find("CONNECTION TIMELINE MILESTONES") != std::string::npos);
    DUWN_ASSERT(report.find("C0") != std::string::npos);
    DUWN_ASSERT(report.find("Process Start") != std::string::npos);
    DUWN_ASSERT(report.find("C1") != std::string::npos);
    DUWN_ASSERT(report.find("Network Discovery Complete") != std::string::npos);
    DUWN_ASSERT(report.find("C15") != std::string::npos);
    DUWN_ASSERT(report.find("First Real Audio WASAPI Write") != std::string::npos);
    DUWN_ASSERT(report.find("[NOT REACHED]") != std::string::npos);
}

DUWN_TEST(ConnectionTimeline_BitrateSemanticConsistency) {
    duwn::ui::UiState state;
    state.video_bitrate_mbps = 18.52;
    state.media_bitrate_mbps = state.video_bitrate_mbps;

    DUWN_ASSERT(state.media_bitrate_mbps == 18.52);
    DUWN_ASSERT(state.media_bitrate_mbps == state.video_bitrate_mbps);
}

DUWN_TEST(ConnectionTimeline_MilestoneSemanticsDecoupling) {
    auto& timeline = ConnectionTimeline::Get();
    timeline.ResetAll();

    // Verify C5 naming is "Application Ready for Client Media Session"
    const char* c5_name = ConnectionMilestoneName(ConnectionMilestone::C5_MediaSessionReady);
    DUWN_ASSERT(std::string(c5_name) == "Application Ready for Client Media Session");

    // Verify C5_AdvertisingReady is alias for C5_MediaSessionReady
    DUWN_ASSERT(static_cast<int>(ConnectionMilestone::C5_AdvertisingReady) ==
                static_cast<int>(ConnectionMilestone::C5_MediaSessionReady));

    // Record C0, C4B, and C5
    timeline.Record(ConnectionMilestone::C0_ProcessStart);
    // Simulate brief progression
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timeline.Record(ConnectionMilestone::C4B_AdvertisementActiveInternal);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timeline.Record(ConnectionMilestone::C5_MediaSessionReady);

    double discovery_ready_ms = timeline.GetDiscoveryReadyInternalMs();
    double media_session_ready_ms = timeline.GetMediaSessionReadyMs();

    DUWN_ASSERT(discovery_ready_ms > 0.0);
    DUWN_ASSERT(media_session_ready_ms > 0.0);
    DUWN_ASSERT(media_session_ready_ms >= discovery_ready_ms);

    // Verify decoupled user-facing metrics
    double elapsed_c0_c4b = timeline.GetElapsedMs(
        ConnectionMilestone::C0_ProcessStart,
        ConnectionMilestone::C4B_AdvertisementActiveInternal);
    double elapsed_c0_c5 = timeline.GetElapsedMs(
        ConnectionMilestone::C0_ProcessStart,
        ConnectionMilestone::C5_MediaSessionReady);

    DUWN_ASSERT(std::abs(discovery_ready_ms - elapsed_c0_c4b) < 0.001);
    DUWN_ASSERT(std::abs(media_session_ready_ms - elapsed_c0_c5) < 0.001);
}
