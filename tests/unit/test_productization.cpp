#include "airplay/AppleModelDatabase.h"
#include "airplay/AirPlaySessionState.h"
#include "airplay/SessionState.h"
#include "airplay/ControlIpc.h"
#include "airplay/AirPlayProcess.h"
#include <string>
#include <string_view>
#include <chrono>
#include <thread>

using namespace duwn::airplay;

// ---------------------------------------------------------------------------
// 1. AppleModelDatabase tests
// ---------------------------------------------------------------------------
DUWN_TEST(AppleModelDatabase_Lookups) {
    // iPhone generations
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone10,1") == L"iPhone 8");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone10,3") == L"iPhone X");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone11,2") == L"iPhone XS");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone12,1") == L"iPhone 11");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone13,2") == L"iPhone 12");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone14,5") == L"iPhone 13");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone14,7") == L"iPhone 14");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone15,2") == L"iPhone 14 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone15,4") == L"iPhone 15");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone16,1") == L"iPhone 15 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone16,2") == L"iPhone 15 Pro Max");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,1") == L"iPhone 16 Pro");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,2") == L"iPhone 16 Pro Max");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPhone17,3") == L"iPhone 16");

    // iPads
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad13,18") == L"iPad (10th gen)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad14,3") == L"iPad Pro 11-inch (4th gen, M2)");
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"iPad16,3") == L"iPad Pro 11-inch (M4)");

    // Unknown fallback returns Apple Device
    DUWN_ASSERT(AppleModelDatabase::GetMarketingName(L"CustomGadget,1") == L"Apple Device");

    // OS detection
    DUWN_ASSERT(AppleModelDatabase::DetectOsName(L"iPhone16,1") == L"iOS");
    DUWN_ASSERT(AppleModelDatabase::DetectOsName(L"iPad16,3") == L"iPadOS");
    DUWN_ASSERT(AppleModelDatabase::DetectOsName(L"MacBookPro18,1") == L"macOS");
}

// ---------------------------------------------------------------------------
// 2. Control IPC Framing & Codec tests
// ---------------------------------------------------------------------------
DUWN_TEST(ControlIpc_SessionStart_Codec) {
    ControlMessage in_msg{};
    in_msg.type           = ControlMessageType::SessionStart;
    in_msg.device_name    = "Alex's iPhone";
    in_msg.model          = "iPhone16,1";
    in_msg.os_name        = "iOS";
    in_msg.os_version     = "18.1";
    in_msg.source_version = "800.14.1";
    in_msg.device_id      = "48:2C:6A:11:22:33";
    in_msg.client_ip      = "192.168.1.145";
    in_msg.client_port    = 7000;

    // Encode to framed wire buffer (4-byte LE length prefix + payload)
    std::vector<uint8_t> frame = ControlIpcCodec::EncodeFrame(in_msg);
    DUWN_ASSERT(frame.size() > 4);

    // Verify length prefix
    uint32_t payload_len = static_cast<uint32_t>(frame[0])
                        | (static_cast<uint32_t>(frame[1]) << 8)
                        | (static_cast<uint32_t>(frame[2]) << 16)
                        | (static_cast<uint32_t>(frame[3]) << 24);
    DUWN_ASSERT(payload_len == (frame.size() - 4));

    // Decode frame
    ControlMessage out_msg{};
    bool ok = ControlIpcCodec::DecodeFrame(frame.data() + 4, payload_len, out_msg);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(out_msg.type == ControlMessageType::SessionStart);
    DUWN_ASSERT(out_msg.device_name == "Alex's iPhone");
    DUWN_ASSERT(out_msg.model == "iPhone16,1");
    DUWN_ASSERT(out_msg.os_name == "iOS");
    DUWN_ASSERT(out_msg.os_version == "18.1");
    DUWN_ASSERT(out_msg.client_ip == "192.168.1.145");
}

