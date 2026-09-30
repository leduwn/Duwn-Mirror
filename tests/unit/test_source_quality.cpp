// test_source_quality.cpp — Unit tests for Adaptive Source Quality & SourceQualityTracker.

#include "video/SourceQualityTracker.h"
#include <string>

using namespace duwn::video;

DUWN_TEST(SourceQuality_ThreeWayResolutionSeparation) {
    SourceQualityTracker tracker;

    // 1. Requested Receiver Envelope (UxPlay sidecar bounding box)
    RequestedReceiverEnvelope req;
    req.width = 2560;
    req.height = 2560;
    req.fps = 60;
    req.preset_name = "1440p60";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // 2. Actual Source Aperture (Decoded stream)
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 18.5, "H264");
    }

    // 3. Render Target (Output window canvas & preview)
    RenderTargetInfo rt;
    rt.output_width = 3840;
    rt.output_height = 1776;
    rt.preview_width = 720;
    rt.preview_height = 333;
    rt.scaling_policy = "AspectFit";

    const auto cur_req = tracker.GetRequested();
    const auto cur_actual = tracker.GetActual();

    // All geometries must remain strictly distinct
    DUWN_ASSERT(cur_req.width == 2560 && cur_req.height == 2560);
    DUWN_ASSERT(cur_req.LongEdge() == 2560);

    DUWN_ASSERT(cur_actual.coded_width == 1920 && cur_actual.coded_height == 896);
    DUWN_ASSERT(cur_actual.visible_width == 1920 && cur_actual.visible_height == 888);
    DUWN_ASSERT(cur_actual.LongEdge() == 1920);

    DUWN_ASSERT(rt.output_width == 3840 && rt.output_height == 1776);
    DUWN_ASSERT(rt.preview_width == 720 && rt.preview_height == 333);
}

DUWN_TEST(SourceQuality_DeliveredAsRequested_1080p) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 1920;
    req.height = 1920;
    req.fps = 60;
    req.preset_name = "1080p60";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // iPhone XS Landscape 1920x888
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 15.0, "H264");
    }

    DUWN_ASSERT(tracker.IsStable());
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);
    DUWN_ASSERT(std::string(QualityEffectivenessToString(tracker.GetEffectiveness())) == "DELIVERED_AS_REQUESTED");
}

DUWN_TEST(SourceQuality_SourceLimited_2KRequest_1920Actual) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 2560;
    req.height = 2560;
    req.fps = 60;
    req.preset_name = "1440p60";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // iPhone XS hardware sender delivers 1920x888
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 18.0, "H264");
    }

    DUWN_ASSERT(tracker.IsStable());
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::SourceLimited);
    DUWN_ASSERT(std::string(QualityEffectivenessToString(tracker.GetEffectiveness())) == "SOURCE_LIMITED");
}

DUWN_TEST(SourceQuality_SourceLimited_4KRequest_1920Actual) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 3840;
    req.height = 3840;
    req.fps = 60;
    req.preset_name = "4K";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // iPhone XS delivers 1920x888
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 20.0, "H264");
    }

    DUWN_ASSERT(tracker.IsStable());
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::SourceLimited);
    DUWN_ASSERT(std::string(QualityEffectivenessToString(tracker.GetEffectiveness())) == "SOURCE_LIMITED");
}

DUWN_TEST(SourceQuality_PartiallyDelivered_4KRequest_2560Actual) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 3840;
    req.height = 3840;
    req.fps = 60;
    req.preset_name = "4K";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // Device delivers 2560x1440 (exceeds 1080p, but below 4K)
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(2560, 1440, 2560, 1440, 60.0, 25.0, "HEVC");
    }

    DUWN_ASSERT(tracker.IsStable());
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::PartiallyDelivered);
    DUWN_ASSERT(std::string(QualityEffectivenessToString(tracker.GetEffectiveness())) == "PARTIALLY_DELIVERED");
}

DUWN_TEST(SourceQuality_OriginalSemantics_1to1Passthrough) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 2560;
    req.height = 2560;
    req.fps = 60;
    req.preset_name = "Original60";
    req.is_original = true;
    tracker.SetRequestedEnvelope(req);

    // Sender delivers any resolution, e.g. 1920x888
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 12.0, "H264");
    }

    DUWN_ASSERT(tracker.IsStable());
    // In Original mode, 1:1 passthrough is intentional design, so delivered as requested
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);
    DUWN_ASSERT(std::string(QualityEffectivenessToString(tracker.GetEffectiveness())) == "DELIVERED_AS_REQUESTED");
}

