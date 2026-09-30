#include "wired/WiredDeviceManager.h"
#include "wired/WiredControlClient.h"

DUWN_TEST(wired_readiness_waits_for_usb_network) {
    using duwn::wired::ClassifyWiredPhase;
    using duwn::wired::WiredPhase;
    DUWN_ASSERT(ClassifyWiredPhase(0, true, true) == WiredPhase::UsbNotConnected);
    DUWN_ASSERT(ClassifyWiredPhase(1, false, false) == WiredPhase::AppleRuntimeMissing);
    DUWN_ASSERT(ClassifyWiredPhase(1, true, false) == WiredPhase::AppleServiceStopped);
    DUWN_ASSERT(ClassifyWiredPhase(1, true, true) == WiredPhase::WaitingForUsbNetwork);
}

DUWN_TEST(wired_screen_to_hid_coordinates_rejects_letterbox) {
    D2D1_RECT_F video_rect{100.0f, 50.0f, 500.0f, 850.0f};
    uint16_t x{0}, y{0};

    // Outside bounds (letterbox/pillarbox)
    DUWN_ASSERT(!duwn::wired::WiredControlClient::ScreenToHidCoordinates(50.0f, 200.0f, video_rect, 0, x, y));
    DUWN_ASSERT(!duwn::wired::WiredControlClient::ScreenToHidCoordinates(550.0f, 200.0f, video_rect, 0, x, y));
    DUWN_ASSERT(!duwn::wired::WiredControlClient::ScreenToHidCoordinates(200.0f, 20.0f, video_rect, 0, x, y));
    DUWN_ASSERT(!duwn::wired::WiredControlClient::ScreenToHidCoordinates(200.0f, 900.0f, video_rect, 0, x, y));
}

DUWN_TEST(wired_screen_to_hid_coordinates_portrait_mapping) {
    D2D1_RECT_F video_rect{100.0f, 100.0f, 500.0f, 900.0f};
    uint16_t x{0}, y{0};

    // Top-left
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(100.0f, 100.0f, video_rect, 0, x, y));
    DUWN_ASSERT(x == 0 && y == 0);

    // Bottom-right
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(500.0f, 900.0f, video_rect, 0, x, y));
    DUWN_ASSERT(x == 65535 && y == 65535);

    // Center
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(300.0f, 500.0f, video_rect, 0, x, y));
    DUWN_ASSERT(x >= 32767 && x <= 32768);
    DUWN_ASSERT(y >= 32767 && y <= 32768);
}

DUWN_TEST(wired_screen_to_hid_coordinates_orientations) {
    D2D1_RECT_F video_rect{0.0f, 0.0f, 1000.0f, 1000.0f};
    uint16_t x{0}, y{0};

    // 90 degrees rotation (Landscape Left)
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(0.0f, 0.0f, video_rect, 90, x, y));
    DUWN_ASSERT(x == 0 && y == 65535);

    // 180 degrees rotation (Portrait Inverted)
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(0.0f, 0.0f, video_rect, 180, x, y));
    DUWN_ASSERT(x == 65535 && y == 65535);

    // 270 degrees rotation (Landscape Right)
    DUWN_ASSERT(duwn::wired::WiredControlClient::ScreenToHidCoordinates(0.0f, 0.0f, video_rect, 270, x, y));
    DUWN_ASSERT(x == 65535 && y == 0);
}

#include "airplay/AirPlayProcess.h"
#include "airplay/SessionState.h"
#include "video/RtpCodecClassifier.h"

DUWN_TEST(wired_h265_probe_command_line_isolation) {
    using namespace duwn::airplay;
    SessionState state;

    // 1. Wireless mode (bind_ipv4 empty): -h265 must NEVER appear by default
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", nullptr);
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L""; // Wireless
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") == std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip") == std::wstring::npos);
    }

    // 2. Wired mode without env var: -h265 MUST appear (promoted to production default)
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L"172.20.10.4";
        cfg.bind_prefix = 28;
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") != std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip 172.20.10.4 -bind-prefix 28 -h265") != std::wstring::npos);
        // Explicit quality flags must remain unchanged
        DUWN_ASSERT(cmd.find(L"-s 1920x1920@60 -fps 60") != std::wstring::npos);
    }

    // 3. Wired mode with DUWN_DEV_WIRED_H265_PROBE=0: -h265 must NOT appear (dev-disable override)
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", L"0");
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L"172.20.10.4";
        cfg.bind_prefix = 28;
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") == std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip 172.20.10.4 -bind-prefix 28") != std::wstring::npos);
    }

    // Clean up env var
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);
}