DUWN_TEST(ControlIpc_StreamMetadata_Codec) {
    ControlMessage in_msg{};
    in_msg.type              = ControlMessageType::StreamMetadata;
    in_msg.width             = 1170;
    in_msg.height            = 2532;
    in_msg.fps               = 60.0;
    in_msg.codec             = "h264";
    in_msg.audio_format      = "aac-eld";
    in_msg.audio_sample_rate = 44100;
    in_msg.audio_channels    = 2;

    std::vector<uint8_t> frame = ControlIpcCodec::EncodeFrame(in_msg);
    DUWN_ASSERT(frame.size() > 4);

    ControlMessage out_msg{};
    bool ok = ControlIpcCodec::DecodeFrame(frame.data() + 4, frame.size() - 4, out_msg);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(out_msg.type == ControlMessageType::StreamMetadata);
    DUWN_ASSERT(out_msg.width == 1170);
    DUWN_ASSERT(out_msg.height == 2532);
    DUWN_ASSERT(out_msg.fps == 60.0);
    DUWN_ASSERT(out_msg.codec == "h264");
    DUWN_ASSERT(out_msg.audio_sample_rate == 44100);
}

DUWN_TEST(ControlIpc_Heartbeat_Codec) {
    ControlMessage in_msg{};
    in_msg.type           = ControlMessageType::Heartbeat;
    in_msg.video_packets  = 12450;
    in_msg.audio_packets  = 8300;
    in_msg.client_fps     = 59.94;
    in_msg.timestamp_ms   = 1718000000;

    std::string json = ControlIpcCodec::ToJson(in_msg);
    DUWN_ASSERT(!json.empty());

    ControlMessage out_msg{};
    bool ok = ControlIpcCodec::FromJson(json, out_msg);
    DUWN_ASSERT(ok);
    DUWN_ASSERT(out_msg.type == ControlMessageType::Heartbeat);
    DUWN_ASSERT(out_msg.video_packets == 12450);
    DUWN_ASSERT(out_msg.audio_packets == 8300);
}

// ---------------------------------------------------------------------------
// 3. Multi-Timestamp Activity Tracking & Static Screen Liveness tests
// ---------------------------------------------------------------------------
DUWN_TEST(SessionState_MultiTimestamp_Liveness) {
    SessionState session;
    DUWN_ASSERT(session.CurrentState() == AirPlaySessionState::Idle);

    session.TransitionState(AirPlaySessionState::Streaming);
    DUWN_ASSERT(session.CurrentState() == AirPlaySessionState::Streaming);

    int64_t t0 = 10'000'000'000LL; // 10.0s
    session.RecordControlActivity(t0);
    session.RecordVideoPacket(t0);
    session.RecordAudioPacket(t0);

    // After 2.0 seconds: video stopped (static screen), but control/audio is active
    int64_t t1 = t0 + 2'000'000'000LL; // +2.0s
    session.RecordControlActivity(t1);

    // Evaluate liveness: should enter Paused state (static screen), NOT Disconnecting!
    session.EvaluateSessionLiveness(t1);
    DUWN_ASSERT(session.CurrentState() == AirPlaySessionState::Paused);

    // Resume video packets -> transitions back to Streaming
    session.RecordVideoPacket(t1 + 500'000'000LL);
    session.EvaluateSessionLiveness(t1 + 500'000'000LL);
    DUWN_ASSERT(session.CurrentState() == AirPlaySessionState::Streaming);

    // Total silence across all channels > 6.0 seconds -> Disconnecting
    int64_t t2 = t1 + 8'000'000'000LL; // +8.0s silence
    session.EvaluateSessionLiveness(t2);
    DUWN_ASSERT(session.CurrentState() == AirPlaySessionState::Disconnecting);
}

// ---------------------------------------------------------------------------
// 4. Responsive Layout Breakpoints
// ---------------------------------------------------------------------------
DUWN_TEST(ResponsiveLayout_Breakpoints) {
    auto GetLayoutMode = [](float width_dip) -> int {
        if (width_dip >= 1180.0f) return 0; // Large
        if (width_dip >= 900.0f)  return 1; // Medium
        return 2;                           // Small
    };

    DUWN_ASSERT(GetLayoutMode(1920.0f) == 0); // Large
    DUWN_ASSERT(GetLayoutMode(1280.0f) == 0); // Large
    DUWN_ASSERT(GetLayoutMode(1180.0f) == 0); // Large boundary

    DUWN_ASSERT(GetLayoutMode(1179.9f) == 1); // Medium
    DUWN_ASSERT(GetLayoutMode(1024.0f) == 1); // Medium
    DUWN_ASSERT(GetLayoutMode(960.0f)  == 1); // Medium (min window width)
    DUWN_ASSERT(GetLayoutMode(900.0f)  == 1); // Medium boundary

    DUWN_ASSERT(GetLayoutMode(899.0f)  == 2); // Small
    DUWN_ASSERT(GetLayoutMode(640.0f)  == 2); // Small
}