DUWN_TEST(SourceQuality_OrientationChange_PreservesCapabilityState) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 1920;
    req.height = 1920;
    req.fps = 60;
    req.preset_name = "1080p60";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // 1. Initial landscape stream (1920x888)
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 15.0, "H264");
    }
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);
    DUWN_ASSERT(tracker.GetObservation().max_observed_long_edge == 1920);

    // 2. Rotate to portrait (500x1080)
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(512, 1088, 500, 1080, 60.0, 10.0, "H264");
    }

    // Must preserve capability: device is 1080p capable, so 1080p request is still DeliveredAsRequested
    DUWN_ASSERT(tracker.GetObservation().max_observed_landscape_long_edge == 1920);
    DUWN_ASSERT(tracker.GetObservation().max_observed_portrait_long_edge == 1080);
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);
}

DUWN_TEST(SourceQuality_TransientAperture_Rejection) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 1920;
    req.height = 1920;
    req.fps = 60;
    req.preset_name = "1080p60";
    req.is_original = false;
    tracker.SetRequestedEnvelope(req);

    // 1. Establish stable baseline at 1920x888 (65 frames)
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 15.0, "H264");
    }
    DUWN_ASSERT(tracker.IsStable());
    DUWN_ASSERT(tracker.GetActual().visible_width == 1920);

    // 2. Sender introduces a 5-frame transient glitch / orientation flip
    for (int i = 0; i < 5; ++i) {
        tracker.OnFrame(1280, 720, 1280, 720, 30.0, 8.0, "H264");
    }

    // Tracker must reject transient change because stable frame threshold (60) is not met
    DUWN_ASSERT(tracker.GetActual().visible_width == 1920);
    DUWN_ASSERT(tracker.GetActual().visible_height == 888);

    // 3. Glitch ends and stream returns to 1920x888
    for (int i = 0; i < 10; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 15.0, "H264");
    }
    DUWN_ASSERT(tracker.GetActual().visible_width == 1920);
}

DUWN_TEST(SourceQuality_HypotheticalHighResDevice_NotCappedByIPhoneXS) {
    SourceQualityTracker tracker;
    tracker.SetClientInfo(L"iPad13,1", L"iPad Air", L"AirPlay/860", L"Wireless");

    // 1. Request 2K (2560x2560)
    RequestedReceiverEnvelope req2k;
    req2k.width = 2560;
    req2k.height = 2560;
    req2k.fps = 60;
    req2k.preset_name = "1440p60";
    tracker.SetRequestedEnvelope(req2k);

    // Hypothetical device actually transmits a true 2560 stream
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(2560, 1440, 2560, 1440, 60.0, 22.0, "HEVC");
    }

    // Must NOT be clamped to 1080p!
    DUWN_ASSERT(tracker.GetActual().LongEdge() == 2560);
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);

    // 2. Request 4K (3840x3840) with a hypothetical 4K stream
    RequestedReceiverEnvelope req4k;
    req4k.width = 3840;
    req4k.height = 3840;
    req4k.fps = 60;
    req4k.preset_name = "4K";
    tracker.SetRequestedEnvelope(req4k);
    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(3840, 2160, 3840, 2160, 60.0, 35.0, "HEVC");
    }

    DUWN_ASSERT(tracker.GetActual().LongEdge() == 3840);
    DUWN_ASSERT(tracker.GetEffectiveness() == QualityEffectiveness::DeliveredAsRequested);
}

DUWN_TEST(SourceQuality_FormatTelemetryOutput) {
    SourceQualityTracker tracker;
    RequestedReceiverEnvelope req;
    req.width = 2560;
    req.height = 2560;
    req.fps = 60;
    req.preset_name = "1440p60";
    tracker.SetRequestedEnvelope(req);
    tracker.SetClientInfo(L"iPhone11,2", L"iPhone XS", L"AirPlay/860.7.1", L"Wireless");

    for (int i = 0; i < 65; ++i) {
        tracker.OnFrame(1920, 896, 1920, 888, 60.0, 16.5, "H264");
    }

    std::string telemetry = tracker.FormatTelemetryBlock();
    DUWN_ASSERT(telemetry.find("[SOURCE QUALITY]") != std::string::npos);
    DUWN_ASSERT(telemetry.find("preset=1440p60") != std::string::npos);
    DUWN_ASSERT(telemetry.find("requested_envelope=2560x2560") != std::string::npos);
    DUWN_ASSERT(telemetry.find("coded=1920x896") != std::string::npos);
    DUWN_ASSERT(telemetry.find("visible=1920x888") != std::string::npos);
    DUWN_ASSERT(telemetry.find("long_edge=1920") != std::string::npos);
    DUWN_ASSERT(telemetry.find("quality_state=SOURCE_LIMITED") != std::string::npos);
}