DUWN_TEST(wireless_dev_h265_probe_isolation) {
    using namespace duwn::airplay;
    SessionState state;

    // Ensure clean initial state
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", nullptr);
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);

    // 1. Production Wireless (no env var): -h265 must NOT appear
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L""; // Wireless
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") == std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip") == std::wstring::npos);
    }

    // 2. Dev Wireless with DUWN_DEV_WIRELESS_H265_PROBE=1: -h265 MUST appear
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", L"1");
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L""; // Wireless
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") != std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip") == std::wstring::npos);
    }

    // 3. Wired mode with DUWN_DEV_WIRED_H265_PROBE=0: -h265 must NOT appear
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", L"0");
    {
        AirPlayProcessConfig cfg{};
        cfg.uxplay_exe_path = L"uxplay.exe";
        cfg.receiver_name = L"Duwn Test";
        cfg.bind_ipv4 = L"172.20.10.4"; // Wired
        cfg.bind_prefix = 28;
        AirPlayProcess proc(cfg, state, nullptr);
        std::wstring cmd = proc.BuildCommandLine();
        DUWN_ASSERT(cmd.find(L"-h265") == std::wstring::npos);
        DUWN_ASSERT(cmd.find(L"-bind-ip 172.20.10.4 -bind-prefix 28") != std::wstring::npos);
    }

    // Clean up env vars
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRELESS_H265_PROBE", nullptr);
    ::SetEnvironmentVariableW(L"DUWN_DEV_WIRED_H265_PROBE", nullptr);
}

DUWN_TEST(rtp_codec_classifier_detection) {
    using namespace duwn::video;

    // Empty and invalid
    {
        uint8_t empty_buf[1]{};
        auto r = ClassifyRtpPayload(std::span<const uint8_t>(empty_buf, 0));
        DUWN_ASSERT(r.codec == DetectedCodec::Unknown);

        uint8_t invalid_forbidden[4] = {0x80, 0x00, 0x00, 0x00};
        r = ClassifyRtpPayload(invalid_forbidden);
        DUWN_ASSERT(r.codec == DetectedCodec::Unknown);
    }

    // H.264 NAL types: SPS (0x67 = 7), PPS (0x68 = 8), IDR (0x65 = 5), Non-IDR (0x61 = 1), FU-A (0x5C = 28)
    {
        uint8_t sps[4] = {0x67, 0x42, 0x00, 0x1E};
        auto r = ClassifyRtpPayload(sps);
        DUWN_ASSERT(r.codec == DetectedCodec::H264);
        DUWN_ASSERT(std::string_view(r.sender_codec) == "H264");
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (H264 SPS-7)");

        uint8_t pps[4] = {0x68, 0xCE, 0x38, 0x80};
        r = ClassifyRtpPayload(pps);
        DUWN_ASSERT(r.codec == DetectedCodec::H264);

        uint8_t idr[4] = {0x65, 0x88, 0x80, 0x40};
        r = ClassifyRtpPayload(idr);
        DUWN_ASSERT(r.codec == DetectedCodec::H264);

        uint8_t fu_a[4] = {0x5C, 0x85, 0x88, 0x80}; // 0x5C & 0x1F = 28 (FU-A)
        r = ClassifyRtpPayload(fu_a);
        DUWN_ASSERT(r.codec == DetectedCodec::H264);
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (H264 FU-A-28)");
    }

    // H.265 NAL types: VPS (32 << 1 = 0x40), SPS (33 << 1 = 0x42), PPS (34 << 1 = 0x44), FU (49 << 1 = 0x62)
    // with valid layer_id=0, tid_plus1=1: byte 0 has bit 7=0, byte 1 has [0:5]=0 (layer_id), [0:2]=1 (tid) -> byte 1 = 0x01
    {
        uint8_t vps[4] = {0x40, 0x01, 0x0C, 0x01}; // type 32
        auto r = ClassifyRtpPayload(vps);
        DUWN_ASSERT(r.codec == DetectedCodec::H265);
        DUWN_ASSERT(std::string_view(r.sender_codec) == "H265");
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (HEVC VPS-32)");

        uint8_t sps[4] = {0x42, 0x01, 0x01, 0x60}; // type 33
        r = ClassifyRtpPayload(sps);
        DUWN_ASSERT(r.codec == DetectedCodec::H265);
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (HEVC SPS-33)");

        uint8_t pps[4] = {0x44, 0x01, 0xA0, 0x44}; // type 34
        r = ClassifyRtpPayload(pps);
        DUWN_ASSERT(r.codec == DetectedCodec::H265);
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (HEVC PPS-34)");

        uint8_t fu[4] = {0x62, 0x01, 0x81, 0x00}; // type 49 (FU)
        r = ClassifyRtpPayload(fu);
        DUWN_ASSERT(r.codec == DetectedCodec::H265);
        DUWN_ASSERT(std::string_view(r.evidence) == "RtpPayload (HEVC FU-49)");
    }
}