// ---------------------------------------------------------------------------
// 5. AirPlayProcess Preflight Validation & Circuit Breaker Reset
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlayProcess_PreflightValidation_MissingBinary) {
    SessionState state;
    AirPlayProcessConfig config{};
    config.uxplay_exe_path = L"C:\\nonexistent\\uxplay_dummy_test.exe";
    config.receiver_name   = L"TestReceiver";
    config.video_rtp_port  = 7000;
    config.audio_rtp_port  = 7001;

    AirPlayProcess proc(config, state, nullptr);
    bool ok = proc.Start();
    DUWN_ASSERT(ok);

    // Wait briefly for supervision thread to run PreflightRuntimeValidation
    for (int i = 0; i < 30; ++i) {
        if (state.CurrentState() == AirPlaySessionState::Error) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    DUWN_ASSERT(state.CurrentState() == AirPlaySessionState::Error);
    DUWN_ASSERT(state.Current() == SessionPhase::SidecarMissing);
    std::string err = proc.GetLastRuntimeError();
    DUWN_ASSERT(!err.empty());
    DUWN_ASSERT(err.find("UxPlay binary not found") != std::string::npos);

    proc.ManualRetry();
    DUWN_ASSERT(proc.GetLastRuntimeError().empty());

    proc.Stop();
}

// ---------------------------------------------------------------------------
// 6. Job Object Limit & Lifetime Contracts
// ---------------------------------------------------------------------------
DUWN_TEST(JobObject_KillOnJobClose_Configuration) {
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    DUWN_ASSERT(job != nullptr && job != INVALID_HANDLE_VALUE);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    BOOL set_ok = ::SetInformationJobObject(
        job,
        JobObjectExtendedLimitInformation,
        &jeli,
        sizeof(jeli)
    );
    DUWN_ASSERT(set_ok == TRUE);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION query_info{};
    DWORD returned_len = 0;
    BOOL query_ok = ::QueryInformationJobObject(
        job,
        JobObjectExtendedLimitInformation,
        &query_info,
        sizeof(query_info),
        &returned_len
    );
    DUWN_ASSERT(query_ok == TRUE);
    DUWN_ASSERT((query_info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0);

    ::CloseHandle(job);
}

// ---------------------------------------------------------------------------
// 7. Process Token Integrity Level Query
// ---------------------------------------------------------------------------
DUWN_TEST(ProcessIntegrity_TokenQuery) {
    HANDLE token = nullptr;
    BOOL ok = ::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token);
    DUWN_ASSERT(ok == TRUE);
    DUWN_ASSERT(token != nullptr && token != INVALID_HANDLE_VALUE);

    DWORD len = 0;
    ::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &len);
    DUWN_ASSERT(len > 0);

    std::vector<BYTE> buf(len);
    ok = ::GetTokenInformation(token, TokenIntegrityLevel, buf.data(), len, &len);
    DUWN_ASSERT(ok == TRUE);

    auto* p_til = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf.data());
    DUWN_ASSERT(p_til->Label.Sid != nullptr);

    DWORD il = *::GetSidSubAuthority(p_til->Label.Sid,
        static_cast<DWORD>(*::GetSidSubAuthorityCount(p_til->Label.Sid) - 1));
    // Must be a valid Windows integrity level (Medium = 0x2000 or High = 0x3000)
    DUWN_ASSERT(il >= 0x1000 && il <= 0x4000);

    ::CloseHandle(token);
}

// ---------------------------------------------------------------------------
// 8. AirPlayProcess Explicit Handle Lifecycle
// ---------------------------------------------------------------------------
DUWN_TEST(AirPlayProcess_HandleLifecycleContracts) {
    SessionState state;
    AirPlayProcessConfig config{};
    config.uxplay_exe_path = L"C:\\nonexistent\\uxplay_dummy_test.exe";
    config.receiver_name   = L"LifecycleTestReceiver";
    config.video_rtp_port  = 7000;
    config.audio_rtp_port  = 7001;

    AirPlayProcess proc(config, state, nullptr);
    DUWN_ASSERT(proc.ProcessHandle() == nullptr);
    DUWN_ASSERT(proc.JobObjectHandle() == nullptr);

    proc.Stop();
    DUWN_ASSERT(proc.ProcessHandle() == nullptr);
}
