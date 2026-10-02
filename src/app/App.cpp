#include "App.h"

#include "CrashHandler.h"

#include "DiagnosticsCollector.h"

#include "common/Version.h"

#include "common/logging/Logger.h"

#include "common/clock/MonotonicClock.h"

#include "common/metrics/Metrics.h"

#include "common/threading/ThreadUtil.h"

#include "common/system/CpuCapabilities.h"

#include "common/system/ClipboardUtil.h"

#include "common/telemetry/LatencyTelemetry.h"

#include "common/telemetry/ConnectionTimeline.h"

#include "network/NetworkEnvironment.h"

#include "sync/MasterClock.h"

#include "ui/Loc.h"

#include "video/RtpCodecClassifier.h"

#include <mfapi.h>

#include <objbase.h>

#include <format>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <thread>

#include <chrono>

#include <filesystem>

#include <iterator>

#include <cwchar>

#include <cmath>

#include <shellapi.h>



#pragma comment(lib, "mf.lib")

#pragma comment(lib, "mfplat.lib")

#pragma comment(lib, "ole32.lib")

#pragma comment(lib, "shell32.lib")



namespace duwn::app {



namespace {

struct CustomResolutionInput {

    uint32_t width{};

    uint32_t height{};

    bool accepted{false};

};



LRESULT CALLBACK CustomResolutionProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {

    if (message == WM_NCCREATE) {

        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);

        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));

    }

    auto* input = reinterpret_cast<CustomResolutionInput*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_COMMAND && LOWORD(wp) == IDOK && input) {

        wchar_t width_text[16]{}, height_text[16]{};

        ::GetDlgItemTextW(hwnd, 101, width_text, 16);

        ::GetDlgItemTextW(hwnd, 102, height_text, 16);

        wchar_t* width_end = nullptr;

        wchar_t* height_end = nullptr;

        const unsigned long width = std::wcstoul(width_text, &width_end, 10);

        const unsigned long height = std::wcstoul(height_text, &height_end, 10);

        if (!width_text[0] || !height_text[0] || *width_end || *height_end ||

            width < 320 || width > 3840 || height < 180 || height > 2160) {

            ::MessageBoxW(hwnd, L"Enter a width from 320 to 3840 and a height from 180 to 2160.",

                          L"Custom Output Resolution", MB_OK | MB_ICONINFORMATION);

            return 0;

        }

        input->width = static_cast<uint32_t>(width);

        input->height = static_cast<uint32_t>(height);

        input->accepted = true;

        ::DestroyWindow(hwnd);

        return 0;

    }

    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {

        ::DestroyWindow(hwnd);

        return 0;

    }

    return ::DefWindowProcW(hwnd, message, wp, lp);

}



bool PromptCustomResolution(HWND owner, uint32_t& width, uint32_t& height) noexcept {

    constexpr wchar_t class_name[] = L"DUWN Custom Resolution";

    WNDCLASSW wc{};

    wc.lpfnWndProc = CustomResolutionProc;

    wc.hInstance = ::GetModuleHandleW(nullptr);

    wc.lpszClassName = class_name;

    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    ::RegisterClassW(&wc);

    RECT owner_rect{};

    ::GetWindowRect(owner, &owner_rect);

    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - 300) / 2;

    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - 150) / 2;

    CustomResolutionInput input{};

    HWND dialog = ::CreateWindowExW(WS_EX_DLGMODALFRAME, class_name, L"Custom Output Resolution",

        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 300, 150,

        owner, nullptr, wc.hInstance, &input);

    if (!dialog) return false;

    auto add_control = [&](const wchar_t* kind, const wchar_t* title, DWORD style,

                           int left, int top, int w, int h, int id) {

        return ::CreateWindowExW(0, kind, title, WS_CHILD | WS_VISIBLE | style,

            left, top, w, h, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);

    };

    add_control(L"STATIC", L"Width × Height", 0, 18, 17, 250, 20, 0);

    HWND width_edit = add_control(L"EDIT", std::format(L"{}", width).c_str(),

        WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 18, 44, 95, 25, 101);

    add_control(L"STATIC", L"×", 0, 125, 47, 20, 20, 0);

    add_control(L"EDIT", std::format(L"{}", height).c_str(),

        WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 154, 44, 95, 25, 102);

    add_control(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, 92, 83, 86, 27, IDOK);

    add_control(L"BUTTON", L"Cancel", WS_TABSTOP, 188, 83, 86, 27, IDCANCEL);

    ::EnableWindow(owner, FALSE);

    ::ShowWindow(dialog, SW_SHOW);

    ::SetFocus(width_edit);

    MSG msg{};

    while (::IsWindow(dialog)) {

        const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);

        if (result <= 0) {

            if (result == 0) ::PostQuitMessage(static_cast<int>(msg.wParam));

            break;

        }

        if (!::IsDialogMessageW(dialog, &msg)) {

            ::TranslateMessage(&msg);

            ::DispatchMessageW(&msg);

        }

    }

    if (::IsWindow(dialog)) ::DestroyWindow(dialog);

    ::EnableWindow(owner, TRUE);

    ::SetForegroundWindow(owner);

    if (input.accepted) { width = input.width; height = input.height; }

    return input.accepted;

}

} // namespace



App::~App() {

    Shutdown();

}



int App::Run(bool test_motion, bool verify_capture) noexcept {

    sync::MasterClock::Initialize();

    Logger::Initialize();



    DUWN_LOG_INFO("App", "Duwn Mirror starting");

    common::CpuCapabilities::Get().LogCapabilities();

    m_net_env = network::NetworkEnvironmentInfo::Probe();

    m_verify_capture = verify_capture;

    duwn::telemetry::ConnectionTimeline::Get().Record(
        duwn::telemetry::ConnectionMilestone::C1_NetworkDiscoveryComplete,
        std::format("best_ip={}, adapters={}", m_net_env.best_adapter_ip, m_net_env.adapters.size()));

    m_net_env.LogEnvironment();



    m_settings = Settings::Load();
    m_meta_coord.Initialize(m_settings);

    Logger::SetLevel(m_settings.debug_log ? LogLevel::Debug : LogLevel::Info);

    if (!m_settings.remember_selected_mode)

        m_settings.connection_mode = m_settings.default_connection_mode;

    m_connection_mode.store(m_settings.connection_mode, std::memory_order_release);

    m_match_source.store(m_settings.match_source, std::memory_order_relaxed);



    // Firewall state reconciliation: verify real OS firewall state against settings.json

    const bool public_rules_valid = network::VerifyPublicFirewallRulesExist();

    const bool any_public_rules = network::ArePublicFirewallRulesPresent();



    auto rec = network::ReconcileFirewallState(m_settings.allow_public_networks, public_rules_valid, any_public_rules);

    if (rec.needs_save) {

        DUWN_LOG_WARN("App", "Firewall Reconciliation: settings.json had allow_public_networks=true, "

                              "but valid Public firewall rules are missing in OS. Reconciling setting to false.");

        m_settings.allow_public_networks = rec.setting_value;

        m_settings.Save();

    } else if (rec.rules_unexpected_warning) {

        DUWN_LOG_WARN("App", "Firewall Reconciliation: settings.json had allow_public_networks=false, "

                              "but Public firewall rules are active in Windows Defender Firewall. Reconciling to true.");

        m_settings.allow_public_networks = rec.setting_value;

    }

    m_startup_public_rules_missing = rec.rules_missing_warning;

    m_startup_public_rules_unexpected = rec.rules_unexpected_warning;

    const bool domain_private_valid = network::VerifyDomainPrivateFirewallRulesExist();
    auto fw_state = network::EvaluateFirewallValidationState(
        m_net_env.primary_profile,
        m_settings.allow_public_networks,
        public_rules_valid,
        domain_private_valid,
        false,
        false
    );
    m_firewall_validation_state.store(fw_state, std::memory_order_release);



    if (!Init()) {

        DUWN_LOG_ERROR("App", "Init failed — exiting");

        Logger::Shutdown();

        return 1;

    }

    if (test_motion) {
        DUWN_LOG_INFO("App", "Starting synthetic test motion source for pipeline verification");
        m_test_motion_thread = std::jthread([this](std::stop_token st) { TestMotionLoop(std::move(st)); });
    }



    // Single message loop — processes all thread messages (both windows' WndProcs

    // are dispatched here). WM_QUIT posted to the thread queue ends the loop.

    // Separate PumpMessages() on each window was a bug: whichever ran second

    // could swallow messages from the other window's queue.

    bool running = true;

    MSG msg{};

    while (running && m_running.load(std::memory_order_acquire)) {

        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {

            if (msg.message == WM_QUIT) { running = false; break; }

            // WM_DUWN_FATAL posted from render/audio threads — must handle on main thread.

            if (msg.message == WM_DUWN_FATAL) {

                HWND main_hwnd = m_main_hwnd.load(std::memory_order_acquire);

                if (msg.hwnd == nullptr || msg.hwnd == main_hwnd) {

                    DUWN_LOG_ERRORF("App",

                        "Fatal error signalled from background thread (wParam={}, lParam={}, thread_id={})",

                        static_cast<unsigned long long>(msg.wParam),

                        static_cast<unsigned long long>(msg.lParam),

                        ::GetCurrentThreadId());

                    running = false;

                    ::PostQuitMessage(static_cast<int>(msg.wParam));

                    break;

                }

            }

            if (msg.message == WM_DUWN_RESTART_AIRPLAY) {

                RestartAirPlaySidecar();

                continue;

            }

            if (msg.message == WM_DUWN_WIRED_REFRESH) {

                UpdateWiredConnection();

                continue;

            }

            if (msg.message == WM_DUWN_SESSION_PHASE) {

                ProcessPendingSessionEvents();

                continue;

            }

            if (msg.message == WM_DUWN_FIRST_FRAME) {

                if (m_window) {
                    m_window->SetStatusText(ui::loc::Get(ui::loc::S::Status_Streaming));
                }

                uint32_t fw = static_cast<uint32_t>(msg.wParam);

                uint32_t fh = static_cast<uint32_t>(msg.lParam);

                if (m_output_window && m_settings.auto_open_output_window) {

                    m_output_window->ShowNoActivate();

                    if (m_window) {

                        m_window->SetOutputControlsState(true, m_output_window->IsFullscreen(),

                            m_output_window->IsAspectLocked(), m_output_window->IsAlwaysOnTop());

                    }

                }

                if (m_preview_window) {
                    m_preview_window->SetVideoGeometry(fw, fh);
                }
                if (m_window) {
                    m_window->LayoutVideoSurface();
                    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
                }
                PublishMetadataSnapshot();

                continue;

            }

            if (msg.message == WM_DUWN_NETWORK_CHANGED) {

                OnNetworkEnvironmentChanged(m_net_monitor ? m_net_monitor->CurrentEnvironment() : network::NetworkEnvironmentInfo::Probe());

                continue;

            }

            // Audio device change: hot-plug or default-device switch

            if (msg.message == audio::WM_APP_AUDIO_DEVICE_CHANGED) {
                if (m_wasapi) {
                    m_wasapi->OnDeviceEnvironmentChanged();
                    if (m_window) {
                        m_window->State().audio_device_id = m_wasapi->CurrentDeviceId();
                        m_window->State().resolved_audio_device_name = m_wasapi->ResolvedDeviceName();
                        m_window->State().audio_fallback_active = m_wasapi->IsFallbackActive();
                        if (m_wasapi->IsFallbackActive()) {
                            m_window->State().audio_device_name = std::format(L"{} (Tạm thời)", m_wasapi->ResolvedDeviceName());
                        } else {
                            m_window->State().audio_device_name = m_wasapi->ResolvedDeviceName();
                        }
                        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
                    }
                }
                continue;
            }

            if (msg.message == audio::WM_APP_AUDIO_DEVICE_LIST_CHANGED) {
                if (m_window) {
                    auto devs = m_audio_device_mgr.Enumerate();
                    auto& list = m_window->State().available_audio_devices;
                    list.clear();
                    for (const auto& d : devs) {
                        list.push_back({d.id, d.friendly_name});
                        if (d.id == m_settings.monitor_device_id) {
                            m_window->State().audio_device_name = d.friendly_name;
                        }
                    }
                    if (m_settings.monitor_device_id.empty()) {
                        m_window->State().audio_device_name = L"System Default";
                    }
                    if (m_wasapi) {
                        m_window->State().resolved_audio_device_name = m_wasapi->ResolvedDeviceName();
                        m_window->State().audio_fallback_active = m_wasapi->IsFallbackActive();
                    }
                    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
                }
                continue;
            }

            ::TranslateMessage(&msg);

            ::DispatchMessageW(&msg);

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    }



    Shutdown();

    Logger::Flush();

    Logger::Shutdown();

    return 0;

}



bool App::Init() noexcept {

    // COM must be initialised on the main thread

    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);



    m_running.store(true, std::memory_order_release);

    // Window
    m_window = std::make_unique<MainWindow>([this]{
        m_running.store(false, std::memory_order_release);
    });
    if (!m_window->Create(m_settings)) return false;
    m_main_hwnd.store(m_window->Hwnd(), std::memory_order_release);
    m_window->SetStatusText(L"Starting…");



    // Crash recovery & First-run detection

    std::wstring last_crash_file;

    if (CrashHandler::HasPreviousCrash(&last_crash_file) || m_settings.unclean_shutdown) {

        m_window->State().show_crash_banner = true;

        m_window->State().crash_banner_file = last_crash_file;

    }



    if (!m_settings.first_run_completed) {

        m_window->State().is_first_run = true;

    }



    // Mark unclean_shutdown during active session

    m_settings.unclean_shutdown = true;

    m_settings.Save();



    // Connect UI Action Callbacks

    m_window->SetOnToggleOutputWindow([this] {

        if (m_output_window) {

            bool vis = ::IsWindowVisible(m_output_window->Hwnd());

            if (vis) m_output_window->Hide();

            else m_output_window->Show();

            const bool now_vis = !vis;

            m_window->SetOutputControlsState(now_vis, m_output_window->IsFullscreen(),

                                             m_output_window->IsAspectLocked(),

                                             m_output_window->IsAlwaysOnTop());

            m_window->State().output_window_visible = now_vis;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

    });

    m_window->SetOnToggleFullscreen([this] {

        if (m_output_window) {

            m_output_window->ToggleFullscreen();

            m_window->SetOutputControlsState(::IsWindowVisible(m_output_window->Hwnd()),

                                             m_output_window->IsFullscreen(),

                                             m_output_window->IsAspectLocked(),

                                             m_output_window->IsAlwaysOnTop());

        }

    });

    m_window->SetOnToggleAspectLock([this] {

        if (m_output_window) {

            m_output_window->ToggleAspectLock();

            m_window->SetOutputControlsState(::IsWindowVisible(m_output_window->Hwnd()),

                                             m_output_window->IsFullscreen(),

                                             m_output_window->IsAspectLocked(),

                                             m_output_window->IsAlwaysOnTop());

        }

    });

    m_window->SetOnToggleAlwaysOnTop([this] {

        if (m_output_window) {

            m_output_window->ToggleAlwaysOnTop();

            m_window->SetOutputControlsState(::IsWindowVisible(m_output_window->Hwnd()),

                                             m_output_window->IsFullscreen(),

                                             m_output_window->IsAspectLocked(),

                                             m_output_window->IsAlwaysOnTop());

        }

    });

    m_window->SetOnTogglePreview([this] {

        if (m_preview_window) {

            m_preview_window->ToggleVisibility();

            const bool prev_vis = m_preview_window->IsVisible();

            m_window->State().preview_visible = prev_vis;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

            PublishMetadataSnapshot();

        }

    });

    m_window->SetOnFullscreenPreview([this] {

        if (m_preview_window) {

            m_preview_window->ToggleFullscreen();

            if (m_preview_renderer) {

                m_preview_renderer->LogSwapChainConfig(

                    m_preview_window->IsFullscreen() ? "PreviewWindow (Fullscreen)" : "PreviewWindow (Windowed)");

            }

        }

    });

    m_window->SetOnTogglePreviewAlwaysOnTop([this] {

        if (m_preview_window) {

            bool next_top = !m_preview_window->IsAlwaysOnTop();

            m_preview_window->SetAlwaysOnTop(next_top);

            m_settings.preview_always_on_top = next_top;

            m_window->State().preview_always_on_top = next_top;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

    });

    m_window->SetOnToggleMute([this] {
        if (m_wasapi) {
            bool muted = !m_wasapi->IsMuted();
            m_wasapi->SetMuted(muted);
            m_settings.audio_muted = muted;
            m_settings.Save();
            if (m_window) {
                m_window->State().audio_muted = muted;
                ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
            }
        }
    });

    m_window->SetOnVolumeChanged([this](float vol) {
        if (m_wasapi) {
            m_wasapi->SetVolume(vol);
        }
        m_settings.monitor_volume = vol;
        m_settings.Save();
        if (m_window) {
            m_window->State().audio_volume = vol;
            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
        }
    });

    m_window->SetOnToggleScreenOnly([this] {
        if (!m_window) return;
        bool next_screen_only = !m_window->State().is_screen_only;
        m_window->State().is_screen_only = next_screen_only;
        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
    });

    m_window->SetOnDisconnect([this] {

        if (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WirelessAirPlay)

            RestartAirPlaySidecar();

        else RefreshWiredDevices();

    });

    m_window->SetOnFlushPipeline([this] {

        if (m_video_decoder) m_video_decoder->Flush();

        if (m_scheduler)     m_scheduler->Flush();

        if (m_audio_engine)  m_audio_engine->Flush();

    });

    m_window->SetOnSettingChanged([this](int id, int value) { ApplySettingChange(id, value); });



    // Test Audio: play 440Hz sine for 0.5s via AudioRingBuffer

    m_window->SetOnTestAudio([this] {

        if (!m_audio_ring || !m_wasapi) return;

        std::thread([ring = m_audio_ring.get()] {

            constexpr int kRate = 48000;

            constexpr int kCh = 2;

            constexpr float kFreq = 440.0f;

            constexpr int kFrames = kRate / 2; // 0.5s

            std::vector<float> buf(kFrames * kCh);

            for (int i = 0; i < kFrames; ++i) {

                float s = 0.25f * std::sinf(2.0f * 3.14159265f * kFreq * i / kRate);

                buf[i * kCh + 0] = s;

                buf[i * kCh + 1] = s;

            }

            ring->Push(buf.data(), kFrames);

        }).detach();

    });



    // Audio device change: switch WASAPI endpoint live

    m_window->SetOnAudioDeviceChanged([this](std::wstring_view device_id) {

        const std::wstring requested(device_id);

        if (m_wasapi && !m_wasapi->SwitchEndpoint(requested)) {

            if (m_window) m_window->SetStatusText(ui::loc::Get(ui::loc::S::Audio_DeviceUnavailable));

            return;

        }

        m_settings.monitor_device_id = requested;

        m_settings.Save();

        m_audio_device_mgr.SetWatchedDeviceId(m_settings.monitor_device_id);

        // Update UI display name

        if (m_window) {

            if (device_id.empty()) {

                m_window->State().audio_device_name = L"System Default";

            } else {

                auto devs = m_audio_device_mgr.Enumerate();

                for (const auto& d : devs) {

                    if (d.id == device_id) {

                        m_window->State().audio_device_name = d.friendly_name;

                        break;

                    }

                }

            }

            m_window->State().audio_device_id = m_settings.monitor_device_id;

            m_window->State().resolved_audio_device_name = m_wasapi
                ? m_wasapi->ResolvedDeviceName() : L"—";
            m_window->State().audio_fallback_active = m_wasapi ? m_wasapi->IsFallbackActive() : false;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

    });



    // Language change

    m_window->SetOnLanguageChanged([this](std::wstring_view lang) {

        m_settings.language = std::wstring(lang);

        m_settings.Save();

        // Update loc runtime

        using namespace duwn::ui::loc;

        if (lang == L"en-US") SetLang(Lang::En);

        else if (lang == L"vi-VN") SetLang(Lang::Vi);

        else SetLang(Lang::System);

        m_window->State().language = m_settings.language;

        if (m_window) ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

    });



    // Wired USB touch control callbacks

    m_window->SetOnTouchTap([this](uint16_t x, uint16_t y) {

        m_wired_control.SendTap(x, y);

    });

    m_window->SetOnTouchDrag([this](uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {

        m_wired_control.SendDrag(x1, y1, x2, y2);

    });



    SyncUiVideoSettings();



    // Apply saved language on startup

    {

        using namespace duwn::ui::loc;

        if (m_settings.language == L"en-US")      SetLang(Lang::En);

        else if (m_settings.language == L"vi-VN") SetLang(Lang::Vi);

        else                                       SetLang(Lang::System);

    }

    // Early AirPlay sidecar startup: spawn child OS process early so process creation,
    // GStreamer plugin initialization, socket binding, and mDNS advertisement run concurrently
    // on separate CPU cores while the host initializes D3D11, VideoProcessor, swapchains, and WASAPI.
    airplay::AirPlayEngineConfig ap_cfg;
    {
        wchar_t exe_path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
        ap_cfg.uxplay_exe_path = (exe_dir / m_settings.uxplay_exe_path).wstring();
    }
    {
        wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD computer_name_size = static_cast<DWORD>(std::size(computer_name));
        if (::GetComputerNameW(computer_name, &computer_name_size) && computer_name_size > 0) {
            ap_cfg.receiver_name = L"DuwnMirror@" + std::wstring(computer_name, computer_name_size);
        } else {
            ap_cfg.receiver_name = L"DuwnMirror";
        }
    }
    ap_cfg.max_fps               = m_settings.receiver_fps;
    ap_cfg.receiver_width        = m_settings.receiver_width;
    ap_cfg.receiver_height       = m_settings.receiver_height;
    ap_cfg.config_generation     = m_meta_coord.GetConfigGeneration();
    ap_cfg.receiver_quality_name = std::string(GetReceiverQualityName(m_settings.receiver_quality));
    ap_cfg.enable_fps_data       = true;
    ap_cfg.debug_log             = m_settings.debug_log;

    m_airplay = std::make_unique<airplay::AirPlayEngine>(std::move(ap_cfg));
    m_airplay->SetVideoCallback([this](const uint8_t* d, size_t s,
                                        uint32_t ts, int64_t arr,
                                        bool marker, uint16_t seq){
        OnVideoData(d, s, ts, arr, marker, seq);
    });
    m_airplay->SetAudioCallback([this](const uint8_t* d, size_t s,
                                        uint32_t ts, int64_t arr){
        OnAudioData(d, s, ts, arr);
    });
    m_airplay->SetPhaseCallback([this](airplay::SessionPhase prev,
                                        airplay::SessionPhase next){
        OnPhase(prev, next);
    });
    m_airplay->SetStateCallback([this](airplay::AirPlaySessionState /*prev*/,
                                        airplay::AirPlaySessionState next){
        if (m_window) {
            m_window->UpdateSessionState(next);
        }
        if (next == airplay::AirPlaySessionState::Idle || next == airplay::AirPlaySessionState::Disconnecting) {
            m_source_quality_tracker.ResetSession();
            auto& m = GlobalMetrics();
            m.client_fps.store(0.0, std::memory_order_relaxed);
            m.client_dropped_frames.store(0, std::memory_order_relaxed);
            m.client_total_frames.store(0, std::memory_order_relaxed);
            m.video_rtp_packets.store(0, std::memory_order_relaxed);
            m.video_rtp_bytes.store(0, std::memory_order_relaxed);
            m.audio_rtp_packets.store(0, std::memory_order_relaxed);
            m.audio_rtp_bytes.store(0, std::memory_order_relaxed);
            m.video_decoded_frames.store(0, std::memory_order_relaxed);
            m.video_rendered_frames.store(0, std::memory_order_relaxed);
            m.video_dropped_frames.store(0, std::memory_order_relaxed);
            m.session_q_full.store(0, std::memory_order_relaxed);

            if (m_video_decoder) {
                std::lock_guard<std::mutex> lock(m_decoder_mutex);
                m_video_decoder->Flush();
                m_video_decoder->ResetHevcAssembler();
                if (m_video_decoder->Init(1920, 1080, video::VideoCodecType::H264)) {
                    m_decoder_ready.store(true, std::memory_order_release);
                } else {
                    m_decoder_ready.store(false, std::memory_order_release);
                }
            }

            if (m_meta_coord.IsReceiverConfigDirty()) {
                bool expected = false;
                if (m_sidecar_restart_posted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    HWND hwnd = m_main_hwnd.load(std::memory_order_acquire);
                    if (hwnd) {
                        ::PostMessageW(hwnd, WM_DUWN_RESTART_AIRPLAY, 0, 0);
                    } else {
                        m_sidecar_restart_posted.store(false, std::memory_order_release);
                    }
                }
            }
        }
    });
    m_airplay->SetClientInfoCallback([this](const airplay::AirPlayClientInfo& info){
        if (m_window) {
            m_window->UpdateClientInfo(info);
        }
        m_source_quality_tracker.SetClientInfo(
            info.model,
            info.model_marketing_name,
            info.user_agent,
            m_settings.connection_mode == ConnectionMode::WiredUsb ? L"Wired" : L"Wireless");
    });
    m_airplay->SetMetadataCallback([this](const airplay::StreamMetadata& m){
        OnMetadata(m);
    });

    if (m_settings.connection_mode == ConnectionMode::WirelessAirPlay && !m_airplay->Start()) {
        DUWN_LOG_ERROR("App", "AirPlayEngine early start failed");
        return false;
    }

    m_ble_beacon = std::make_unique<network::BleBeaconPublisher>();
    if (m_settings.connection_mode == ConnectionMode::WirelessAirPlay) {
        network::BleBeaconConfig ble_cfg;
        ble_cfg.ipv4_address = m_net_env.best_adapter_ip;
        ble_cfg.airplay_port = 7000;
        ble_cfg.enable_beacon = true;
        m_ble_beacon->Start(ble_cfg);
    }

    // D3D11 device (shared by all GPU subsystems)

    m_d3d = std::make_unique<video::D3D11Device>();

    const bool force_software = m_settings.renderer_mode == RendererMode::Software;

    const bool force_hardware = m_settings.renderer_mode == RendererMode::HardwareD3D11;

    const bool force_compatibility = m_settings.renderer_mode == RendererMode::Compatibility;

    if (!m_d3d->Create(force_software, !force_hardware)) {

        if (force_hardware) m_window->SetStatusText(L"Hardware renderer unavailable. Choose Auto or Compatibility.");

        DUWN_LOG_ERROR("App", "D3D11Device creation failed");

        return false;

    }

    m_active_renderer.store(m_d3d->IsHardware() ? 1 : 2, std::memory_order_relaxed);



    // Video decoder

    m_video_decoder = std::make_unique<video::VideoDecoder>(

        *m_d3d,

        [this](video::VideoFrame frame) {
            // Called on decode thread — push to scheduler
            const char* codec_str = (m_video_decoder && m_video_decoder->GetActiveCodec() == video::VideoCodecType::H265) ? "HEVC" : "H264";
            m_source_quality_tracker.OnFrame(
                frame.width, frame.height,
                frame.visible_width, frame.visible_height,
                60.0, 0.0,
                codec_str);
            m_scheduler->PushFrame(std::move(frame));
        }

    );

    m_video_decoder->SetPreference((force_software || force_compatibility || !m_d3d->IsHardware()) ? video::DecoderPreference::SoftwareOnly

        : force_hardware ? video::DecoderPreference::HardwareOnly

        : video::DecoderPreference::Auto);

    if (m_video_decoder->Init(1920, 1080)) {

        m_decoder_ready.store(true, std::memory_order_release);

        DUWN_LOG_INFO("App", "Video decoder pre-initialized for 1920x1080");

    } else {

        m_decoder_ready.store(false, std::memory_order_release);

        if (force_hardware) {

            m_window->SetStatusText(L"Hardware renderer unavailable. Choose Auto or Compatibility.");

        }

    }



    // Frame scheduler

    video::SchedulerConfig sched_cfg;

    sched_cfg.streaming_policy = ResolveStreamingPolicy(m_settings.streaming_mode,
        m_settings.custom_video_freshness_ms, m_settings.custom_video_queue_frames);

    sched_cfg.frame_duration_ns  = 16'666'667LL; // 60fps (~16.67ms)

    sched_cfg.max_queue_depth    = 2;            // Experimental PresentationClock mode only

    sched_cfg.late_threshold_ns  = 25'000'000LL; // 25ms cap

    sched_cfg.drop_late_frames   = true;



    m_scheduler = std::make_unique<video::FrameScheduler>(

        sched_cfg,

        [this](video::VideoFrame& f) { OnFramePresent(f); }

    );



    // Output Window — borderless, black, no title bar.

    // VideoRenderer attaches here, not to MainWindow.

    // OBS / TikTok Live Studio should capture this window.

    m_output_window = std::make_unique<OutputWindow>();

    const uint32_t kInitW = m_settings.output_width;

    const uint32_t kInitH = m_settings.output_height;

    if (!m_output_window->Create(kInitW, kInitH,

            [this](uint32_t w, uint32_t h) {

                // WM_SIZE callback (UI thread) — signal the render thread to resize.

                // The actual ResizeBuffers happens on the render thread in Present().

                if (m_renderer) m_renderer->SignalResize(w, h);

            })) {

        DUWN_LOG_ERROR("App", "OutputWindow creation failed");

        return false;

    }



    // Video renderer — attached to OutputWindow, not MainWindow

    if (m_d3d->IsHardware())

        m_renderer = std::make_unique<video::VideoRenderer>(*m_d3d, m_output_window->Hwnd());

    else

        m_renderer = std::make_unique<video::WarpVideoRenderer>(*m_d3d, m_output_window->Hwnd());

    if (!m_renderer->Init(kInitW, kInitH)) {

        DUWN_LOG_ERROR("App", "VideoRenderer init failed");

        return false;

    }

    m_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

    m_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));

    m_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));

    m_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));

    const int initial_color[] = {m_settings.brightness, m_settings.contrast, m_settings.saturation,

                                 m_settings.hue, m_settings.sharpness};

    for (size_t i = 0; i < 5; ++i) m_renderer->SetColorControl(i, initial_color[i]);

    m_renderer->LogSwapChainConfig("OutputWindow");

    m_scheduler->SetDxgiWaitableProvider([this]() -> void* {

        return m_renderer ? m_renderer->GetFrameLatencyWaitableObject() : nullptr;

    });



    m_output_window->SetOnVisibilityChanged([this](bool visible) {

        if (m_window) {

            m_window->State().output_window_visible = visible;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

    });



    // Preview Window — independent user-facing viewer window.

    // User can resize, move, maximize, or fullscreen without altering capture resolution.

    m_preview_window = std::make_unique<PreviewWindow>();

    const uint32_t kPrevInitW = (m_settings.preview_width > 0) ? static_cast<uint32_t>(m_settings.preview_width) : kInitW;

    const uint32_t kPrevInitH = (m_settings.preview_height > 0) ? static_cast<uint32_t>(m_settings.preview_height) : kInitH;

    if (!m_preview_window->Create(kPrevInitW, kPrevInitH,

            [this](uint32_t w, uint32_t h) {

                if (m_preview_renderer) m_preview_renderer->SignalResize(w, h);

            })) {

        DUWN_LOG_ERROR("App", "PreviewWindow creation failed");

        return false;

    }

    if (m_settings.preview_always_on_top) {

        m_preview_window->SetAlwaysOnTop(true);

    }

    if (m_settings.preview_x != -1 && m_settings.preview_y != -1 &&

        m_settings.preview_width > 0 && m_settings.preview_height > 0) {

        m_preview_window->SetWindowRect(m_settings.preview_x, m_settings.preview_y,

                                       m_settings.preview_width, m_settings.preview_height);

    }

    m_preview_window->SetOnVisibilityChanged([this](bool visible) {

        if (m_window) {

            m_window->State().preview_visible = visible;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

    });



    // Preview Renderer — attached to embedded video surface inside MainWindow (single-window Workspace)
    HWND preview_target_hwnd = (m_window && m_window->VideoSurfaceHwnd())
        ? m_window->VideoSurfaceHwnd()
        : m_preview_window->Hwnd();

    if (m_d3d->IsHardware())
        m_preview_renderer = std::make_unique<video::VideoRenderer>(*m_d3d, preview_target_hwnd);
    else
        m_preview_renderer = std::make_unique<video::WarpVideoRenderer>(*m_d3d, preview_target_hwnd);

    m_preview_renderer->SetNonBlocking(true);

    if (m_window) {
        m_window->SetOnVideoSurfaceResize([this](uint32_t w, uint32_t h) {
            if (m_preview_renderer) m_preview_renderer->SignalResize(w, h);
        });
    }

    const uint32_t p_client_w = 640;
    const uint32_t p_client_h = 360;

    if (!m_preview_renderer->Init(p_client_w, p_client_h)) {
        DUWN_LOG_ERROR("App", "Preview VideoRenderer init failed");
        return false;
    }

    m_preview_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());
    m_preview_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));
    m_preview_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));
    m_preview_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));
    for (size_t i = 0; i < 5; ++i) m_preview_renderer->SetColorControl(i, initial_color[i]);

    // Export server & shared texture initialization
    m_capture_server = std::make_unique<capture::CaptureServer>();
    m_capture_server->Start(L"DUWN_MIRROR_CAPTURE");
    m_shared_texture = std::make_unique<capture::SharedTexture>();
    if (m_d3d->Device()) {
        m_shared_texture->Create(m_d3d->Device(), kInitW, kInitH);
    }

    m_preview_renderer->LogSwapChainConfig("PreviewWindow");

    // Video minimal infrastructure is ready
    m_video_min_ready.store(true, std::memory_order_release);

    // Audio pipeline
    m_audio_ring   = std::make_unique<audio::AudioRingBuffer>(8192, 2);
    m_audio_engine = std::make_unique<audio::AudioEngine>(*m_audio_ring);
    m_wasapi       = std::make_unique<audio::WasapiOutput>(*m_audio_ring);
    m_wasapi->SetUnderrunCallback([this] {
        if (m_audio_engine) m_audio_engine->NotifyUnderrun();
    });

    if (!m_wasapi->Init(m_settings.monitor_device_id)) {
        DUWN_LOG_WARN("App", "WASAPI init failed; audio output disabled");
        // Non-fatal for Milestone 0
    } else {
        m_wasapi->SetVolume(m_settings.monitor_volume);
        m_wasapi->SetMuted(m_settings.audio_muted);
        m_wasapi->Start();
    }

    // Audio minimal infrastructure is ready
    m_audio_min_ready.store(true, std::memory_order_release);

    // Critical Media Infrastructure Ready (C2) recorded before non-critical work
    duwn::telemetry::ConnectionTimeline::Get().Record(
        duwn::telemetry::ConnectionMilestone::C2_MediaInfrastructureReady,
        m_d3d && m_d3d->IsHardware() ? "D3D11 Hardware + MediaFoundation + WASAPI" : "D3D11 WARP + MediaFoundation + WASAPI");
    m_media_infrastructure_ready.store(true, std::memory_order_release);

    // Non-critical startup tasks (deferred after C2 to avoid startup latency regressions)
    LogCapabilityReport();

    // Audio device manager — registers IMMNotificationClient; needs main HWND so
    // WM_APP_AUDIO_DEVICE_CHANGED posts to the message loop.
    if (!m_audio_device_mgr.Init(m_window->Hwnd())) {
        DUWN_LOG_WARN("App", "AudioDeviceManager init failed; hot-plug notifications disabled");
    }
    m_audio_device_mgr.SetWatchedDeviceId(m_settings.monitor_device_id);
    if (m_window) {
        auto devs = m_audio_device_mgr.Enumerate();
        auto& list = m_window->State().available_audio_devices;
        list.clear();
        for (const auto& d : devs) {
            list.push_back({d.id, d.friendly_name});
            if (d.id == m_settings.monitor_device_id) {
                m_window->State().audio_device_name = d.friendly_name;
            }
        }
        if (m_settings.monitor_device_id.empty()) {
            m_window->State().audio_device_name = L"System Default";
        }
        m_window->State().audio_volume = m_settings.monitor_volume;
        m_window->State().audio_muted = m_settings.audio_muted;
        m_window->State().resolved_audio_device_name = m_wasapi
            ? m_wasapi->ResolvedDeviceName() : L"—";
        m_window->State().audio_fallback_active = m_wasapi ? m_wasapi->IsFallbackActive() : false;
    }
    m_noncritical_ready.store(true, std::memory_order_release);

    m_net_monitor = std::make_unique<network::NetworkChangeMonitor>([this](const network::NetworkEnvironmentInfo& /*new_env*/) {
        HWND hwnd = m_main_hwnd.load(std::memory_order_relaxed);
        if (hwnd) {
            ::PostMessageW(hwnd, WM_DUWN_NETWORK_CHANGED, 0, 0);
        }
    });
    m_net_monitor->Start(&m_net_env);



    // Direct IPC Transport initialization (if configured or enabled)
    TryStartIpcConsumer();



    // FrameScheduler last (needs renderer ready)

    m_scheduler->Start();

    if (m_settings.connection_mode == ConnectionMode::WiredUsb) {

        m_wired_control_requested = true;

        UpdateWiredConnection();

    }



    PublishMetadataSnapshot();

    // Metrics thread

    m_metrics_thread = std::jthread([this](std::stop_token st){

        MetricsLoop(std::move(st));

    });



    if (m_settings.connection_mode == ConnectionMode::WirelessAirPlay)

        m_window->SetStatusText(m_decoder_ready.load(std::memory_order_acquire)

            ? L"Ready — open AirPlay on iPhone"

            : L"Hardware renderer unavailable. Choose Auto or Compatibility.");

    DUWN_LOG_INFO("App", "Init complete");

    return true;

}



video::AspectRatioMode App::GetEffectiveAspectRatioMode() const noexcept {

    if (m_settings.capture_canvas == CaptureCanvas::FollowSource) {

        return video::AspectRatioMode::AspectLocked;

    }

    switch (m_settings.aspect_mode) {

    case AspectMode::Auto:

    case AspectMode::Fit:

        return video::AspectRatioMode::Fit;

    case AspectMode::Fill:

        return video::AspectRatioMode::Fill;

    case AspectMode::Stretch:

        return video::AspectRatioMode::Stretch;

    }

    return video::AspectRatioMode::Fit;

}



video::OutputDimensions App::ComputeCurrentOutputDimensions(
    uint32_t src_w, uint32_t src_h,
    const SessionMetadataSnapshot* snap) const noexcept {

    const CaptureCanvas canvas = snap ? snap->capture_canvas : m_settings.capture_canvas;
    const OutputQuality out_qual = snap ? snap->output_quality : m_settings.output_quality;
    const uint32_t out_w = snap ? snap->output_width : m_settings.output_width;
    const uint32_t out_h = snap ? snap->output_height : m_settings.output_height;

    if (canvas == CaptureCanvas::FollowSource) {

        uint32_t long_edge = GetOutputQualityLongEdge(out_qual);

        if (out_qual == OutputQuality::Custom) {

            return {out_w, out_h};

        }

        return video::ComputeAspectAwareOutputDimensions(src_w, src_h, long_edge);

    }



    switch (canvas) {

    case CaptureCanvas::Canvas_16_9_HD:

        return {1280, 720};

    case CaptureCanvas::Canvas_16_9_FullHD:

        return {1920, 1080};

    case CaptureCanvas::Canvas_16_9_2K:

        return {2560, 1440};

    case CaptureCanvas::Custom:

    default:

        return {out_w, out_h};

    }

}



void App::SyncUiVideoSettings() noexcept {

    if (!m_window) return;

    auto& s = m_window->State();

    s.connection_mode = static_cast<int>(m_connection_mode.load(std::memory_order_relaxed));

    s.remember_selected_mode = m_settings.remember_selected_mode;

    s.default_connection_mode = static_cast<int>(m_settings.default_connection_mode);

    s.renderer_mode = static_cast<int>(m_settings.renderer_mode);

    s.performance_profile = static_cast<int>(m_settings.performance_profile);

    s.streaming_mode = static_cast<int>(m_settings.streaming_mode);
    s.custom_video_freshness_ms = m_settings.custom_video_freshness_ms;
    s.custom_video_queue_frames = m_settings.custom_video_queue_frames;

    s.receiver_quality = static_cast<int>(m_settings.receiver_quality);

    s.output_quality = static_cast<int>(m_settings.output_quality);

    s.capture_canvas = static_cast<int>(m_settings.capture_canvas);

    s.aspect_mode = static_cast<int>(m_settings.aspect_mode);

    s.pixel_perfect_mode = static_cast<int>(m_settings.pixel_perfect);

    s.color_preset = static_cast<int>(m_settings.color_preset);

    s.requested_width = m_settings.receiver_width;

    s.requested_height = m_settings.receiver_height;

    s.requested_fps = m_settings.receiver_fps;

    {

        const std::wstring_view rec_classes[] = {

            ui::loc::Get(ui::loc::S::Opt_Rec_Auto),

            ui::loc::Get(ui::loc::S::Opt_Rec_720p30),

            ui::loc::Get(ui::loc::S::Opt_Rec_720p60),

            ui::loc::Get(ui::loc::S::Opt_Rec_1080p30),

            ui::loc::Get(ui::loc::S::Opt_Rec_1080p60),

            ui::loc::Get(ui::loc::S::Opt_Rec_1440p60)

        };

        s.requested_quality_class = rec_classes[std::clamp(static_cast<int>(m_settings.receiver_quality), 0, 5)];

    }

    s.output_width = m_settings.output_width;

    s.output_height = m_settings.output_height;

    s.selected_output_width = m_settings.output_width;

    s.selected_output_height = m_settings.output_height;

    s.match_source = (m_settings.capture_canvas == CaptureCanvas::FollowSource &&

                      (m_settings.output_quality == OutputQuality::Auto || m_settings.output_quality == OutputQuality::Original));

    s.pixel_perfect = (m_settings.pixel_perfect == PixelPerfectMode::On);

    s.scaling_quality = static_cast<int>(m_settings.scaling_quality);

    s.brightness = m_settings.brightness;

    s.contrast = m_settings.contrast;

    s.saturation = m_settings.saturation;

    s.hue = m_settings.hue;

    s.sharpness = m_settings.sharpness;

    s.color_range = static_cast<int>(m_settings.color_range);

    s.color_matrix = static_cast<int>(m_settings.color_matrix);

    s.start_on_boot = m_settings.start_on_boot;

    s.start_minimized = m_settings.start_minimized;

    s.minimize_to_tray = m_settings.minimize_to_tray;

    s.remember_window_pos = m_settings.remember_window_pos;

    s.debug_log = m_settings.debug_log;

    s.allow_public_networks = m_settings.allow_public_networks;

    s.is_public_network = m_net_env.is_public_profile;

    s.public_rules_missing = m_startup_public_rules_missing;

    s.public_rules_unexpected = m_startup_public_rules_unexpected;

    s.auto_open_output_window = m_settings.auto_open_output_window;

    s.output_start_fullscreen = m_settings.output_start_fullscreen;

    s.preferred_monitor = m_settings.preferred_monitor;

    s.hide_cursor = m_settings.hide_cursor;

    s.remember_output_pos = m_settings.remember_output_pos;

    s.gpu_name = m_d3d ? m_d3d->AdapterName() : L"—";

    s.config_generation = m_meta_coord.GetConfigGeneration();

    s.sidecar_generation = m_meta_coord.GetSidecarGeneration();

    s.receiver_quality_pending = m_meta_coord.IsQualityPending();

    if (m_renderer)

        for (size_t i = 0; i < 5; ++i) s.filter_supported[i] = m_renderer->SupportsColorControl(i);

    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

}



void App::RestartAirPlaySidecar() noexcept {

    if (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WiredUsb &&

        m_wired_bind_ipv4.empty()) return;

    if (!m_airplay) return;



    m_sidecar_restart_posted.store(false, std::memory_order_release);



    const uint64_t gen = m_meta_coord.GetConfigGeneration();

    const std::string_view q_name = GetReceiverQualityName(m_settings.receiver_quality);



    DUWN_LOG_INFOF("App", "Restarting AirPlay sidecar (config_gen={}, quality={}, {}x{}@{}fps)",

        gen, q_name, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);



    if (m_window) {

        m_window->SetStatusText(L"Restarting AirPlay receiver…");

    }



    m_airplay->Stop();
    StopIpcConsumer();
    ResetSessionFirstEvents();

    m_airplay->ConfigureReceiverQuality(

        m_settings.receiver_width,

        m_settings.receiver_height,

        m_settings.receiver_fps,

        gen,

        q_name

    );



    if (!m_airplay->Start()) {

        DUWN_LOG_ERROR("App", "Failed to restart AirPlay receiver sidecar");

        if (m_window) {

            m_window->SetStatusText(L"Error: AirPlay failed to restart");

        }

        return;

    }



    TryStartIpcConsumer();

    m_meta_coord.MarkSidecarRestarted(gen, m_settings.receiver_quality);
    if (m_window) {
        m_window->State().sidecar_generation = gen;
        m_window->State().receiver_quality_pending = false;
    }
    PublishMetadataSnapshot();

    DUWN_LOG_INFOF("App", "AirPlay sidecar restart initiated (config_gen={})", gen);

}



void App::UpdateWiredConnection() noexcept {

    if (!m_window || m_connection_mode.load(std::memory_order_acquire) != ConnectionMode::WiredUsb)

        return;

    const auto current = m_wired_devices.Probe(m_airplay ? m_airplay->GetSidecarPid() : 0);

    m_window->State().wired = current;

    if (current.usb_interface_count == 0) {

        m_wired_network_wait_ticks = 0;

        if (m_wired_control.GetState() != wired::WiredControlState::Disabled)

            m_wired_control.Stop();

        m_window->State().control_state = wired::WiredControlState::Disabled;

    } else if (m_wired_control_requested &&

               m_wired_control.GetState() == wired::WiredControlState::Disabled) {

        StartWiredControl();

    }

    if (!current.network_up || current.network_ipv4.empty()) {

        if (current.usb_interface_count > 0) ++m_wired_network_wait_ticks;

        if (!m_wired_bind_ipv4.empty()) {

            m_wired_reconnect_hint.store(true, std::memory_order_release);

            DUWN_LOG_INFO("Wired", "Apple USB network disconnected; stopping AirPlay media");

            if (m_airplay) m_airplay->Stop();

            m_wired_bind_ipv4.clear();

            m_decoder_ready.store(false, std::memory_order_release);

            if (m_scheduler) m_scheduler->Flush();

            if (m_video_decoder) {

                std::lock_guard<std::mutex> lock(m_decoder_mutex);

                m_video_decoder->ResetCodecState();

            }

            if (m_audio_engine) m_audio_engine->Flush();

            if (m_renderer) m_renderer->PresentBlack();

            if (m_preview_renderer) m_preview_renderer->PresentBlack();

            if (m_settings.hide_preview_on_disconnect && m_preview_window) {

                m_preview_window->Hide();

            }

            ResetSessionFirstEvents();

            m_last_preview_src_w = 0;

            m_last_preview_src_h = 0;

            m_window->State().status = ui::ConnectionStatus::Idle;

            m_window->State().width = m_window->State().height = 0;

        }

        m_window->SetStatusText(current.usb_interface_count == 0 ? L"USB disconnected"

            : ui::loc::Get(m_wired_network_wait_ticks >= 10

                ? ui::loc::S::Wired_EnablePersonalHotspot

                : ui::loc::S::Wired_PreparingUsbNetwork));

        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        return;

    }

    m_wired_network_wait_ticks = 0;

    if (current.network_ipv4 != m_wired_bind_ipv4) {

        if (m_airplay) m_airplay->Stop();

        m_wired_bind_ipv4 = current.network_ipv4;

        DUWN_LOG_INFOF("Wired", "Apple USB Ethernet ifIndex={} LUID={} prefix={}",

            current.network_if_index, current.network_luid, current.network_prefix);

        if (m_airplay) {

            m_airplay->SetBindIpv4(m_wired_bind_ipv4, current.network_prefix);

            if (!m_airplay->Start()) m_window->SetStatusText(L"USB AirPlay receiver failed to start");

        }

    }

    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

}



void App::RefreshWiredDevices() noexcept {

    if (!m_window) return;

    m_window->SetStatusText(ui::loc::Get(ui::loc::S::Wired_Searching));

    if (!m_metrics_thread.joinable()) {

        UpdateWiredConnection();

    } else {

        ::PostMessageW(m_window->Hwnd(), WM_DUWN_WIRED_REFRESH, 0, 0);

    }

    // The metrics loop refreshes USB detection every second once running.

}



void App::HandleWiredControlAction(int action_id) noexcept {

    switch (action_id) {

    case ui::Control_Wired_Btn_Home:

        m_wired_control.SendButton("home");

        break;

    case ui::Control_Wired_Btn_Lock:

        m_wired_control.SendButton("lock");

        break;

    case ui::Control_Wired_Btn_VolDown:

        m_wired_control.SendButton("volume-down");

        break;

    case ui::Control_Wired_Btn_VolUp:

        m_wired_control.SendButton("volume-up");

        break;

    case ui::Control_Wired_Btn_Mute:

        m_wired_control.SendButton("mute");

        break;

    case ui::Control_Wired_Btn_Siri:

        m_wired_control.SendButton("siri");

        break;

    default:

        break;

    }

}



void App::StartWiredControl() noexcept {

    m_wired_control.Start();

    if (m_window) {

        auto& s = m_window->State();

        s.device_control_enabled = true;

        s.control_state = m_wired_control.GetState();

        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

    }

}



void App::StopWiredControl() noexcept {

    m_wired_control.Stop();

    if (m_window) {

        auto& s = m_window->State();

        s.device_control_enabled = false;

        s.control_state = wired::WiredControlState::Disabled;

        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

    }

}



void App::SwitchConnectionMode(ConnectionMode mode) noexcept {

    if (m_connection_mode.load(std::memory_order_acquire) == mode) return;

    m_connection_mode.store(mode, std::memory_order_release);

    m_render_generation.fetch_add(1, std::memory_order_acq_rel);

    if (m_airplay) m_airplay->Stop();

    StopIpcConsumer();

    if (m_scheduler) { m_scheduler->Stop(); m_scheduler->Flush(); }

    {

        std::lock_guard<std::mutex> lock(m_decoder_mutex);

        m_decoder_ready.store(false, std::memory_order_release);

        if (m_video_decoder) m_video_decoder->ResetCodecState();

    }

    if (m_audio_engine) m_audio_engine->Flush();

    if (m_audio_ring) m_audio_ring->Flush();

    if (m_renderer) m_renderer->PresentBlack();

    if (m_preview_renderer) m_preview_renderer->PresentBlack();

    ResetSessionFirstEvents();

    m_last_preview_src_w = 0;

    m_last_preview_src_h = 0;

    m_stream_width.store(0, std::memory_order_relaxed);

    m_stream_height.store(0, std::memory_order_relaxed);

    auto& metrics = GlobalMetrics();

    metrics.video_visible_width.store(0, std::memory_order_relaxed);

    metrics.video_visible_height.store(0, std::memory_order_relaxed);

    if (m_window) {

        auto& state = m_window->State();

        state.connection_mode = static_cast<int>(mode);

        state.session_state = airplay::AirPlaySessionState::Idle;

        state.status = ui::ConnectionStatus::Idle;

        state.width = state.height = 0;

        state.render_fps = state.decoded_fps = state.source_fps = 0;

        state.device_name = state.model_name = state.product_type = state.model_db_match = state.os_version = L"—";

        state.client_ip = L"—";

        state.open_dropdown = 0;

    }

    m_settings.connection_mode = mode;

    m_settings.Save();

    if (m_scheduler) m_scheduler->Start();

    if (mode == ConnectionMode::WirelessAirPlay) {

        StopWiredControl();

        m_wired_control_requested = false;

        m_wired_bind_ipv4.clear();

        m_wired_reconnect_hint.store(false, std::memory_order_release);

        if (m_ble_beacon) {
            network::BleBeaconConfig ble_cfg;
            ble_cfg.airplay_port = 7000;
            ble_cfg.enable_beacon = true;
            m_ble_beacon->Start(ble_cfg);
        }

        if (m_airplay) m_airplay->SetBindIpv4({});

        if (m_airplay) m_airplay->ConfigureReceiverQuality(

            m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps,

            m_meta_coord.GetConfigGeneration(),

            GetReceiverQualityName(m_settings.receiver_quality));

        m_meta_coord.MarkSidecarRestarted(m_meta_coord.GetConfigGeneration(), m_settings.receiver_quality);

        if (m_airplay && !m_airplay->Start())

            DUWN_LOG_ERROR("App", "Wireless mode selected but AirPlay service failed to start");

        TryStartIpcConsumer();

    } else {

        if (m_ble_beacon) {
            m_ble_beacon->Stop();
        }

        m_wired_control_requested = true;

        m_wired_reconnect_hint.store(false, std::memory_order_release);

        UpdateWiredConnection();

    }

    if (m_window) ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

    PublishMetadataSnapshot();

    DUWN_LOG_INFOF("App", "Connection mode switched to {}",

        mode == ConnectionMode::WirelessAirPlay ? "Wireless AirPlay" : "Wired USB");

}



bool App::RecreateVideoPipeline() noexcept {

    m_decoder_ready.store(false, std::memory_order_release);

    if (m_airplay) m_airplay->Stop();

    StopIpcConsumer();

    if (m_scheduler) { m_scheduler->Stop(); m_scheduler->Flush(); }

    m_video_decoder.reset();

    m_renderer.reset();

    m_preview_renderer.reset();

    m_d3d.reset();

    m_active_renderer.store(0, std::memory_order_relaxed);

    m_active_filter_caps.store(0, std::memory_order_relaxed);



    const bool force_software = m_settings.renderer_mode == RendererMode::Software;

    const bool force_hardware = m_settings.renderer_mode == RendererMode::HardwareD3D11;

    const bool force_compatibility = m_settings.renderer_mode == RendererMode::Compatibility;

    m_d3d = std::make_unique<video::D3D11Device>();

    if (!m_d3d->Create(force_software, !force_hardware)) {

        m_window->SetStatusText(L"Hardware renderer unavailable. Choose Auto or Compatibility.");

        return false;

    }

    m_active_renderer.store(m_d3d->IsHardware() ? 1 : 2, std::memory_order_relaxed);

    m_video_decoder = std::make_unique<video::VideoDecoder>(*m_d3d,
        [this](video::VideoFrame frame) {
            const char* codec_str = (m_video_decoder && m_video_decoder->GetActiveCodec() == video::VideoCodecType::H265) ? "HEVC" : "H264";
            m_source_quality_tracker.OnFrame(
                frame.width, frame.height,
                frame.visible_width, frame.visible_height,
                60.0, 0.0,
                codec_str);
            m_scheduler->PushFrame(std::move(frame));
        });

    m_video_decoder->SetPreference((force_software || force_compatibility || !m_d3d->IsHardware()) ? video::DecoderPreference::SoftwareOnly

        : force_hardware ? video::DecoderPreference::HardwareOnly

        : video::DecoderPreference::Auto);

    if (!m_video_decoder->Init(1920, 1080)) {

        m_window->SetStatusText(force_hardware

            ? L"Hardware renderer unavailable. Choose Auto or Compatibility."

            : L"Video decoder unavailable for selected renderer.");

        return false;

    }

    m_decoder_ready.store(true, std::memory_order_release);

    if (m_d3d->IsHardware())

        m_renderer = std::make_unique<video::VideoRenderer>(*m_d3d, m_output_window->Hwnd());

    else

        m_renderer = std::make_unique<video::WarpVideoRenderer>(*m_d3d, m_output_window->Hwnd());

    if (!m_renderer->Init(m_output_window->ClientWidth(), m_output_window->ClientHeight())) {

        m_window->SetStatusText(L"Renderer unavailable. Choose Auto or Compatibility.");

        return false;

    }

    m_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

    m_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));

    m_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));

    m_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));

    const int values[] = {m_settings.brightness, m_settings.contrast, m_settings.saturation,

                          m_settings.hue, m_settings.sharpness};

    for (size_t i = 0; i < 5; ++i) m_renderer->SetColorControl(i, values[i]);

    if (m_preview_window && m_preview_window->Hwnd()) {

        HWND prev_hwnd = m_preview_window->Hwnd();

        const uint32_t pw = m_preview_window->ClientWidth() > 0 ? m_preview_window->ClientWidth() : 640;

        const uint32_t ph = m_preview_window->ClientHeight() > 0 ? m_preview_window->ClientHeight() : 360;

        if (m_d3d->IsHardware())

            m_preview_renderer = std::make_unique<video::VideoRenderer>(*m_d3d, prev_hwnd);

        else

            m_preview_renderer = std::make_unique<video::WarpVideoRenderer>(*m_d3d, prev_hwnd);

        m_preview_renderer->SetNonBlocking(true);

        if (m_preview_renderer->Init(pw, ph)) {

            m_preview_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

            m_preview_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));

            m_preview_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));

            m_preview_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));

            for (size_t i = 0; i < 5; ++i) m_preview_renderer->SetColorControl(i, values[i]);

        } else {

            DUWN_LOG_WARN("App", "Preview VideoRenderer init failed during pipeline recreation");

        }

    }

    m_scheduler->Start();

    TryStartIpcConsumer();

    if (m_airplay) m_airplay->ConfigureReceiverQuality(

        m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps,

        m_meta_coord.GetConfigGeneration(),

        GetReceiverQualityName(m_settings.receiver_quality));

    if (m_airplay && (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WirelessAirPlay ||

        !m_wired_bind_ipv4.empty()) && !m_airplay->Start()) {

        m_window->SetStatusText(L"AirPlay service could not restart.");

        return false;

    }

    m_window->SetStatusText(L"Renderer ready — reconnect AirPlay on iPhone");

    return true;

}

void App::OnNetworkEnvironmentChanged(const network::NetworkEnvironmentInfo& new_env) noexcept {
    DUWN_LOG_INFO("App", "Handling dynamic network environment change on main thread...");
    m_net_env = new_env;
    m_net_env.LogEnvironment();

    if (m_window) {
        m_window->State().is_public_network = m_net_env.is_public_profile;
        if (m_net_env.network_isolation_suspected) {
            DUWN_LOG_WARN("App", "AP Client Isolation suspected on active Wi-Fi network; peer-to-peer AirPlay discovery may be impaired");
        }
        ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
    }

    if (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WirelessAirPlay) {
        if (m_ble_beacon) {
            network::BleBeaconConfig ble_cfg;
            ble_cfg.ipv4_address = m_net_env.best_adapter_ip;
            ble_cfg.airplay_port = 7000;
            ble_cfg.enable_beacon = true;
            m_ble_beacon->Start(ble_cfg);
        }

        const bool is_streaming = m_airplay && (
            m_airplay->CurrentPhase() == airplay::SessionPhase::Streaming ||
            m_airplay->CurrentPhase() == airplay::SessionPhase::Connecting ||
            m_airplay->CurrentSessionState() == airplay::AirPlaySessionState::Streaming ||
            m_airplay->CurrentSessionState() == airplay::AirPlaySessionState::Connected);

        if (!is_streaming) {
            DUWN_LOG_INFO("App", "AirPlay receiver is idle; restarting sidecar to refresh mDNS and adapter binding");
            if (m_airplay) {
                m_airplay->Stop();
                if (!m_airplay->Start()) {
                    DUWN_LOG_ERROR("App", "Failed to restart AirPlay sidecar after network transition");
                    if (m_window) m_window->SetStatusText(L"AirPlay receiver restart failed after network change");
                } else {
                    DUWN_LOG_INFO("App", "AirPlay sidecar successfully restarted on updated network interface");
                }
            }
        } else {
            DUWN_LOG_INFO("App", "Active streaming session detected during network change; preserving session without interruption");
        }
    }
}



void App::ApplySettingChange(int id, int value) noexcept {

    if (id == ui::Control_Mode_Wireless || id == ui::Control_Mode_Wired) {

        SwitchConnectionMode(id == ui::Control_Mode_Wireless

            ? ConnectionMode::WirelessAirPlay : ConnectionMode::WiredUsb);

        return;

    }

    if (id == ui::Control_Wired_Refresh) { RefreshWiredDevices(); return; }

    if (id == ui::Control_Wired_ToggleControl) {

        auto c_state = m_wired_control.GetState();

        if (c_state == wired::WiredControlState::Ready ||

            c_state == wired::WiredControlState::Active ||

            c_state == wired::WiredControlState::Starting ||

            c_state == wired::WiredControlState::ConnectingRsd ||

            c_state == wired::WiredControlState::OpeningHid) {

            StopWiredControl();

            m_wired_control_requested = false;

        } else {

            m_wired_control_requested = true;

            StartWiredControl();

        }

        return;

    }

    if (id >= ui::Control_Wired_Btn_Home && id <= ui::Control_Wired_Btn_Siri) {

        HandleWiredControlAction(id);

        return;

    }

    if (id == ui::Control_Wired_Start) {

        UpdateWiredConnection();

        return;

    }

    if (id == ui::Control_Wired_Troubleshoot) {

        ::ShellExecuteW(m_window ? m_window->Hwnd() : nullptr, L"open", L"devmgmt.msc",

                        nullptr, nullptr, SW_SHOWNORMAL);

        return;

    }

    if (id == ui::Control_Btn_OpenLogs || id == ui::Control_Btn_CrashOpenLogs) {

        std::wstring log_dir = Logger::GetLogDirectory();

        ::ShellExecuteW(m_window ? m_window->Hwnd() : nullptr, L"open",

                        log_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        return;

    }

    if (id == ui::Control_Btn_CopyDiagnostics) {

        if (m_window) {

            std::wstring report = DiagnosticsCollector::BuildReport(m_window->State(), m_settings);

            duwn::system::ClipboardUtil::SetText(report);

            m_window->SetStatusText(duwn::ui::loc::Get(duwn::ui::loc::S::About_DiagCopied));

        }

        return;

    }

    if (id == ui::Control_Btn_OpenNotices) {

        wchar_t executable[MAX_PATH]{};

        ::GetModuleFileNameW(nullptr, executable, MAX_PATH);

        auto notices = std::filesystem::path(executable).parent_path() / L"THIRD_PARTY_NOTICES.txt";

        if (!std::filesystem::exists(notices)) {

            notices = std::filesystem::path(executable).parent_path().parent_path().parent_path() / L"THIRD_PARTY_NOTICES.txt";

        }

        ::ShellExecuteW(m_window ? m_window->Hwnd() : nullptr, L"open",

                        notices.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        return;

    }

    if (id == ui::Control_Btn_OpenGitHub) {

        ::ShellExecuteW(m_window ? m_window->Hwnd() : nullptr, L"open",

                        L"https://github.com/duwn/duwn-mirror", nullptr, nullptr, SW_SHOWNORMAL);

        return;

    }

    if (id == ui::Control_Btn_FirstRunContinue) {

        m_settings.first_run_completed = true;

        m_settings.Save();

        if (m_window) {

            m_window->State().is_first_run = false;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

        return;

    }

    if (id == ui::Control_Btn_CrashDismiss) {

        CrashHandler::ClearCrashFlag();

        m_settings.unclean_shutdown = false;

        m_settings.Save();

        if (m_window) {

            m_window->State().show_crash_banner = false;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

        return;

    }

    if (id == ui::Control_Btn_OpenSettingsFile) {

        wchar_t local_data[MAX_PATH]{};

        ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_data, MAX_PATH);

        const auto file = std::filesystem::path(local_data) / L"Duwn Mirror" / L"settings.json";

        ::ShellExecuteW(m_window ? m_window->Hwnd() : nullptr, L"open",

                        file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        return;

    }

    const auto old_renderer = m_settings.renderer_mode;

    const auto old_quality  = m_settings.receiver_quality;

    using namespace ui;

    switch (id) {

    case Control_Set_StreamingMode:
        m_settings.streaming_mode = static_cast<StreamingMode>(std::clamp(value, 0, 3));
        break;

    case Control_Set_VideoFreshness:
        if (value >= 0 && value < static_cast<int>(std::size(kCustomFreshnessChoicesMs))) {
            m_settings.custom_video_freshness_ms = kCustomFreshnessChoicesMs[value];
            m_settings.streaming_mode = StreamingMode::Custom;
        }
        break;

    case Control_Set_VideoQueueFrames:
        m_settings.custom_video_queue_frames = static_cast<uint32_t>(std::clamp(value + 1, 1, 3));
        m_settings.streaming_mode = StreamingMode::Custom;
        break;

    case Control_Set_Renderer:

        if (value >= 0 && value <= 3) {

            m_settings.renderer_mode = static_cast<RendererMode>(value);

        } else {

            m_settings.renderer_mode = static_cast<RendererMode>((static_cast<int>(m_settings.renderer_mode) + 1) % 4);

        }

        break;

    case Control_Set_Profile: {

        if (value >= 0 && value <= 4) {

            m_settings.performance_profile = static_cast<PerformanceProfile>(value);

        } else {

            m_settings.performance_profile = static_cast<PerformanceProfile>((static_cast<int>(m_settings.performance_profile) + 1) % 5);

        }

        switch (m_settings.performance_profile) {

        case PerformanceProfile::Auto:

            m_settings.renderer_mode = RendererMode::Auto;

            m_settings.receiver_quality = ReceiverQuality::Auto;

            GetReceiverQualityDimensions(m_settings.receiver_quality, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);

            m_settings.output_quality = OutputQuality::Auto;

            m_settings.capture_canvas = CaptureCanvas::FollowSource;

            m_settings.output_width = 1920; m_settings.output_height = 1080;

            m_settings.match_source = true;

            m_settings.aspect_mode = AspectMode::Auto;

            m_settings.pixel_perfect = PixelPerfectMode::Auto;

            m_settings.scaling_quality = ScalingQuality::Auto;

            break;

        case PerformanceProfile::HighQuality:

            m_settings.renderer_mode = RendererMode::HardwareD3D11;

            m_settings.receiver_quality = ReceiverQuality::Auto;

            GetReceiverQualityDimensions(m_settings.receiver_quality, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);

            m_settings.output_quality = OutputQuality::QHD_2K;

            m_settings.capture_canvas = CaptureCanvas::FollowSource;

            m_settings.output_width = 2560; m_settings.output_height = 1440;

            m_settings.match_source = false;

            m_settings.aspect_mode = AspectMode::Auto;

            m_settings.pixel_perfect = PixelPerfectMode::Auto;

            m_settings.scaling_quality = ScalingQuality::Sharp;

            break;

        case PerformanceProfile::Balanced:

            m_settings.renderer_mode = RendererMode::Auto;

            m_settings.receiver_quality = ReceiverQuality::Auto;

            GetReceiverQualityDimensions(m_settings.receiver_quality, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);

            m_settings.output_quality = OutputQuality::FullHD;

            m_settings.capture_canvas = CaptureCanvas::FollowSource;

            m_settings.output_width = 1920; m_settings.output_height = 1080;

            m_settings.match_source = false;

            m_settings.aspect_mode = AspectMode::Auto;

            m_settings.pixel_perfect = PixelPerfectMode::Auto;

            m_settings.scaling_quality = ScalingQuality::Fast;

            break;

        case PerformanceProfile::LowSpec:

            m_settings.renderer_mode = RendererMode::Auto;

            m_settings.receiver_quality = ReceiverQuality::P720_30;

            GetReceiverQualityDimensions(m_settings.receiver_quality, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);

            m_settings.output_quality = OutputQuality::HD;

            m_settings.capture_canvas = CaptureCanvas::FollowSource;

            m_settings.output_width = 1280; m_settings.output_height = 720;

            m_settings.match_source = false;

            m_settings.aspect_mode = AspectMode::Auto;

            m_settings.pixel_perfect = PixelPerfectMode::Off;

            m_settings.scaling_quality = ScalingQuality::Fast;

            break;

        case PerformanceProfile::Custom:

            break;

        }

        break;

    }

    case Control_Set_Receiver: {

        if (value >= 0 && value <= 6) {

            m_settings.receiver_quality = static_cast<ReceiverQuality>(value);

        } else {

            m_settings.receiver_quality = static_cast<ReceiverQuality>((static_cast<int>(m_settings.receiver_quality) + 1) % 7);

        }

        GetReceiverQualityDimensions(m_settings.receiver_quality, m_settings.receiver_width, m_settings.receiver_height, m_settings.receiver_fps);

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    }

    case Control_Set_Output: {

        if (value >= 0 && value <= 5) {

            m_settings.output_quality = static_cast<OutputQuality>(value);

        } else if (value == 6) {

            uint32_t width = m_settings.output_width, height = m_settings.output_height;

            if (PromptCustomResolution(m_window->Hwnd(), width, height)) {

                m_settings.output_quality = OutputQuality::Custom;

                m_settings.output_width = width;

                m_settings.output_height = height;

            }

        } else {

            m_settings.output_quality = static_cast<OutputQuality>((static_cast<int>(m_settings.output_quality) + 1) % 6);

        }

        m_settings.match_source = (m_settings.capture_canvas == CaptureCanvas::FollowSource &&

                                  (m_settings.output_quality == OutputQuality::Auto || m_settings.output_quality == OutputQuality::Original));

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    }

    case Control_Set_CaptureCanvas: {

        if (value == 0) {

            m_settings.capture_canvas = CaptureCanvas::FollowSource;

        } else if (value == 1) {

            m_settings.capture_canvas = CaptureCanvas::Canvas_16_9_HD;

            m_settings.output_width = 1280;

            m_settings.output_height = 720;

            if (m_settings.aspect_mode == AspectMode::Auto) m_settings.aspect_mode = AspectMode::Fit;

        } else if (value == 2) {

            m_settings.capture_canvas = CaptureCanvas::Canvas_16_9_FullHD;

            m_settings.output_width = 1920;

            m_settings.output_height = 1080;

            if (m_settings.aspect_mode == AspectMode::Auto) m_settings.aspect_mode = AspectMode::Fit;

        } else if (value == 3) {

            m_settings.capture_canvas = CaptureCanvas::Canvas_16_9_2K;

            m_settings.output_width = 2560;

            m_settings.output_height = 1440;

            if (m_settings.aspect_mode == AspectMode::Auto) m_settings.aspect_mode = AspectMode::Fit;

        } else if (value == 4) {

            uint32_t width = m_settings.output_width, height = m_settings.output_height;

            if (PromptCustomResolution(m_window->Hwnd(), width, height)) {

                m_settings.capture_canvas = CaptureCanvas::Custom;

                m_settings.output_width = width;

                m_settings.output_height = height;

                if (m_settings.aspect_mode == AspectMode::Auto) m_settings.aspect_mode = AspectMode::Fit;

            }

        }

        m_settings.match_source = (m_settings.capture_canvas == CaptureCanvas::FollowSource &&

                                  (m_settings.output_quality == OutputQuality::Auto || m_settings.output_quality == OutputQuality::Original));

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    }

    case Control_Set_AspectMode: {

        if (m_settings.capture_canvas == CaptureCanvas::FollowSource) {

            m_settings.aspect_mode = AspectMode::Auto;

        } else {

            if (value >= 0 && value <= 2) {

                m_settings.aspect_mode = static_cast<AspectMode>(value + 1);

            } else {

                int cur = static_cast<int>(m_settings.aspect_mode);

                if (cur < 1 || cur > 3) cur = 1;

                else cur = (cur % 3) + 1;

                m_settings.aspect_mode = static_cast<AspectMode>(cur);

            }

        }

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    }

    case Control_Set_CustomOutput: {

        uint32_t width = m_settings.output_width, height = m_settings.output_height;

        if (!PromptCustomResolution(m_window->Hwnd(), width, height)) return;

        m_settings.output_quality = OutputQuality::Custom;

        m_settings.capture_canvas = CaptureCanvas::Custom;

        m_settings.output_width = width;

        m_settings.output_height = height;

        m_settings.match_source = false;

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    }

    case Control_Set_Scaling:

        if (value >= 0 && value <= 3) {

            m_settings.scaling_quality = static_cast<ScalingQuality>(value);

        } else {

            m_settings.scaling_quality = static_cast<ScalingQuality>((static_cast<int>(m_settings.scaling_quality) + 1) % 4);

        }

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    case Control_Set_PixelPerfect:

        if (value >= 0 && value <= 2) {

            m_settings.pixel_perfect = static_cast<PixelPerfectMode>(value);

        } else {

            m_settings.pixel_perfect = static_cast<PixelPerfectMode>((static_cast<int>(m_settings.pixel_perfect) + 1) % 3);

        }

        m_settings.performance_profile = PerformanceProfile::Custom;

        break;

    case Control_Set_ColorPreset:

        if (value >= 0 && value <= 3) {

            m_settings.color_preset = static_cast<ColorPreset>(value);

            switch (m_settings.color_preset) {

            case ColorPreset::Neutral:

                m_settings.brightness = 0; m_settings.contrast = 0; m_settings.saturation = 0;

                m_settings.hue = 0; m_settings.sharpness = 0;

                break;

            case ColorPreset::Vivid:

                m_settings.brightness = 4; m_settings.contrast = 8; m_settings.saturation = 14;

                m_settings.hue = 0; m_settings.sharpness = 15;

                break;

            case ColorPreset::Soft:

                m_settings.brightness = -2; m_settings.contrast = -6; m_settings.saturation = -8;

                m_settings.hue = 0; m_settings.sharpness = 0;

                break;

            case ColorPreset::Custom:

                break;

            }

        }

        break;

    case Control_Set_ResetColor:

        m_settings.brightness = m_settings.contrast = m_settings.saturation = m_settings.hue = m_settings.sharpness = 0;

        m_settings.color_preset = ColorPreset::Neutral;

        break;

    case Control_Reset_Brightness:

        m_settings.brightness = 0;

        m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        break;

    case Control_Reset_Contrast:

        m_settings.contrast = 0;

        m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        break;

    case Control_Reset_Saturation:

        m_settings.saturation = 0;

        m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        break;

    case Control_Reset_Hue:

        m_settings.hue = 0;

        m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        break;

    case Control_Reset_Sharpness:

        m_settings.sharpness = 0;

        m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        break;

    case Control_Toggle_AdvancedColor:

        if (m_window) m_window->State().advanced_color_expanded = !m_window->State().advanced_color_expanded;

        break;

    case Control_Set_ColorRange:

        if (value >= 0 && value <= 2) {

            m_settings.color_range = static_cast<ColorRange>(value);

        } else {

            m_settings.color_range = static_cast<ColorRange>((static_cast<int>(m_settings.color_range) + 1) % 3);

        }

        break;

    case Control_Set_ColorMatrix:

        if (value >= 0 && value <= 3) {

            m_settings.color_matrix = static_cast<ColorMatrix>(value);

        } else {

            m_settings.color_matrix = static_cast<ColorMatrix>((static_cast<int>(m_settings.color_matrix) + 1) % 4);

        }

        break;

    case Control_Toggle_StartOnBoot:

        m_settings.start_on_boot = (value != 0);

        break;

    case Control_Toggle_StartMinimized:

        m_settings.start_minimized = (value != 0);

        break;

    case Control_Toggle_MinimizeToTray:

        m_settings.minimize_to_tray = (value != 0);

        break;

    case Control_Toggle_RememberWindowPos:

        m_settings.remember_window_pos = (value != 0);

        break;

    case Control_Toggle_RememberMode:

        m_settings.remember_selected_mode = (value != 0);

        break;

    case Control_Set_DefaultMode:

        m_settings.default_connection_mode = value == 1

            ? ConnectionMode::WiredUsb : ConnectionMode::WirelessAirPlay;

        break;

    case Control_Toggle_DebugLog:

        m_settings.debug_log = (value != 0);

        Logger::SetLevel(m_settings.debug_log ? LogLevel::Debug : LogLevel::Info);

        break;

    case Control_Toggle_AllowPublicNetworks: {

        HWND hwnd = m_main_hwnd.load(std::memory_order_relaxed);

        if (value != 0) {

            // Explain security implication before requesting elevation

            int resp = ::MessageBoxW(

                hwnd,

                loc::Get(loc::S::Network_PublicSecurityPrompt),

                loc::Get(loc::S::Network_PublicSecurityTitle),

                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

            if (resp == IDYES) {

                bool ok = duwn::network::ConfigurePublicFirewall(true, hwnd);

                if (ok) {

                    m_settings.allow_public_networks = true;

                } else {

                    m_settings.allow_public_networks = false;

                    ::MessageBoxW(

                        hwnd,

                        loc::Get(loc::S::Network_PublicUacDenied),

                        loc::Get(loc::S::Network_PublicSecurityTitle),

                        MB_OK | MB_ICONERROR);

                }

            } else {

                m_settings.allow_public_networks = false;

            }

        } else {

            // Disable flow: request elevation only if Public DUWN rules currently exist

            bool rules_exist = duwn::network::ArePublicFirewallRulesPresent();

            if (rules_exist) {

                bool ok = duwn::network::ConfigurePublicFirewall(false, hwnd);

                if (ok) {

                    m_settings.allow_public_networks = false;

                } else {

                    // UAC denied or removal failed: do NOT falsely show as disabled; restore toggle to On

                    m_settings.allow_public_networks = true;

                    ::MessageBoxW(

                        hwnd,

                        loc::Get(loc::S::Network_PublicDisableUacDenied),

                        loc::Get(loc::S::Network_PublicSecurityTitle),

                        MB_OK | MB_ICONWARNING);

                }

            } else {

                m_settings.allow_public_networks = false;

            }

        }

        m_settings.Save();

        if (m_window) {

            m_window->State().allow_public_networks = m_settings.allow_public_networks;

            m_window->State().public_rules_missing = false;

            m_window->State().public_rules_unexpected = false;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

        break;

    }

    case Control_Btn_RepairPublicRules: {

        HWND hwnd = m_main_hwnd.load(std::memory_order_relaxed);

        int resp = ::MessageBoxW(

            hwnd,

            loc::Get(loc::S::Network_PublicSecurityPrompt),

            loc::Get(loc::S::Network_PublicSecurityTitle),

            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

        if (resp == IDYES) {

            bool ok = duwn::network::ConfigurePublicFirewall(true, hwnd);

            if (ok) {

                m_settings.allow_public_networks = true;

                if (m_window) {

                    m_window->State().allow_public_networks = true;

                    m_window->State().public_rules_missing = false;

                    m_window->State().public_rules_unexpected = false;

                    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

                }

            } else {

                m_settings.allow_public_networks = false;

                ::MessageBoxW(

                    hwnd,

                    loc::Get(loc::S::Network_PublicUacDenied),

                    loc::Get(loc::S::Network_PublicSecurityTitle),

                    MB_OK | MB_ICONERROR);

            }

            m_settings.Save();

        }

        break;

    }

    case Control_Btn_DisablePublicRules: {

        HWND hwnd = m_main_hwnd.load(std::memory_order_relaxed);

        bool rules_exist = duwn::network::ArePublicFirewallRulesPresent();

        if (rules_exist) {

            bool ok = duwn::network::ConfigurePublicFirewall(false, hwnd);

            if (ok) {

                m_settings.allow_public_networks = false;

                if (m_window) {

                    m_window->State().allow_public_networks = false;

                    m_window->State().public_rules_missing = false;

                    m_window->State().public_rules_unexpected = false;

                    ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

                }

            } else {

                m_settings.allow_public_networks = true;

                ::MessageBoxW(

                    hwnd,

                    loc::Get(loc::S::Network_PublicDisableUacDenied),

                    loc::Get(loc::S::Network_PublicSecurityTitle),

                    MB_OK | MB_ICONWARNING);

            }

        } else {

            m_settings.allow_public_networks = false;

            if (m_window) {

                m_window->State().allow_public_networks = false;

                m_window->State().public_rules_missing = false;

                m_window->State().public_rules_unexpected = false;

                ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

            }

        }

        m_settings.Save();

        break;

    }

    case Control_Btn_OpenNetworkSettings:

        ::ShellExecuteW(nullptr, L"open", L"ms-settings:network", nullptr, nullptr, SW_SHOWNORMAL);

        break;

    case Control_Btn_AllowOnThisNetwork: {

        HWND hwnd = m_main_hwnd.load(std::memory_order_relaxed);

        int resp = ::MessageBoxW(

            hwnd,

            loc::Get(loc::S::Network_PublicSecurityPrompt),

            loc::Get(loc::S::Network_PublicSecurityTitle),

            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

        if (resp == IDYES) {

            bool ok = duwn::network::ConfigurePublicFirewall(true, hwnd);

            if (ok) {

                m_settings.allow_public_networks = true;

            } else {

                m_settings.allow_public_networks = false;

                ::MessageBoxW(

                    hwnd,

                    loc::Get(loc::S::Network_PublicUacDenied),

                    loc::Get(loc::S::Network_PublicSecurityTitle),

                    MB_OK | MB_ICONERROR);

            }

        } else {

            m_settings.allow_public_networks = false;

        }

        m_settings.Save();

        if (m_window) {

            m_window->State().allow_public_networks = m_settings.allow_public_networks;

            m_window->State().public_rules_missing = false;

            m_window->State().public_rules_unexpected = false;

            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);

        }

        break;

    }

    case Control_Toggle_AutoOpenOutput:

        m_settings.auto_open_output_window = (value != 0);

        break;

    case Control_Toggle_StartFullscreen:

        m_settings.output_start_fullscreen = (value != 0);

        break;

    case Control_Set_PreferredMonitor:

        m_settings.preferred_monitor = value;

        break;

    case Control_Toggle_HideCursor:

        m_settings.hide_cursor = (value != 0);

        break;

    case Control_Toggle_RememberOutputPos:

        m_settings.remember_output_pos = (value != 0);

        break;

    default:

        if (id >= Control_Set_Brightness && id <= Control_Set_Sharpness) {

            if (!m_renderer || !m_renderer->SupportsColorControl(static_cast<size_t>(id - Control_Set_Brightness))) return;

            int* controls[] = {&m_settings.brightness, &m_settings.contrast, &m_settings.saturation,

                               &m_settings.hue, &m_settings.sharpness};

            *controls[id - Control_Set_Brightness] = (id == Control_Set_Sharpness)

                ? std::clamp(value, 0, 100) : std::clamp(value, -100, 100);

            m_settings.color_preset = (m_settings.brightness == 0 && m_settings.contrast == 0 && m_settings.saturation == 0 && m_settings.hue == 0 && m_settings.sharpness == 0) ? ColorPreset::Neutral : ColorPreset::Custom;

        } else return;

    }



    if (m_renderer) {

        m_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

        m_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));

        m_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));

        m_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));

        const int controls[] = {m_settings.brightness, m_settings.contrast, m_settings.saturation,

                                m_settings.hue, m_settings.sharpness};

        for (size_t i = 0; i < 5; ++i) m_renderer->SetColorControl(i, controls[i]);

    }

    if (m_preview_renderer) {

        m_preview_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

        m_preview_renderer->SetPixelPerfect(static_cast<int>(m_settings.pixel_perfect));

        m_preview_renderer->SetScalingQuality(static_cast<int>(m_settings.scaling_quality));

        m_preview_renderer->SetColorSpace(static_cast<int>(m_settings.color_range), static_cast<int>(m_settings.color_matrix));

        const int controls[] = {m_settings.brightness, m_settings.contrast, m_settings.saturation,

                                m_settings.hue, m_settings.sharpness};

        for (size_t i = 0; i < 5; ++i) m_preview_renderer->SetColorControl(i, controls[i]);

    }

    m_match_source.store(m_settings.match_source, std::memory_order_relaxed);

    if (m_output_window && (id == Control_Set_Output || id == Control_Set_CaptureCanvas || id == Control_Set_CustomOutput || id == Control_Set_Profile || id == Control_Set_AspectMode)) {

        uint32_t vis_w = GlobalMetrics().video_visible_width.load(std::memory_order_relaxed);

        uint32_t vis_h = GlobalMetrics().video_visible_height.load(std::memory_order_relaxed);

        auto out_dims = ComputeCurrentOutputDimensions(vis_w, vis_h);

        uint32_t w = out_dims.width;

        uint32_t h = out_dims.height;

        if (w && h) {

            m_output_window->SetCanvasSize(w, h);

            if (m_renderer) m_renderer->SignalResize(w, h);

            m_output_window->SetAspectRatio(w, h);

            if (m_renderer) m_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

            m_last_aspect_w = w;

            m_last_aspect_h = h;

        }

    }

    const bool quality_changed = (old_quality != m_settings.receiver_quality);
    m_meta_coord.UpdateRequestedSettings(m_settings, quality_changed);

    if (quality_changed) {

        if (m_window) m_window->State().receiver_quality_pending = true;

    }



    if (old_renderer != m_settings.renderer_mode) {

        RecreateVideoPipeline();

    } else if (quality_changed) {

        const bool is_streaming = m_airplay && (

            m_airplay->CurrentPhase() == airplay::SessionPhase::Streaming ||

            m_airplay->CurrentPhase() == airplay::SessionPhase::Connecting ||

            m_airplay->CurrentSessionState() == airplay::AirPlaySessionState::Streaming ||

            m_airplay->CurrentSessionState() == airplay::AirPlaySessionState::Connected);



        if (is_streaming) {

            if (m_window) {

                m_window->State().receiver_quality_pending = true;

                m_window->SetStatusText(L"AirPlay will reconnect to apply Receiver Quality.");

            }

        } else {

            RestartAirPlaySidecar();

        }

    }

    if (m_scheduler) {
        const StreamingPolicy policy = ResolveStreamingPolicy(m_settings.streaming_mode,
            m_settings.custom_video_freshness_ms, m_settings.custom_video_queue_frames);
        m_scheduler->SetStreamingPolicy(policy);
        if (id == Control_Set_StreamingMode || id == Control_Set_VideoFreshness ||
            id == Control_Set_VideoQueueFrames) {
            DUWN_LOG_INFOF("App", "[ReceiverDelivery] mode={} decoded_frames={} freshness_ms={} cadence_percent={} latest={} (source quality unchanged)",
                static_cast<int>(m_settings.streaming_mode), policy.max_decoded_frames,
                policy.max_residence_ms, policy.cadence_percent, policy.always_latest);
        }
    }
    SyncUiVideoSettings();
    PublishMetadataSnapshot();

    if (id < Control_Set_Brightness || id > Control_Set_Sharpness)

        m_settings.Save();

}



void App::Shutdown() noexcept {

    m_running.store(false, std::memory_order_release);
    m_media_infrastructure_ready.store(false, std::memory_order_release);



    // Save persistent settings (%LOCALAPPDATA%\Duwn Mirror\settings.json)

    if (m_window && m_window->Hwnd()) {

        WINDOWPLACEMENT wp{sizeof(wp)};

        if (::GetWindowPlacement(m_window->Hwnd(), &wp)) {

            m_settings.window_preferences.x = wp.rcNormalPosition.left;

            m_settings.window_preferences.y = wp.rcNormalPosition.top;

            m_settings.window_preferences.width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;

            m_settings.window_preferences.height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

            m_settings.window_preferences.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);

        }

    }

    if (m_output_window) {

        m_settings.aspect_ratio_locked = m_output_window->IsAspectLocked();

        m_settings.always_on_top = m_output_window->IsAlwaysOnTop();

    }

    if (m_preview_window) {

        int px = 0, py = 0, pw = 0, ph = 0;

        m_preview_window->GetWindowRect(px, py, pw, ph);

        m_settings.preview_x = px;

        m_settings.preview_y = py;

        m_settings.preview_width = pw;

        m_settings.preview_height = ph;

        m_settings.preview_always_on_top = m_preview_window->IsAlwaysOnTop();

    }

    if (m_wasapi) {

        m_settings.audio_muted = m_wasapi->IsMuted();

    }

    m_settings.unclean_shutdown = false;

    m_settings.Save();

    CrashHandler::ClearCrashFlag();



    m_metrics_thread.request_stop();
    if (m_metrics_thread.joinable()) m_metrics_thread.join();

    m_test_motion_thread.request_stop();
    if (m_test_motion_thread.joinable()) m_test_motion_thread.join();



    StopWiredControl();

    if (m_ble_beacon)   m_ble_beacon->Stop();

    if (m_net_monitor)  m_net_monitor->Stop();

    StopIpcConsumer();

    if (m_airplay)      m_airplay->Stop();

    if (m_scheduler)    m_scheduler->Stop();

    if (m_wasapi)       m_wasapi->Stop();



    m_ble_beacon.reset();

    m_net_monitor.reset();

    m_ipc_consumer.reset();

    m_airplay.reset();

    m_scheduler.reset();

    m_video_decoder.reset();

    m_audio_engine.reset();

    m_wasapi.reset();

    m_audio_ring.reset();

    m_renderer.reset();

    m_preview_renderer.reset();

    m_d3d.reset();

    m_preview_window.reset();

    m_output_window.reset();

    m_window.reset();



    ::MFShutdown();

    ::CoUninitialize();

}



bool App::WaitForMediaReadiness(uint32_t timeout_ms) const noexcept {
    if (m_media_infrastructure_ready.load(std::memory_order_acquire)) {
        return true;
    }
    auto start = std::chrono::steady_clock::now();
    while (!m_media_infrastructure_ready.load(std::memory_order_acquire)) {
        if (!m_running.load(std::memory_order_relaxed)) {
            return false;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void App::ResetSessionFirstEvents() noexcept {
    m_session_first_frame_handled.store(false, std::memory_order_release);
    m_first_video_rtp_recorded.store(false, std::memory_order_release);
    m_first_au_recorded.store(false, std::memory_order_release);
    m_first_output_present_recorded.store(false, std::memory_order_release);
    m_first_preview_present_recorded.store(false, std::memory_order_release);
    m_probe_packet_count.store(0, std::memory_order_relaxed);
}

void App::TryStartIpcConsumer() noexcept {
    if (m_connection_mode.load(std::memory_order_acquire) != ConnectionMode::WirelessAirPlay ||
        m_settings.transport_mode != TransportMode::DirectIpc) {
        StopIpcConsumer();
        return;
    }

    if (!m_ipc_consumer) {
        m_ipc_consumer = std::make_unique<ipc::VideoIpcConsumer>();
    }

    if (m_ipc_consumer->IsOpen() || m_ipc_consumer->Open()) {
        m_direct_ipc_active.store(true, std::memory_order_release);
        DUWN_LOG_INFO("App", "Direct IPC Video Consumer attached to shared memory ring");

        m_ipc_consumer->Start([this](const ipc::IpcAccessUnitHeader& hdr, const uint8_t* payload, size_t size) {
            GlobalMetrics().ipc_frames_consumed.fetch_add(1, std::memory_order_relaxed);
            if (hdr.producer_send_ns > 0) {
                int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();
                double lat_ms = static_cast<double>(now_ns - hdr.producer_send_ns) / 1'000'000.0;
                GlobalMetrics().ipc_transport_latency_ms.store(lat_ms, std::memory_order_relaxed);
            }

            if (m_connection_mode.load(std::memory_order_acquire) != ConnectionMode::WirelessAirPlay ||
                !m_decoder_ready.load(std::memory_order_acquire)) return;

            video::EncodedAccessUnit au;
            au.data.assign(payload, payload + size);
            au.pts_ns = hdr.pts_ns;
            au.dts_ns = hdr.dts_ns;
            au.sequence_number = hdr.sequence_number;
            au.format_generation = hdr.format_generation;
            au.codec = (m_video_decoder && m_video_decoder->GetActiveCodec() != video::VideoCodecType::Unknown)
                ? m_video_decoder->GetActiveCodec()
                : video::VideoCodecType::H264;
            au.has_idr = (hdr.flags & ipc::IpcVideoFlags::Keyframe) != 0;
            au.has_sps = (hdr.flags & ipc::IpcVideoFlags::HasSps) != 0;
            au.has_pps = (hdr.flags & ipc::IpcVideoFlags::HasPps) != 0;
            au.width_hint = hdr.width;
            au.height_hint = hdr.height;
            au.au_received_qpc = clock::MonotonicClock::NowQpcTicks();

            std::lock_guard<std::mutex> lock(m_decoder_mutex);
            if (m_video_decoder) {
                m_video_decoder->FeedAccessUnit(std::move(au));
            }
        });
    } else {
        m_direct_ipc_active.store(false, std::memory_order_release);
        DUWN_LOG_WARN("App", "Direct IPC shared memory ring not ready, falling back to RTP legacy");
    }
}

void App::StopIpcConsumer() noexcept {
    m_direct_ipc_active.store(false, std::memory_order_release);
    if (m_ipc_consumer) {
        m_ipc_consumer->Stop();
    }
}



void App::OnVideoData(const uint8_t* data, size_t size,

                       uint32_t rtp_ts, int64_t arrival_ns,

                       bool marker, uint16_t seq) noexcept {

    GlobalMetrics().video_rtp_packets.fetch_add(1, std::memory_order_relaxed);

    GlobalMetrics().video_rtp_bytes.fetch_add(size, std::memory_order_relaxed);



    if (!m_first_video_rtp_recorded.exchange(true, std::memory_order_relaxed)) {

        duwn::telemetry::ConnectionTimeline::Get().Record(
            duwn::telemetry::ConnectionMilestone::C8_FirstVideoRtp,
            std::format("seq={}, size={} B, ts={}", seq, size, rtp_ts));

        DUWN_LOG_INFOF("Diagnostics",

            "FIRST EVENT: Video RTP packet received (seq={}, size={} B, ts={})",

            seq, size, rtp_ts);

    }

    if (m_probe_packet_count.load(std::memory_order_relaxed) < 10) {
        int count = m_probe_packet_count.fetch_add(1, std::memory_order_relaxed);
        if (count < 10) {
            auto classification = video::ClassifyRtpPayload(std::span<const uint8_t>(data, size));
            int64_t qpc = clock::MonotonicClock::Now().time_since_epoch().count();
            DUWN_LOG_INFOF("WiredCodecProbe",
                "sender_codec={} rtp_payload_type={} clock_rate={} first_valid_packet_qpc={} evidence={}",
                classification.sender_codec,
                classification.rtp_payload_type,
                classification.clock_rate,
                qpc,
                classification.evidence);
        }
    }

    if (!m_video_min_ready.load(std::memory_order_acquire)) {
        GlobalMetrics().video_dropped_min_ready.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // If Direct IPC is active and receiving frames, avoid decoding redundant legacy RTP
    if (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WirelessAirPlay &&
        m_direct_ipc_active.load(std::memory_order_acquire)) {
        GlobalMetrics().video_dropped_ipc_active.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Decoder auto-recovery and dynamic codec adaptation on incoming unambiguous RTP parameter sets / keyframes
    auto classification = video::ClassifyRtpPayload(std::span<const uint8_t>(data, size));
    if (classification.codec != video::DetectedCodec::Unknown) {
        const auto codec_type = (classification.codec == video::DetectedCodec::H265)
            ? video::VideoCodecType::H265
            : video::VideoCodecType::H264;

        const bool decoder_ready = m_decoder_ready.load(std::memory_order_acquire);
        const bool codec_mismatch = !decoder_ready ||
            (m_video_decoder && m_video_decoder->GetActiveCodec() != codec_type);

        if (codec_mismatch) {
            const uint32_t init_w = (m_stream_width.load(std::memory_order_relaxed) > 0)
                ? m_stream_width.load(std::memory_order_relaxed)
                : (m_settings.receiver_width > 0 ? m_settings.receiver_width : 1920);
            const uint32_t init_h = (m_stream_height.load(std::memory_order_relaxed) > 0)
                ? m_stream_height.load(std::memory_order_relaxed)
                : (m_settings.receiver_height > 0 ? m_settings.receiver_height : 1080);

            m_decoder_ready.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_decoder_mutex);
            if (m_video_decoder) {
                m_video_decoder->Flush();
                m_video_decoder->ResetCodecState();
                if (m_video_decoder->Init(init_w, init_h, codec_type)) {
                    m_stream_width.store(init_w, std::memory_order_relaxed);
                    m_stream_height.store(init_h, std::memory_order_relaxed);
                    m_decoder_ready.store(true, std::memory_order_release);
                    DUWN_LOG_INFOF("App", "Video decoder configured for {}x{} ({}) via {}",
                        init_w, init_h, codec_type == video::VideoCodecType::H265 ? "HEVC" : "H.264",
                        classification.evidence);
                } else {
                    m_decoder_ready.store(false, std::memory_order_release);
                    DUWN_LOG_ERRORF("App", "Failed to configure video decoder for {}x{} ({}) via {}",
                        init_w, init_h, codec_type == video::VideoCodecType::H265 ? "HEVC" : "H.264",
                        classification.evidence);
                }
            }
        }
    }

    if (!m_decoder_ready.load(std::memory_order_acquire)) {
        GlobalMetrics().video_dropped_decoder_not_ready.fetch_add(1, std::memory_order_relaxed);
        return;
    }



    {

        std::lock_guard<std::mutex> lock(m_decoder_mutex);

        if (m_video_decoder) {

            m_video_decoder->FeedRtp(data, size, rtp_ts, arrival_ns, marker, seq);

        }

    }

}



void App::OnAudioData(const uint8_t* data, size_t size,

                       uint32_t rtp_ts, int64_t arrival_ns) noexcept {

    GlobalMetrics().audio_rtp_packets.fetch_add(1, std::memory_order_relaxed);

    GlobalMetrics().audio_rtp_bytes.fetch_add(size, std::memory_order_relaxed);



    static std::atomic<bool> s_first_audio_rtp{false};

    if (!s_first_audio_rtp.exchange(true, std::memory_order_relaxed)) {

        duwn::telemetry::ConnectionTimeline::Get().Record(
            duwn::telemetry::ConnectionMilestone::C14_FirstAudioRtp,
            std::format("size={} B, ts={}", size, rtp_ts));

        DUWN_LOG_INFOF("Diagnostics",

            "FIRST EVENT: Audio RTP packet received (size={} B, ts={})",

            size, rtp_ts);

    }



    if (!m_audio_min_ready.load(std::memory_order_acquire)) {
        // Audio pipeline not yet ready — drop early audio packet without blocking RTP thread
        return;
    }



    if (m_audio_engine) {

        m_audio_engine->Feed(data, size, rtp_ts, arrival_ns);

    }

}



void App::OnPhase(airplay::SessionPhase prev,
                  airplay::SessionPhase next) noexcept {
    uint64_t sidecar_gen = m_airplay ? m_airplay->GetSidecarGeneration() : 0;
    const uint64_t current_side_gen = m_meta_coord.GetSidecarGeneration();
    if (sidecar_gen < current_side_gen && sidecar_gen != 0) {
        DUWN_LOG_WARNF("App", "Ignoring stale OnPhase event {} -> {} from sidecar generation {} (current={})",
                       airplay::PhaseString(prev), airplay::PhaseString(next), sidecar_gen, current_side_gen);
        return;
    }

    using P = airplay::SessionPhase;
    switch (next) {
    case P::Advertising:
        if (m_renderer) m_renderer->PresentBlack();
        if (m_preview_renderer) m_preview_renderer->PresentBlack();
        ResetSessionFirstEvents();
        m_last_preview_src_w = 0;
        m_last_preview_src_h = 0;
        break;

    case P::Reconnecting:
        m_decoder_ready.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_decoder_mutex);
            if (m_video_decoder) m_video_decoder->ResetCodecState();
        }
        if (m_scheduler)     m_scheduler->Flush();
        if (m_audio_engine)  m_audio_engine->Flush();
        if (m_renderer)      m_renderer->PresentBlack();
        if (m_preview_renderer) m_preview_renderer->PresentBlack();
        StopIpcConsumer();
        ResetSessionFirstEvents();
        m_last_preview_src_w = 0;
        m_last_preview_src_h = 0;

        if (m_meta_coord.IsReceiverConfigDirty()) {
            bool expected = false;
            if (m_sidecar_restart_posted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                HWND hwnd = m_main_hwnd.load(std::memory_order_acquire);
                if (hwnd) {
                    ::PostMessageW(hwnd, WM_DUWN_RESTART_AIRPLAY, 0, 0);
                } else {
                    m_sidecar_restart_posted.store(false, std::memory_order_release);
                }
            }
        }
        break;

    default:
        break;
    }

    m_meta_coord.PostPhaseEvent(prev, next, sidecar_gen);

    HWND hwnd = m_main_hwnd.load(std::memory_order_acquire);
    if (hwnd) {
        ::PostMessageW(hwnd, WM_DUWN_SESSION_PHASE, 0, 0);
    }
}

void App::ProcessPendingSessionEvents() noexcept {
    m_meta_coord.ProcessPendingEvents([this](const SessionPhaseEvent& ev, bool quality_applied) {
        HandleSessionPhaseOnMainThread(ev, quality_applied);
    });
    PublishMetadataSnapshot();
}

void App::HandleSessionPhaseOnMainThread(const SessionPhaseEvent& ev, bool quality_applied) noexcept {
    if (m_window) {
        m_window->UpdateSessionPhase(ev.next);
        if (ev.generation > 0) {
            m_window->State().sidecar_generation = ev.generation;
        }
        if (quality_applied) {
            m_window->State().receiver_quality_pending = false;
            ::InvalidateRect(m_window->Hwnd(), nullptr, FALSE);
        }
    }

    using P = airplay::SessionPhase;
    switch (ev.next) {
    case P::Advertising:
        if (m_settings.hide_preview_on_disconnect && m_preview_window) {
            m_preview_window->Hide();
        }
        if (m_window) {
            m_window->SetStatusText(m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WiredUsb
                ? L"USB Screen Mirroring ready" : L"Ready — open AirPlay on iPhone");
        }
        break;

    case P::Connecting:
        if (m_window) {
            m_window->SetStatusText(L"Connecting…");
        }
        break;

    case P::Streaming:
        if (m_window) {
            bool has_frame = m_first_output_present_recorded.load(std::memory_order_relaxed);
            m_window->SetStatusText(has_frame ? ui::loc::Get(ui::loc::S::Status_Streaming) : ui::loc::Get(ui::loc::S::Status_ConnectedWaitingVideo));
        }
        DUWN_LOG_INFO("App", "AirPlay streaming session active — media pipeline configuration:");
        LogCapabilityReport();
        break;

    case P::Reconnecting:
        if (m_window) {
            m_window->SetStatusText(L"Reconnecting…");
        }
        if (m_settings.hide_preview_on_disconnect && m_preview_window) {
            m_preview_window->Hide();
        }
        break;

    case P::SidecarMissing:
        if (m_window) {
            m_window->SetStatusText(L"Error: uxplay.exe missing in duwn-airplay");
        }
        break;

    case P::AdvertisingFailed:
        if (m_window) {
            m_window->SetStatusText(L"Error: AirPlay failed to start");
        }
        break;

    default:
        break;
    }
}



void App::OnMetadata(const airplay::StreamMetadata& meta) noexcept {

    if (!WaitForMediaReadiness(2500)) {
        DUWN_LOG_ERROR("App", "Timed out waiting for media infrastructure readiness during metadata setup");
        return;
    }

    // 1. Audio setup: always initialize audio if audio properties present, independent of video validity
    if (meta.audio_sample_rate > 0 && m_audio_engine) {
        m_audio_engine->Init(meta);
    }

    // 2. Video setup: only process when video metadata is actually valid
    if (meta.video_codec != airplay::VideoCodec::Unknown && meta.video_width > 0 && meta.video_height > 0) {
        if (m_window) {
            m_window->UpdateStreamMetadata(meta);
        }

        const uint32_t w = meta.video_width;
        const uint32_t h = meta.video_height;
        const auto codec_type = (meta.video_codec == airplay::VideoCodec::H265)
            ? video::VideoCodecType::H265
            : video::VideoCodecType::H264;

        const bool is_ready = m_decoder_ready.load(std::memory_order_acquire);
        const bool same_config = is_ready &&
                                 (m_stream_width.load(std::memory_order_relaxed) == w) &&
                                 (m_stream_height.load(std::memory_order_relaxed) == h) &&
                                 (m_video_decoder && m_video_decoder->GetActiveCodec() == codec_type);

        if (!same_config) {
            if (m_video_decoder) {
                m_decoder_ready.store(false, std::memory_order_release);
                std::lock_guard<std::mutex> lock(m_decoder_mutex);
                m_video_decoder->Flush();
                if (m_video_decoder->Init(w, h, codec_type)) {
                    m_stream_width.store(w, std::memory_order_relaxed);
                    m_stream_height.store(h, std::memory_order_relaxed);
                    m_decoder_ready.store(true, std::memory_order_release);
                    DUWN_LOG_INFOF("App", "Video decoder ready for {}x{} ({})",
                        w, h, codec_type == video::VideoCodecType::H265 ? "HEVC" : "H.264");
                } else {
                    if (codec_type == video::VideoCodecType::H265) {
                        m_window->SetStatusText(ui::loc::Get(ui::loc::S::Video_HevcUnavailable));
                    } else {
                        m_window->SetStatusText(ui::loc::Get(ui::loc::S::Video_DecoderInitFailure));
                    }
                }
            }
        }

        if (m_preview_window) {
            m_preview_window->SetVideoGeometry(w, h);
        }

        // Update window title
        auto title = std::format(
            L"Duwn Mirror — {}×{}@{:.0f} | {} | {}",
            w, h, meta.video_fps,
            meta.video_codec == airplay::VideoCodec::H265 ? L"H.265" : L"H.264",
            m_d3d && m_d3d->IsHardware() ? L"HW Decode" : L"SW Decode");

        if (m_window) {
            m_window->SetTitle(title);
        }
    }
}



void App::OnFramePresent(video::VideoFrame& frame) noexcept {

    if (!m_renderer) return;

    if (m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WiredUsb)

        m_wired_reconnect_hint.store(false, std::memory_order_release);



    static std::atomic<bool> s_first_rendered{false};

    if (!s_first_rendered.exchange(true, std::memory_order_relaxed)) {

        DUWN_LOG_INFOF("Diagnostics",

            "FIRST EVENT: Video frame rendered to output window (swapchain {}x{})",

            m_renderer->SwapWidth(), m_renderer->SwapHeight());

    }



    GlobalMetrics().video_coded_width.store(frame.width, std::memory_order_relaxed);

    GlobalMetrics().video_coded_height.store(frame.height, std::memory_order_relaxed);

    GlobalMetrics().video_visible_width.store(frame.visible_width, std::memory_order_relaxed);

    GlobalMetrics().video_visible_height.store(frame.visible_height, std::memory_order_relaxed);



    // First presented frame / rotation handling for PreviewWindow and OutputWindow

    if (frame.visible_width > 0 && frame.visible_height > 0) {

        bool was_handled = m_session_first_frame_handled.exchange(true, std::memory_order_relaxed);

        if (!was_handled) {

            HWND main_hwnd = m_main_hwnd.load(std::memory_order_acquire);

            if (main_hwnd) {

                ::PostMessageW(main_hwnd, WM_DUWN_FIRST_FRAME,

                    static_cast<WPARAM>(frame.visible_width),

                    static_cast<LPARAM>(frame.visible_height));

            } else {

                if (m_output_window && m_settings.auto_open_output_window) {

                    m_output_window->ShowNoActivate();

                }

                if (m_preview_window) {
                    m_preview_window->SetVideoGeometry(frame.visible_width, frame.visible_height);
                }
                if (m_window) {
                    m_window->LayoutVideoSurface();
                }
            }

            m_last_preview_src_w = frame.visible_width;

            m_last_preview_src_h = frame.visible_height;

        } else {

            bool prev_is_land = (m_last_preview_src_w >= m_last_preview_src_h);

            bool curr_is_land = (frame.visible_width >= frame.visible_height);

            if (m_last_preview_src_w > 0 && m_last_preview_src_h > 0 && prev_is_land != curr_is_land) {

                if (m_preview_window) {

                    m_preview_window->OnStreamGeometryChanged(frame.visible_width, frame.visible_height);

                }

            }

            m_last_preview_src_w = frame.visible_width;

            m_last_preview_src_h = frame.visible_height;

        }

    }



    // Dynamically update output window aspect ratio to match visible video aperture

    // Only update when aperture is non-empty, orientation matches coded texture, and aspect has changed.

    // Require 2 consecutive matching frames or a format generation change to stabilize against transient MFT glitches.

    if (m_output_window && frame.visible_width > 0 && frame.visible_height > 0) {

        auto out_dims = ComputeCurrentOutputDimensions(frame.visible_width, frame.visible_height);

        uint32_t target_w = out_dims.width;

        uint32_t target_h = out_dims.height;



        bool is_new_aspect = (target_w != m_last_aspect_w || target_h != m_last_aspect_h);

        bool is_new_generation = (frame.format_generation != m_last_aspect_generation);



        if (is_new_aspect) {

            if (target_w == m_pending_aspect_w && target_h == m_pending_aspect_h) {

                ++m_pending_aspect_count;

            } else {

                m_pending_aspect_w     = target_w;

                m_pending_aspect_h     = target_h;

                m_pending_aspect_count = 1;

            }



            // Apply immediately on format generation change, or after 2 consecutive frames

            if (is_new_generation || m_pending_aspect_count >= 2) {

                m_output_window->SetCanvasSize(target_w, target_h);

                if (m_renderer) m_renderer->SignalResize(target_w, target_h);

                m_output_window->SetAspectRatio(target_w, target_h);

                if (m_renderer) {

                    m_renderer->SetAspectRatioMode(GetEffectiveAspectRatioMode());

                }

                m_last_aspect_w          = target_w;

                m_last_aspect_h          = target_h;

                m_last_aspect_generation = frame.format_generation;

                m_pending_aspect_count   = 0;

            }

        } else {

            m_pending_aspect_count = 0;

        }

    }



    const int64_t decoder_output_qpc = frame.process_output_qpc;

    const int64_t output_select_qpc  = frame.queue_pop_qpc;



    const bool skip_wait = (m_renderer && m_renderer->GetFrameLatencyWaitableObject() != nullptr);

    // Prepare pre-present export target so VideoProcessorBlt writes directly to shared ring buffer before Present() flips
    uint32_t active_ring_idx = 0;
    bool export_slot_valid = false;
    if (m_shared_texture && m_capture_server && m_capture_server->IsRunning() && m_d3d && m_renderer) {
        uint32_t sw = m_renderer->SwapWidth();
        uint32_t sh = m_renderer->SwapHeight();
        if (sw > 0 && sh > 0) {
            if (m_shared_texture->Width() != sw || m_shared_texture->Height() != sh) {
                m_shared_texture->Create(m_d3d->Device(), sw, sh);
            }
            LARGE_INTEGER freq{};
            ::QueryPerformanceFrequency(&freq);
            const int64_t lease_ticks = (freq.QuadPart * 250) / 1000; // 250ms lease timeout
            const int64_t now_ticks = clock::MonotonicClock::NowQpcTicks();
            uint32_t retired_slot = 0xFFFFFFFF;
            uint32_t cand_slot = m_capture_server->SelectNextAvailableSlot(
                m_shared_texture->RingSize(), m_last_export_ring_idx.load(std::memory_order_relaxed),
                now_ticks, lease_ticks, &retired_slot);
            if (retired_slot != 0xFFFFFFFF && retired_slot < m_shared_texture->RingSize()) {
                if (m_shared_texture->RecreateSlot(m_d3d->Device(), retired_slot)) {
                    m_capture_server->UpdateSharedHandle(retired_slot,
                        m_shared_texture->SharedHandle(retired_slot),
                        m_shared_texture->ResourceGeneration());
                    DUWN_LOG_INFOF("Capture", "Recreated retired texture slot {} after dead consumer (new gen={})",
                                   retired_slot, m_shared_texture->ResourceGeneration());
                }
            }
            if (cand_slot != 0xFFFFFFFF) {
                active_ring_idx = cand_slot;
                m_shared_texture->EnsureSlotReady(m_d3d->Context(), active_ring_idx);
                m_last_export_ring_idx.store(active_ring_idx, std::memory_order_relaxed);
                m_renderer->SetExportTarget(m_shared_texture->Texture(active_ring_idx), m_shared_texture->Query(active_ring_idx));
                export_slot_valid = true;
            } else {
                m_renderer->SetExportTarget(nullptr);
            }
        }
    }

    const video::PresentResult result = m_renderer->Present(frame, skip_wait);

    if (result == video::PresentResult::Ok) {
        if (!m_first_output_present_recorded.exchange(true, std::memory_order_relaxed)) {
            duwn::telemetry::ConnectionTimeline::Get().Record(
                duwn::telemetry::ConnectionMilestone::C13_FirstOutputPresent,
                std::format("{}x{} visible", frame.visible_width, frame.visible_height));
            DUWN_LOG_INFOF("Diagnostics",
                "FIRST EVENT: Output frame presented successfully ({}x{})",
                frame.visible_width, frame.visible_height);
        }

        const int64_t output_present_qpc = frame.present_end_qpc;
        telemetry::LatencyTelemetry::Get().RecordOutputFrameAge(
            decoder_output_qpc, output_select_qpc, output_present_qpc);

        // Export clean frame to SharedTexture & CaptureServer
        if (export_slot_valid && m_shared_texture && m_capture_server && m_capture_server->IsRunning() && m_d3d) {
            uint32_t sw = m_renderer->SwapWidth();
            uint32_t sh = m_renderer->SwapHeight();
            if (sw > 0 && sh > 0 && m_shared_texture->Texture(active_ring_idx) &&
                m_shared_texture->Width() == sw && m_shared_texture->Height() == sh) {
                if (m_shared_texture->SyncGpu(m_d3d->Context(), active_ring_idx)) {
                    const uint64_t new_idx = frame.sequence_number > 0 ? frame.sequence_number : (++m_export_frame_index);
                    m_export_frame_index.store(new_idx, std::memory_order_relaxed);
                    m_capture_server->PublishFrame(
                        m_shared_texture->SharedHandles(),
                        m_shared_texture->RingSize(),
                        active_ring_idx,
                        sw, sh,
                        DXGI_FORMAT_B8G8R8A8_UNORM,
                        m_shared_texture->ResourceGeneration(),
                        m_shared_texture->AdapterLuid(),
                        new_idx,
                        frame.present_end_qpc);
                } else {
                    m_capture_server->ClearReservation();
                    DUWN_LOG_WARNF("Capture", "SyncGpu timed out on ring slot {}: skipping publish to avoid corrupt frame", active_ring_idx);
                }
            } else {
                m_capture_server->ClearReservation();
            }
        }
    } else {
        if (m_capture_server) {
            m_capture_server->ClearReservation();
        }
    }

    if (m_preview_renderer) {
        const int64_t preview_select_qpc = clock::MonotonicClock::NowQpcTicks();
        const video::PresentResult prev_res = m_preview_renderer->Present(frame, true);
        const int64_t preview_present_qpc = clock::MonotonicClock::NowQpcTicks();

        if (prev_res == video::PresentResult::Ok) {
            if (!m_first_preview_present_recorded.exchange(true, std::memory_order_relaxed)) {
                duwn::telemetry::ConnectionTimeline::Get().Record(
                    duwn::telemetry::ConnectionMilestone::C12_FirstPreviewPresent,
                    std::format("{}x{} visible", frame.visible_width, frame.visible_height));
                DUWN_LOG_INFOF("Diagnostics",
                    "FIRST EVENT: Preview frame presented successfully ({}x{})",
                    frame.visible_width, frame.visible_height);
            }
            telemetry::LatencyTelemetry::Get().RecordPreviewSuccess(
                decoder_output_qpc, preview_select_qpc, preview_present_qpc);
        } else if (prev_res == video::PresentResult::Skipped) {
            telemetry::LatencyTelemetry::Get().RecordPreviewSkip();
        } else {
            telemetry::LatencyTelemetry::Get().RecordPreviewError();
        }
    }

    m_active_filter_caps.store(m_renderer->FilterCaps(), std::memory_order_relaxed);

    switch (result) {

    case video::PresentResult::Ok:

        m_av_sync.ReportVideoRender(frame.pts_ns, sync::MasterClock::NowNs());

        break;

    case video::PresentResult::DeviceLost:

        // HandleDeviceRemoved() releases resources cleanly (Milestone 0: no recovery).

        // Only PostFatalShutdown — UI thread owns m_running and PostQuitMessage.

        DUWN_LOG_ERRORF("App",

            "[FATAL] subsystem=App function=OnFramePresent operation=Present result=DeviceLost pts={} coded={}x{} visible={}x{} thread_id={}",

            frame.pts_ns, frame.width, frame.height, frame.visible_width, frame.visible_height, ::GetCurrentThreadId());

        m_renderer->HandleDeviceRemoved();

        if (m_preview_renderer) m_preview_renderer->HandleDeviceRemoved();

        PostFatalShutdown(1);

        break;

    case video::PresentResult::Fatal:

        // Only PostFatalShutdown — UI thread owns m_running and PostQuitMessage.

        DUWN_LOG_ERRORF("App",

            "[FATAL] subsystem=App function=OnFramePresent operation=Present result=Fatal pts={} coded={}x{} visible={}x{} thread_id={}",

            frame.pts_ns, frame.width, frame.height, frame.visible_width, frame.visible_height, ::GetCurrentThreadId());

        PostFatalShutdown(1);

        break;

    case video::PresentResult::Skipped:

        break;

    }

}

void App::TestMotionLoop(std::stop_token st) noexcept {
    while (!st.stop_requested() && !m_video_min_ready.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (st.stop_requested() || !m_d3d || !m_d3d->Device() || !m_scheduler) return;

    // High resolution timer period for precise 60.0 FPS pacing
    ::timeBeginPeriod(1);

    constexpr uint32_t kW = 1920;
    constexpr uint32_t kH = 1080;
    constexpr size_t kYSize = kW * kH;
    constexpr size_t kUvSize = kW * (kH / 2);
    constexpr size_t kTotalBytes = kYSize + kUvSize;

    std::vector<uint8_t> nv12(kTotalBytes, 128);
    std::fill(nv12.begin(), nv12.begin() + kYSize, static_cast<uint8_t>(28));

    constexpr size_t kPoolSize = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, kPoolSize> textures;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kW;
    desc.Height = kH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DECODER;

    for (size_t i = 0; i < kPoolSize; ++i) {
        HRESULT hr = m_d3d->Device()->CreateTexture2D(&desc, nullptr, textures[i].GetAddressOf());
        if (FAILED(hr) || !textures[i]) {
            DUWN_LOG_ERRORF("App", "TestMotionLoop failed to create NV12 texture hr={:#010x}", static_cast<unsigned>(hr));
            ::timeEndPeriod(1);
            return;
        }
    }

    uint64_t seq = 0;
    int box_x = 100;
    int box_y = 100;
    int dir_x = 14;
    int dir_y = 9;
    constexpr int kBoxW = 320;
    constexpr int kBoxH = 220;
    size_t tex_idx = 0;

    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    const int64_t interval_ticks = (freq.QuadPart * 1000) / 60000; // 16.6666 ms (60.0 FPS)
    LARGE_INTEGER start_qpc{};
    ::QueryPerformanceCounter(&start_qpc);
    int64_t next_frame_qpc = start_qpc.QuadPart;

    DUWN_LOG_INFO("App", "TestMotionLoop started streaming 1920x1080 synthetic motion frames @ 60.0 fps");

    while (!st.stop_requested() && m_running.load(std::memory_order_acquire)) {
        next_frame_qpc += interval_ticks;
        ++seq;
        box_x += dir_x;
        box_y += dir_y;
        if (box_x <= 20 || box_x + kBoxW >= static_cast<int>(kW) - 20) {
            dir_x = -dir_x;
            box_x = std::clamp(box_x, 20, static_cast<int>(kW) - kBoxW - 20);
        }
        if (box_y <= 20 || box_y + kBoxH >= static_cast<int>(kH) - 20) {
            dir_y = -dir_y;
            box_y = std::clamp(box_y, 20, static_cast<int>(kH) - kBoxH - 20);
        }

        std::fill(nv12.begin(), nv12.begin() + kYSize, static_cast<uint8_t>(24));

        for (int r = box_y; r < box_y + kBoxH; ++r) {
            uint8_t* row = nv12.data() + (r * kW);
            for (int c = box_x; c < box_x + kBoxW; ++c) {
                if (r == box_y || r == box_y + kBoxH - 1 || c == box_x || c == box_x + kBoxW - 1) {
                    row[c] = 245;
                } else if ((r + c + static_cast<int>(seq) * 4) % 32 < 16) {
                    row[c] = 210;
                } else {
                    row[c] = 95;
                }
            }
        }

        int scan_y = 1 + static_cast<int>((seq * 12) % (kH - 1));
        uint8_t* scan_row = nv12.data() + (scan_y * kW);
        std::fill(scan_row, scan_row + kW, static_cast<uint8_t>(235));

        // Encode 64-bit frame sequence into row 0 barcode only in explicit verification mode (--verify-capture)
        // High contrast (235=White, 16=Black) survives color matrix and scaling without bit error
        if (m_verify_capture) {
            uint8_t* row0 = nv12.data();
            for (int bit = 0; bit < 64; ++bit) {
                const bool bit_val = ((seq >> bit) & 1ULL) != 0;
                const uint8_t lum = bit_val ? 235 : 16;
                for (int px = 0; px < 8; ++px) {
                    row0[bit * 8 + px] = lum;
                }
            }
        }

        uint8_t* uv_base = nv12.data() + kYSize;
        uint8_t u_val = static_cast<uint8_t>((seq * 3) % 256);
        uint8_t v_val = static_cast<uint8_t>(255 - u_val);
        int uv_box_y = box_y / 2;
        int uv_box_h = kBoxH / 2;
        for (int r = uv_box_y; r < uv_box_y + uv_box_h; ++r) {
            uint8_t* uv_row = uv_base + (r * kW);
            for (int c = box_x; c < box_x + kBoxW; c += 2) {
                uv_row[c + 0] = u_val;
                uv_row[c + 1] = v_val;
            }
        }

        auto& current_tex = textures[tex_idx];
        tex_idx = (tex_idx + 1) % kPoolSize;

        {
            std::lock_guard lock{m_d3d->ContextMutex()};
            m_d3d->Context()->UpdateSubresource(
                current_tex.Get(), 0, nullptr, nv12.data(), kW, static_cast<UINT>(kTotalBytes));
        }

        video::VideoFrame vf;
        vf.texture = current_tex;
        vf.subresource = 0;
        vf.width = kW;
        vf.height = kH;
        vf.visible_width = kW;
        vf.visible_height = kH;
        vf.visible_x = 0;
        vf.visible_y = 0;
        vf.color_matrix = 2;
        vf.color_range = 1;
        vf.format = DXGI_FORMAT_NV12;
        vf.format_generation = 1;
        vf.sequence_number = seq;
        vf.present_end_qpc = clock::MonotonicClock::NowQpcTicks();

        m_scheduler->PushFrame(std::move(vf));

        // Precision monotonic sleep until next target frame time
        LARGE_INTEGER now_qpc{};
        ::QueryPerformanceCounter(&now_qpc);
        while (now_qpc.QuadPart < next_frame_qpc) {
            int64_t diff_ticks = next_frame_qpc - now_qpc.QuadPart;
            int64_t diff_ms = (diff_ticks * 1000) / freq.QuadPart;
            if (diff_ms > 2) {
                ::Sleep(1);
            } else {
                YieldProcessor();
            }
            ::QueryPerformanceCounter(&now_qpc);
        }
        if (now_qpc.QuadPart > next_frame_qpc + interval_ticks) {
            // Late: realign next_frame_qpc to current time to avoid burst
            next_frame_qpc = now_qpc.QuadPart;
        }
    }

    ::timeEndPeriod(1);
}

std::string App::GetActiveTransportString() const noexcept {
    const bool is_wired = m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WiredUsb;
    if (is_wired) return "WiredUsb";
    if (m_settings.transport_mode == TransportMode::DirectIpc && m_direct_ipc_active.load(std::memory_order_acquire)) {
        return "DirectIpc";
    }
    return "LocalRtpUdp";
}

StreamingPolicy App::GetActiveStreamingPolicy() const noexcept {
    if (m_scheduler) {
        return m_scheduler->GetStreamingPolicy();
    }
    return ResolveStreamingPolicy(m_settings.streaming_mode,
        m_settings.custom_video_freshness_ms, m_settings.custom_video_queue_frames);
}

void App::PublishMetadataSnapshot() noexcept {
    const bool prev_vis = m_preview_window ? m_preview_window->IsVisible() : false;
    m_meta_coord.PublishSnapshot(GetActiveTransportString(), GetActiveStreamingPolicy(), prev_vis);
}

SessionMetadataSnapshot App::GetMetadataSnapshot() const noexcept {
    return m_meta_coord.GetSnapshot();
}



void App::MetricsLoop(std::stop_token stop) noexcept {

    threading::SetCurrentThreadName(L"DUWN-Metrics");

    using namespace std::chrono_literals;

    const auto rate_delta = [](uint64_t current, uint64_t previous) noexcept {

        return current >= previous ? current - previous : uint64_t{0};

    };



    uint64_t prev_v_rtp       = 0;

    uint64_t prev_v_bytes     = 0;

    uint64_t prev_v_au        = 0;

    uint64_t prev_v_dec       = 0;

    uint64_t prev_v_rend      = 0;

    uint64_t prev_v_drop      = 0;

    uint64_t prev_v_late      = 0;



    uint64_t prev_ticks       = 0;

    uint64_t prev_unique_pres = 0;

    uint64_t prev_rep_ticks   = 0;

    uint64_t prev_superseded  = 0;

    uint64_t prev_disp_opp    = 0;



    uint64_t prev_a_rtp             = 0;

    uint64_t prev_a_bytes           = 0;

    uint64_t prev_a_underruns       = 0;

    uint64_t prev_silence_fill      = 0;

    uint64_t prev_a_real_underruns  = 0;



    uint64_t prev_au_sub      = 0;

    uint64_t prev_pi_calls    = 0;

    uint64_t prev_pi_succ     = 0;

    uint64_t prev_pi_fail     = 0;

    uint64_t prev_po_calls    = 0;

    uint64_t prev_po_succ     = 0;

    uint64_t prev_po_nmi      = 0;

    uint64_t prev_po_sc       = 0;

    uint64_t prev_po_fail     = 0;



    while (!stop.stop_requested()) {

        std::this_thread::sleep_for(1s);



        int64_t now_ns = clock::MonotonicClock::Now().time_since_epoch().count();

        if (m_airplay) {

            m_airplay->EvaluateSessionLiveness(now_ns);

        }



        auto& m = GlobalMetrics();



        // Video rates

        uint64_t cur_v_rtp   = m.video_rtp_packets.load(std::memory_order_relaxed);

        uint64_t cur_v_bytes = m.video_rtp_bytes.load(std::memory_order_relaxed);

        uint64_t cur_v_au    = m.video_access_units.load(std::memory_order_relaxed);

        uint64_t cur_v_dec   = m.video_decoded_frames.load(std::memory_order_relaxed);

        uint64_t cur_v_rend  = m.video_rendered_frames.load(std::memory_order_relaxed);

        uint64_t cur_v_drop  = m.video_dropped_frames.load(std::memory_order_relaxed);

        uint64_t cur_v_late  = m.video_late_frames.load(std::memory_order_relaxed);



        uint64_t v_rtp_rate  = rate_delta(cur_v_rtp, prev_v_rtp);

        double   v_kb_rate   = static_cast<double>(rate_delta(cur_v_bytes, prev_v_bytes)) / 1024.0;

        uint64_t v_au_rate   = rate_delta(cur_v_au, prev_v_au);

        uint64_t v_dec_fps   = rate_delta(cur_v_dec, prev_v_dec);

        uint64_t v_rend_fps  = rate_delta(cur_v_rend, prev_v_rend);

        uint64_t v_drop_rate = rate_delta(cur_v_drop, prev_v_drop);

        uint64_t v_late_rate = rate_delta(cur_v_late, prev_v_late);



        // Presentation rates

        uint64_t cur_ticks       = m.presentation_ticks.load(std::memory_order_relaxed);

        uint64_t cur_unique_pres = m.presentation_unique_frames.load(std::memory_order_relaxed);

        uint64_t cur_rep_ticks   = m.presentation_repeated_ticks.load(std::memory_order_relaxed);

        uint64_t cur_superseded  = m.video_decoded_superseded.load(std::memory_order_relaxed);

        uint64_t cur_disp_opp    = m.display_opportunities.load(std::memory_order_relaxed);



        uint64_t ticks_rate      = rate_delta(cur_ticks, prev_ticks);

        uint64_t unique_pres_fps = rate_delta(cur_unique_pres, prev_unique_pres);

        uint64_t rep_ticks_rate  = rate_delta(cur_rep_ticks, prev_rep_ticks);

        uint64_t superseded_rate = rate_delta(cur_superseded, prev_superseded);

        uint64_t disp_opp_rate   = rate_delta(cur_disp_opp, prev_disp_opp);



        prev_v_rtp   = cur_v_rtp;

        prev_v_bytes = cur_v_bytes;

        prev_v_au    = cur_v_au;

        prev_v_dec   = cur_v_dec;

        prev_v_rend  = cur_v_rend;

        prev_v_drop  = cur_v_drop;

        prev_v_late  = cur_v_late;



        prev_ticks       = cur_ticks;

        prev_unique_pres = cur_unique_pres;

        prev_rep_ticks   = cur_rep_ticks;

        prev_superseded  = cur_superseded;

        prev_disp_opp    = cur_disp_opp;



        // Audio rates

        uint64_t cur_a_rtp          = m.audio_rtp_packets.load(std::memory_order_relaxed);

        uint64_t cur_a_bytes        = m.audio_rtp_bytes.load(std::memory_order_relaxed);

        uint64_t cur_a_underruns    = m.audio_underruns.load(std::memory_order_relaxed);

        uint64_t cur_silence_fill   = m.audio_silence_fill_frames.load(std::memory_order_relaxed);

        uint64_t cur_real_underruns = m.audio_real_underruns.load(std::memory_order_relaxed);



        uint64_t a_rtp_rate         = rate_delta(cur_a_rtp, prev_a_rtp);

        double   a_kb_rate          = static_cast<double>(rate_delta(cur_a_bytes, prev_a_bytes)) / 1024.0;

        uint64_t a_underrun_rate    = rate_delta(cur_a_underruns, prev_a_underruns);

        uint64_t silence_fill_rate  = rate_delta(cur_silence_fill, prev_silence_fill);

        uint64_t real_underrun_rate = rate_delta(cur_real_underruns, prev_a_real_underruns);



        prev_a_rtp            = cur_a_rtp;

        prev_a_bytes          = cur_a_bytes;

        prev_a_underruns      = cur_a_underruns;

        prev_silence_fill     = cur_silence_fill;

        prev_a_real_underruns = cur_real_underruns;



        // Decoder pipeline rates

        uint64_t cur_au_sub    = m.video_access_units_submitted.load(std::memory_order_relaxed);

        uint64_t cur_pi_calls  = m.decoder_process_input_calls.load(std::memory_order_relaxed);

        uint64_t cur_pi_succ   = m.decoder_process_input_success.load(std::memory_order_relaxed);

        uint64_t cur_pi_fail   = m.decoder_process_input_failed.load(std::memory_order_relaxed);

        uint64_t cur_po_calls  = m.decoder_process_output_calls.load(std::memory_order_relaxed);

        uint64_t cur_po_succ   = m.decoder_process_output_success.load(std::memory_order_relaxed);

        uint64_t cur_po_nmi    = m.decoder_need_more_input.load(std::memory_order_relaxed);

        uint64_t cur_po_sc     = m.decoder_stream_change.load(std::memory_order_relaxed);

        uint64_t cur_po_fail   = m.decoder_output_failed.load(std::memory_order_relaxed);



        uint64_t au_sub_rate   = rate_delta(cur_au_sub, prev_au_sub);

        uint64_t pi_calls_rate = rate_delta(cur_pi_calls, prev_pi_calls);

        uint64_t pi_succ_rate  = rate_delta(cur_pi_succ, prev_pi_succ);

        uint64_t pi_fail_rate  = rate_delta(cur_pi_fail, prev_pi_fail);

        uint64_t po_calls_rate = rate_delta(cur_po_calls, prev_po_calls);

        uint64_t po_succ_rate  = rate_delta(cur_po_succ, prev_po_succ);

        uint64_t po_nmi_rate   = rate_delta(cur_po_nmi, prev_po_nmi);

        uint64_t po_sc_rate    = rate_delta(cur_po_sc, prev_po_sc);

        uint64_t po_fail_rate  = rate_delta(cur_po_fail, prev_po_fail);



        prev_au_sub   = cur_au_sub;

        prev_pi_calls = cur_pi_calls;

        prev_pi_succ  = cur_pi_succ;

        prev_pi_fail  = cur_pi_fail;

        prev_po_calls = cur_po_calls;

        prev_po_succ  = cur_po_succ;

        prev_po_nmi   = cur_po_nmi;

        prev_po_sc    = cur_po_sc;

        prev_po_fail  = cur_po_fail;



        // Resolutions and window

        uint32_t coded_w = m.video_coded_width.load(std::memory_order_relaxed);

        uint32_t coded_h = m.video_coded_height.load(std::memory_order_relaxed);

        uint32_t vis_w   = m.video_visible_width.load(std::memory_order_relaxed);

        uint32_t vis_h   = m.video_visible_height.load(std::memory_order_relaxed);

        int32_t  q_depth = m.video_queue_depth.load(std::memory_order_relaxed);



        uint32_t win_w = m_output_window ? m_output_window->ClientWidth() : 0;

        uint32_t win_h = m_output_window ? m_output_window->ClientHeight() : 0;

        uint32_t prev_w = 0;

        uint32_t prev_h = 0;

        if (m_preview_window && m_preview_window->Hwnd()) {

            RECT prc{};

            if (::GetClientRect(m_preview_window->Hwnd(), &prc)) {

                prev_w = static_cast<uint32_t>(prc.right - prc.left);

                prev_h = static_cast<uint32_t>(prc.bottom - prc.top);

            }

        }



        bool source_conn = (v_rtp_rate > 0 || a_rtp_rate > 0 || cur_v_rtp > 0);

        bool sidecar_up  = m_airplay ? m_airplay->IsSidecarAlive() : false;

        std::string_view phase_str = m_airplay ? airplay::PhaseString(m_airplay->CurrentPhase()) : "Unknown";



        // Stream lifecycle analysis: distinguish why video RTP is idle

        const char* stream_health = "streaming_active";

        if (v_rtp_rate == 0 && a_rtp_rate == 0) {

            if (!sidecar_up) {

                stream_health = "sidecar_exited";

            } else if (phase_str != "Streaming") {

                stream_health = "not_streaming (waiting_for_connection)";

            } else if (cur_v_rtp > 0) {

                // RTP was received in the past, but 0 now

                stream_health = "stream_idle (source_paused_or_media_timeout)";

            } else {

                stream_health = "no_udp_arrival (network/firewall_or_sender_inactive)";

            }

        }



        uint64_t cur_trans_drop = m.video_format_transition_drops.load(std::memory_order_relaxed);
        uint64_t cur_q_full_drop = m.video_queue_full_drops.load(std::memory_order_relaxed);
        uint64_t cur_gen         = m.video_format_generation.load(std::memory_order_relaxed);
        uint64_t cur_pres_late   = m.video_presentation_late_drops.load(std::memory_order_relaxed);
        uint64_t cur_q_overflow  = m.video_queue_overflow_drops.load(std::memory_order_relaxed);
        uint64_t cur_sess_q_full = m.session_q_full.load(std::memory_order_relaxed);
        uint64_t cur_malf        = m.network_malformed_packets.load(std::memory_order_relaxed);
        uint64_t cur_drop_min_rdy = m.video_dropped_min_ready.load(std::memory_order_relaxed);
        uint64_t cur_drop_ipc    = m.video_dropped_ipc_active.load(std::memory_order_relaxed);
        uint64_t cur_drop_dec_unrdy = m.video_dropped_decoder_not_ready.load(std::memory_order_relaxed);
        uint64_t cur_sched_rej   = m.video_scheduler_rejected_frames.load(std::memory_order_relaxed);

        static uint64_t s_metrics_cycle = 0;
        ++s_metrics_cycle;
        DUWN_LOG_INFOF("Diagnostics", "[METRICS CYCLE BEGIN] cycle={}", s_metrics_cycle);

        const SessionMetadataSnapshot meta_snap = GetMetadataSnapshot();
        const char* transport_str = meta_snap.active_transport.c_str();
        const char* stream_mode_str = "Balanced";
        switch (meta_snap.active_streaming_mode) {
        case StreamingMode::LowLatency: stream_mode_str = "Fastest"; break;
        case StreamingMode::Compatibility: stream_mode_str = "Smooth"; break;
        case StreamingMode::Custom: stream_mode_str = "Custom"; break;
        default: stream_mode_str = "Balanced"; break;
        }
        const auto active_policy = meta_snap.active_policy;
        const auto req_quality_sv = GetReceiverQualityName(meta_snap.req_receiver_quality);
        const auto act_quality_sv = GetReceiverQualityName(meta_snap.active_receiver_quality);
        const int pending_val = meta_snap.receiver_quality_pending ? 1 : 0;
        const char* codec_str = "Unknown";
        {
            std::lock_guard lock(m_decoder_mutex);
            if (m_video_decoder) {
                auto c = m_video_decoder->GetActiveCodec();
                codec_str = (c == video::VideoCodecType::H265) ? "HEVC" : (c == video::VideoCodecType::H264 ? "H264" : "Unknown");
            }
        }

        std::string commit_str = "unknown";
        if (DUWN_GIT_COMMIT_SHORT[0] != '\0' && std::string_view(DUWN_GIT_COMMIT_SHORT) != "unknown") {
            commit_str = DUWN_GIT_COMMIT_SHORT;
#if defined(DUWN_GIT_IS_DIRTY) && (DUWN_GIT_IS_DIRTY == 1)
            commit_str += "-dirty";
#endif
        }

        DUWN_LOG_INFOF("Diagnostics",
            "[METADATA] cycle={} | commit={} | transport={} | stream_mode={} | policy(max_q={}, res_ms={}, cad_pct={}, always_latest={}) | req_quality={} | active_quality={} | quality_pending={} | preview_visible={} | actual_stream(codec={}, res={}x{}, fps={:.2f})",
            s_metrics_cycle,
            commit_str,
            transport_str,
            stream_mode_str,
            active_policy.max_decoded_frames,
            active_policy.max_residence_ms,
            active_policy.cadence_percent,
            active_policy.always_latest ? 1 : 0,
            req_quality_sv,
            act_quality_sv,
            pending_val,
            meta_snap.preview_visible ? 1 : 0,
            codec_str,
            coded_w, coded_h,
            m.source_nominal_fps.load(std::memory_order_relaxed));

        DUWN_LOG_INFOF("Diagnostics",
            "[STATS] VIDEO: rtp={}/s ({:.1f} KB/s, malf={}) | au={}/s | dec={} fps | rend={} fps (unique={}, opp={}/s) | ticks={}/s (hold={}/s) | drop={}/s (superseded={}/s, late={}/s, min_rdy={}, ipc={}, dec_unrdy={}, late_drop={}, trans_drop={}, q_overflow={}, sess_q_full={}, life_q_full={}) | q={} | gen={} | coded={}x{} vis={}x{}",
            v_rtp_rate, v_kb_rate, cur_malf, v_au_rate, v_dec_fps, v_rend_fps, unique_pres_fps, disp_opp_rate, ticks_rate, rep_ticks_rate,
            v_drop_rate, superseded_rate, v_late_rate, cur_drop_min_rdy, cur_drop_ipc, cur_drop_dec_unrdy, cur_pres_late, cur_trans_drop, cur_q_overflow, cur_sess_q_full, cur_q_full_drop, q_depth, cur_gen, coded_w, coded_h, vis_w, vis_h);

        if (phase_str == "Streaming" || vis_w > 0) {
            DUWN_LOG_INFO("Diagnostics", m_source_quality_tracker.FormatTelemetryBlock());
        }



        DUWN_LOG_INFOF("Diagnostics",

            "[STATS] CADENCE: nominal_fps={:.2f} | pts_delta_p50={:.2f}ms (avg={:.2f}ms, p95={:.2f}ms) | jitter_p50={:.2f}ms p95={:.2f}ms | outliers={}",

            m.source_nominal_fps.load(std::memory_order_relaxed),

            m.video_pts_delta_p50_ms.load(std::memory_order_relaxed),

            m.video_pts_delta_avg_ms.load(std::memory_order_relaxed),

            m.video_pts_delta_p95_ms.load(std::memory_order_relaxed),

            m.source_jitter_p50_ms.load(std::memory_order_relaxed),

            m.source_jitter_p95_ms.load(std::memory_order_relaxed),

            m.source_outliers.load(std::memory_order_relaxed));



        DUWN_LOG_INFOF("Diagnostics",
            "[STATS] SCHEDULER: wake_error_avg={:.2f}ms p95={:.2f}ms | lateness_avg={:.2f}ms | dec_to_pres={:.2f}ms | q_age={:.2f}ms | rej_frames={}",
            m.video_wake_error_avg_ms.load(std::memory_order_relaxed),
            m.video_wake_error_p95_ms.load(std::memory_order_relaxed),
            m.video_schedule_lateness_avg_ms.load(std::memory_order_relaxed),
            m.video_decode_to_present_avg_ms.load(std::memory_order_relaxed),
            m.video_queue_age_avg_ms.load(std::memory_order_relaxed),
            cur_sched_rej);



        DUWN_LOG_INFOF("Diagnostics",
            "[STATS] PRESENT: interval_avg={:.2f}ms p95={:.2f}ms | call_avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms | dxgi_wait_avg={:.2f}ms | output(att={}, ok={}, skip={}, err={}) | preview(att={}, ok={}, skip={}, err={})",
            m.video_present_interval_avg_ms.load(std::memory_order_relaxed),
            m.video_present_interval_p95_ms.load(std::memory_order_relaxed),
            m.video_present_call_avg_ms.load(std::memory_order_relaxed),
            m.video_present_call_p50_ms.load(std::memory_order_relaxed),
            m.video_present_call_p95_ms.load(std::memory_order_relaxed),
            m.dxgi_wait_avg_ms.load(std::memory_order_relaxed),
            m.video_present_attempts.load(std::memory_order_relaxed),
            m.video_present_ok.load(std::memory_order_relaxed),
            m.video_present_skipped.load(std::memory_order_relaxed),
            m.video_present_errors.load(std::memory_order_relaxed),
            m.preview_present_attempts.load(std::memory_order_relaxed),
            m.preview_present_ok.load(std::memory_order_relaxed),
            m.preview_present_skipped.load(std::memory_order_relaxed),
            m.preview_present_errors.load(std::memory_order_relaxed));

        DUWN_LOG_INFOF("Diagnostics",
            "[STAGE LATENCY] decode: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms (n={}) | queue_res: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms (n={}) | dxgi_wait: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms max={:.2f}ms (n={}) | vp: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms (n={}) | present: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms (n={})",
            m.video_decode_time_ms.load(std::memory_order_relaxed),
            m.video_decode_p50_ms.load(std::memory_order_relaxed),
            m.video_decode_p95_ms.load(std::memory_order_relaxed),
            m.video_decode_sample_count.load(std::memory_order_relaxed),
            m.queue_residence_avg_ms.load(std::memory_order_relaxed),
            m.queue_residence_p50_ms.load(std::memory_order_relaxed),
            m.queue_residence_p95_ms.load(std::memory_order_relaxed),
            m.queue_residence_sample_count.load(std::memory_order_relaxed),
            m.dxgi_wait_avg_ms.load(std::memory_order_relaxed),
            m.dxgi_wait_p50_ms.load(std::memory_order_relaxed),
            m.dxgi_wait_p95_ms.load(std::memory_order_relaxed),
            m.dxgi_wait_max_ms.load(std::memory_order_relaxed),
            m.dxgi_wait_sample_count.load(std::memory_order_relaxed),
            m.vp_duration_avg_ms.load(std::memory_order_relaxed),
            m.vp_duration_p50_ms.load(std::memory_order_relaxed),
            m.vp_duration_p95_ms.load(std::memory_order_relaxed),
            m.vp_sample_count.load(std::memory_order_relaxed),
            m.present_duration_avg_ms.load(std::memory_order_relaxed),
            m.present_duration_p50_ms.load(std::memory_order_relaxed),
            m.present_duration_p95_ms.load(std::memory_order_relaxed),
            m.present_sample_count.load(std::memory_order_relaxed));



        DUWN_LOG_INFOF("Diagnostics",

            "[STATS] DECODER: au_sub={}/s | in_calls={}/s (ok={}, fail={}) | out_calls={}/s (ok={}, need_input={}, sc={}, fail={})",

            au_sub_rate, pi_calls_rate, pi_succ_rate, pi_fail_rate, po_calls_rate, po_succ_rate, po_nmi_rate, po_sc_rate, po_fail_rate);



        DUWN_LOG_INFOF("Diagnostics",

            "[STATS] AUDIO: rtp={}/s ({:.1f} KB/s) | codec=L16 (pt={}) | {}Hz -> {}Hz ({}ch) | buf={:.1f}ms | silence_fill={}/s (life={}) | real_underruns={}/s (total={}, legacy={}/s) | wasapi_run={} | resumes={} (last_gap={:.1f}ms)",

            a_rtp_rate, a_kb_rate, m.audio_payload_type.load(std::memory_order_relaxed),

            m.audio_input_rate.load(std::memory_order_relaxed),

            m.audio_output_rate.load(std::memory_order_relaxed),

            m.audio_channels.load(std::memory_order_relaxed),

            m.audio_buffer_ms.load(std::memory_order_relaxed),

            silence_fill_rate, cur_silence_fill,

            real_underrun_rate, cur_real_underruns, a_underrun_rate,

            m.wasapi_running.load(std::memory_order_relaxed),

            m.audio_resume_events.load(std::memory_order_relaxed),

            m.audio_silence_duration_ms.load(std::memory_order_relaxed));



        DUWN_LOG_INFOF("Diagnostics",

            "[STATS] SYNC/SESSION: A/V={:.1f}ms drift={:.2f}ms/min | sidecar={} | state={} | source={} | output={}x{} preview={}x{} | lifecycle={}",

            m.av_offset_ms.load(std::memory_order_relaxed),

            m.drift_ms_per_min.load(std::memory_order_relaxed),

            sidecar_up ? "alive" : "dead",

            phase_str,

            source_conn ? "connected" : "disconnected",

            win_w, win_h, prev_w, prev_h,

            stream_health);



        // Update and log fine-grained LatencyTelemetry (Phase C: T0-T7 stages)

        double ring_buf_ms = m_audio_ring ? ((m_audio_ring->Available() * 1000.0) / 48000.0) : 0.0;

        double wasapi_ms = m.audio_wasapi_padding_ms.load(std::memory_order_relaxed);

        telemetry::LatencyTelemetry::Get().UpdateAudioBuffering(ring_buf_ms, wasapi_ms);

        auto lat_snap = telemetry::LatencyTelemetry::Get().Snapshot();



        DUWN_LOG_INFOF("Diagnostics",

            "[LATENCY] T0-T7 (native receiver latency): total={:.2f}ms (T0-T1={:.2f}ms, T1-T2={:.2f}ms, T2-T3={:.2f}ms, T3-T4={:.2f}ms, T4-T5[q_age]={:.2f}ms, T5-T6[vp]={:.2f}ms, T6-T7[pres]={:.2f}ms) | audio_buffered={:.2f}ms (ring={:.2f}ms, wasapi_padding={:.2f}ms) | AV_skew=unavailable",

            lat_snap.total_video_pipeline_ms,

            lat_snap.t0_to_t1_ms,

            lat_snap.t1_to_t2_ms,

            lat_snap.t2_to_t3_ms,

            lat_snap.t3_to_t4_ms,

            lat_snap.t4_to_t5_ms,

            lat_snap.t5_to_t6_ms,

            lat_snap.t6_to_t7_ms,

            lat_snap.audio_buffered_ms,

            lat_snap.audio_ring_buffer_ms,

            lat_snap.audio_wasapi_padding_ms);

        telemetry::OutputFrameAgeStats out_age_stats;
        telemetry::PreviewFrameAgeStats prev_age_stats;
        telemetry::LatencyTelemetry::Get().GetFrameAgeStats(out_age_stats, prev_age_stats);

        if (out_age_stats.sample_count > 0) {
            DUWN_LOG_INFOF("Diagnostics",
                "[FRAME AGE OUTPUT] count={} | select: p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms | present: p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms",
                out_age_stats.sample_count,
                out_age_stats.age_at_select.p50, out_age_stats.age_at_select.p95, out_age_stats.age_at_select.p99, out_age_stats.age_at_select.max_val,
                out_age_stats.age_at_present.p50, out_age_stats.age_at_present.p95, out_age_stats.age_at_present.p99, out_age_stats.age_at_present.max_val);
        }

        if (prev_age_stats.sample_count > 0) {
            DUWN_LOG_INFOF("Diagnostics",
                "[FRAME AGE PREVIEW] count={} skips={} | select: p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms | select->pres: p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms | present: p50={:.2f}ms p95={:.2f}ms p99={:.2f}ms max={:.2f}ms",
                prev_age_stats.sample_count, prev_age_stats.skips,
                prev_age_stats.age_at_select.p50, prev_age_stats.age_at_select.p95, prev_age_stats.age_at_select.p99, prev_age_stats.age_at_select.max_val,
                prev_age_stats.select_to_present.p50, prev_age_stats.select_to_present.p95, prev_age_stats.select_to_present.p99, prev_age_stats.select_to_present.max_val,
                prev_age_stats.age_at_present.p50, prev_age_stats.age_at_present.p95, prev_age_stats.age_at_present.p99, prev_age_stats.age_at_present.max_val);
        }

        DUWN_LOG_INFOF("Diagnostics",
            "[AUDIO LATENCY] packet={:.2f}ms gap={:.2f}ms peak_gap={:.2f}ms A0-A1={:.3f} A1-A2={:.3f} A2-A3={:.3f} A3-A4={:.3f} A5-A6={:.3f}ms | ring={:.2f}ms target={:.2f}ms capacity={:.2f}ms | padding={:.2f}ms period={:.2f}ms stream_latency={:.2f}ms | servo={:.1f}ppm resampler_delay={:.3f}ms | underrun_frames={} overrun_frames={} backlog_drops={} recoveries={}",
            m.audio_packet_duration_ms.load(std::memory_order_relaxed),
            m.audio_arrival_gap_ms.load(std::memory_order_relaxed),
            m.audio_arrival_gap_max_ms.load(std::memory_order_relaxed),
            m.audio_a0_a1_ms.load(std::memory_order_relaxed),
            m.audio_a1_a2_ms.load(std::memory_order_relaxed),
            m.audio_a2_a3_ms.load(std::memory_order_relaxed),
            m.audio_a3_a4_ms.load(std::memory_order_relaxed),
            m.audio_a5_a6_ms.load(std::memory_order_relaxed),
            ring_buf_ms,
            m.audio_target_buffer_ms.load(std::memory_order_relaxed),
            m_audio_ring ? 1000.0 * m_audio_ring->Capacity() / 48000.0 : 0.0,
            wasapi_ms,
            m.audio_engine_period_ms.load(std::memory_order_relaxed),
            m.audio_stream_latency_ms.load(std::memory_order_relaxed),
            m.audio_servo_correction_ppm.load(std::memory_order_relaxed),
            m.audio_resampler_group_delay_ms.load(std::memory_order_relaxed),
            m.audio_underrun_frames.load(std::memory_order_relaxed),
            m.audio_ring_overrun_frames.load(std::memory_order_relaxed),
            m.audio_backlog_recovery_drops.load(std::memory_order_relaxed),
            m.audio_discontinuity_recoveries.load(std::memory_order_relaxed));



        int64_t total_decoded = static_cast<int64_t>(m.video_decoded_frames.load(std::memory_order_relaxed));

        int64_t total_presented = static_cast<int64_t>(m.video_rendered_frames.load(std::memory_order_relaxed));

        int64_t drop_catchup = static_cast<int64_t>(m.video_latency_catchup_drops.load(std::memory_order_relaxed));

        int64_t drop_overflow = static_cast<int64_t>(m.video_queue_overflow_drops.load(std::memory_order_relaxed));

        int64_t drop_format = static_cast<int64_t>(m.video_format_transition_drops.load(std::memory_order_relaxed));

        int64_t drop_stale = static_cast<int64_t>(m.video_stale_generation_drops.load(std::memory_order_relaxed));

        int64_t drop_late = static_cast<int64_t>(m.video_presentation_late_drops.load(std::memory_order_relaxed));

        int64_t cur_queue = static_cast<int64_t>(m.video_queue_depth.load(std::memory_order_relaxed));

        int64_t total_drops = drop_catchup + drop_overflow + drop_format + drop_stale + drop_late;

        int64_t inv_delta = total_decoded - (total_presented + total_drops + cur_queue);



        DUWN_LOG_INFOF("Diagnostics",

            "[INVARIANTS] decoded={} presented={} drops={} (catchup={}, overflow={}, format={}, stale={}, late={}) queue={} delta={} [{}]",

            total_decoded, total_presented, total_drops,

            drop_catchup, drop_overflow, drop_format, drop_stale, drop_late,

            cur_queue, inv_delta, (inv_delta == 0 ? "PASS" : "FAIL"));



        DUWN_LOG_INFOF("Diagnostics",

            "[BURST] <1ms: {} | 1-3ms: {} | 3-8ms: {} | 8-14ms: {} | 14-20ms: {} | >20ms: {} | bursts: 2f={} 3f={} max={} | gap p50={:.2f}ms p95={:.2f}ms (last={:.2f}ms)",

            m.decode_gap_lt_1ms.load(std::memory_order_relaxed),

            m.decode_gap_1_3ms.load(std::memory_order_relaxed),

            m.decode_gap_3_8ms.load(std::memory_order_relaxed),

            m.decode_gap_8_14ms.load(std::memory_order_relaxed),

            m.decode_gap_14_20ms.load(std::memory_order_relaxed),

            m.decode_gap_gt_20ms.load(std::memory_order_relaxed),

            m.burst_2_frames.load(std::memory_order_relaxed),

            m.burst_3_frames.load(std::memory_order_relaxed),

            m.burst_max.load(std::memory_order_relaxed),

            m.decode_output_gap_p50.load(std::memory_order_relaxed),

            m.decode_output_gap_p95.load(std::memory_order_relaxed),

            m.last_decode_output_gap_ms.load(std::memory_order_relaxed));



        DUWN_LOG_INFOF("Diagnostics",

            "[LATENCY] queue_residence: avg={:.2f}ms p95={:.2f}ms | VP: avg={:.2f}ms p95={:.2f}ms | Present: avg={:.2f}ms p95={:.2f}ms | total_pipeline: avg={:.2f}ms p95={:.2f}ms",

            m.queue_residence_avg_ms.load(std::memory_order_relaxed),

            m.queue_residence_p95_ms.load(std::memory_order_relaxed),

            m.vp_duration_avg_ms.load(std::memory_order_relaxed),

            m.vp_duration_p95_ms.load(std::memory_order_relaxed),

            m.present_duration_avg_ms.load(std::memory_order_relaxed),

            m.present_duration_p95_ms.load(std::memory_order_relaxed),

            m.total_pipeline_avg_ms.load(std::memory_order_relaxed),

            m.total_pipeline_p95_ms.load(std::memory_order_relaxed));



        DUWN_LOG_INFOF("Diagnostics",

            "[DXGI] ready_signals={} | interval: avg={:.2f}ms p50={:.2f}ms p95={:.2f}ms max={:.2f}ms | queue_depth: p50={:.1f} p95={:.1f} max={}",

            m.dxgi_ready_signals.load(std::memory_order_relaxed),

            m.dxgi_ready_interval_avg_ms.load(std::memory_order_relaxed),

            m.dxgi_ready_interval_p50_ms.load(std::memory_order_relaxed),

            m.dxgi_ready_interval_p95_ms.load(std::memory_order_relaxed),

            m.dxgi_ready_interval_max_ms.load(std::memory_order_relaxed),

            m.queue_depth_p50.load(std::memory_order_relaxed),

            m.queue_depth_p95.load(std::memory_order_relaxed),

            m.queue_depth_max.load(std::memory_order_relaxed));



        if (meta_snap.active_transport == "DirectIpc") {

            DUWN_LOG_INFOF("Diagnostics",

                "[STATS] TRANSPORT_IPC: consumed={} (drops={}) | transport_latency={:.2f}ms",

                m.ipc_frames_consumed.load(std::memory_order_relaxed),

                m.ipc_producer_dropped.load(std::memory_order_relaxed),

                m.ipc_transport_latency_ms.load(std::memory_order_relaxed));

        }



        double client_fps_report = m.client_fps.load(std::memory_order_relaxed);

        if (client_fps_report > 0.0) {

            DUWN_LOG_INFOF("Diagnostics",

                "[STATS] AIRPLAY_CLIENT_REPORT: fps={:.2f} | drops={} | total_frames={}",

                client_fps_report,

                m.client_dropped_frames.load(std::memory_order_relaxed),

                m.client_total_frames.load(std::memory_order_relaxed));

        } else if (phase_str == "Streaming" && (cur_v_rtp > 300)) {

            static uint32_t s_fpsdata_warn_count = 0;

            if (++s_fpsdata_warn_count % 30 == 1) { // warn every ~30s

                DUWN_LOG_WARN("Diagnostics",

                    "[WARN] UxPlay -FPSdata: no client performance report parsed yet (client_fps=0.0)");

            }

        }

        DUWN_LOG_INFOF("Diagnostics", "[METRICS CYCLE END] cycle={}", s_metrics_cycle);

        // Drift update

        m_drift.Update(m.av_offset_ms.load(std::memory_order_relaxed));



        // Update Modern Direct2D Control Window UI Telemetry

        static int64_t s_uptime_seconds = 0;

        s_uptime_seconds++;

        double lag_ms = m.total_pipeline_avg_ms.load(std::memory_order_relaxed);

        if (lag_ms <= 0.0) {

            lag_ms = m.video_decode_to_present_avg_ms.load(std::memory_order_relaxed);

        }

        if (lag_ms <= 0.0) {

            lag_ms = m.video_decode_time_ms.load(std::memory_order_relaxed) + 16.6;

        }

        uint32_t active_q = m_scheduler ? static_cast<uint32_t>(m_scheduler->DecodedQueueSize()) : 0;

        if (m_window) {

            bool audio_run = m.wasapi_running.load(std::memory_order_relaxed);

            bool audio_mut = m_wasapi ? m_wasapi->IsMuted() : false;

            m_window->UpdateTelemetry(

                static_cast<double>(v_rend_fps),

                lag_ms > 0.0 ? lag_ms : 0.0,

                active_q,

                cur_v_drop,

                s_uptime_seconds

            );

            auto cinfo = m_airplay

                ? m_airplay->CurrentClientInfo() : airplay::AirPlayClientInfo{};

            m_window->UpdateExtendedTelemetry(

                cur_v_rtp,

                cur_a_rtp,

                cur_real_underruns,

                audio_run,

                audio_mut,

                cinfo.peer_address,

                cinfo.device_name

            );

            uint32_t cap_w = m_output_window ? m_output_window->CanvasWidth() : 0;

            uint32_t cap_h = m_output_window ? m_output_window->CanvasHeight() : 0;

            if (cap_w == 0 || cap_h == 0) {

                auto out_dims = ComputeCurrentOutputDimensions(vis_w, vis_h, &meta_snap);

                cap_w = out_dims.width;

                cap_h = out_dims.height;

            }

            auto& state = m_window->State();

            state.width = vis_w;

            state.height = vis_h;

            state.capture_width = cap_w;

            state.capture_height = cap_h;

            state.preview_width = prev_w;

            state.preview_height = prev_h;

            state.output_width = cap_w;

            state.output_height = cap_h;

            state.preview_visible = m_preview_window ? m_preview_window->IsVisible() : false;

            state.output_window_visible = m_output_window ? (::IsWindowVisible(m_output_window->Hwnd()) != 0) : false;

            state.nominal_fps = m.source_nominal_fps.load(std::memory_order_relaxed);

            state.config_generation = m_meta_coord.GetConfigGeneration();

            state.sidecar_generation = m_meta_coord.GetSidecarGeneration();

            state.receiver_quality_pending = meta_snap.receiver_quality_pending;

            state.coded_width = coded_w;

            state.coded_height = coded_h;

            state.video_bitrate_mbps = (v_kb_rate * 8.0) / 1000.0;
            state.media_bitrate_mbps = state.video_bitrate_mbps;
            state.sidecar_pid = m_airplay ? static_cast<uint32_t>(m_airplay->GetSidecarPid()) : 0;

            video::RequestedReceiverEnvelope req_env;
            req_env.width = meta_snap.req_receiver_width;
            req_env.height = meta_snap.req_receiver_height;
            req_env.fps = meta_snap.req_receiver_fps;
            req_env.preset_name = std::string(GetReceiverQualityName(meta_snap.req_receiver_quality));
            req_env.is_original = (meta_snap.req_receiver_quality == ReceiverQuality::Original_60);
            m_source_quality_tracker.SetRequestedEnvelope(req_env);

            auto eff = m_source_quality_tracker.GetEffectiveness();
            state.quality_effectiveness = static_cast<int>(eff);
            switch (eff) {
            case video::QualityEffectiveness::DeliveredAsRequested:
                state.quality_effectiveness_raw = L"DELIVERED_AS_REQUESTED";
                state.quality_state_desc = ui::loc::Get(ui::loc::S::Video_Quality_DeliveredAsRequested);
                break;
            case video::QualityEffectiveness::SourceLimited:
                state.quality_effectiveness_raw = L"SOURCE_LIMITED";
                state.quality_state_desc = ui::loc::Get(ui::loc::S::Video_Quality_SourceLimited);
                break;
            case video::QualityEffectiveness::PartiallyDelivered:
                state.quality_effectiveness_raw = L"PARTIALLY_DELIVERED";
                state.quality_state_desc = ui::loc::Get(ui::loc::S::Video_Quality_PartiallyDelivered);
                break;
            default:
                state.quality_effectiveness_raw = L"UNKNOWN";
                state.quality_state_desc = L"—";
                break;
            }

            if (vis_w > 0 && vis_h > 0) {

                state.orientation_desc = (vis_w >= vis_h) ? L"Landscape" : L"Portrait";

                state.actual_source_desc = std::format(L"{} × {} @ {:.0f} FPS", vis_w, vis_h,

                    state.nominal_fps > 0.0 ? state.nominal_fps : 60.0);

            } else {

                state.orientation_desc = L"—";

                state.actual_source_desc = L"—";

            }

            state.gpu_name = m_d3d ? m_d3d->AdapterName() : L"—";

            state.source_fps = static_cast<double>(v_au_rate);

            state.decoded_fps = static_cast<double>(v_dec_fps);

            state.render_fps = static_cast<double>(v_rend_fps);

            const int decoder_kind = m.video_decoder_kind.load(std::memory_order_relaxed);

            state.decoder_name = decoder_kind == 1 ? L"Hardware Media Foundation H.264 Decoder"

                               : decoder_kind == 2 ? L"Software Media Foundation H.264 Decoder" : L"—";

            state.zero_copy = m.video_decoder_zero_copy.load(std::memory_order_relaxed);

            const int backend = m_active_renderer.load(std::memory_order_relaxed);

            state.renderer_name = backend == 1 ? L"D3D11 Hardware"

                                : backend == 2 ? L"D3D11 WARP" : L"—";

            state.scaler_name = !vis_w ? L"—" : backend == 1 ? L"D3D11 VideoProcessor"

                              : backend == 2 ? L"D3D11 shader" : L"—";

            state.color_processing_name = !vis_w ? L"—" : backend == 1

                ? (m_active_filter_caps.load(std::memory_order_relaxed) ? L"Hardware filters" : L"VideoProcessor")

                : backend == 2 ? L"Shader" : L"—";

            const bool wired_mode = m_connection_mode.load(std::memory_order_acquire) == ConnectionMode::WiredUsb;

            state.connection_mode = wired_mode ? 1 : 0;

            state.transport_name = wired_mode ? L"AirPlay over Apple USB Ethernet"

                : meta_snap.active_transport == "DirectIpc" ? L"Direct IPC" : L"Local RTP/UDP";

            if (wired_mode) {

                state.wired_needs_mirroring_reconnect = m_wired_reconnect_hint.load(std::memory_order_acquire);

                HWND hwnd = m_main_hwnd.load(std::memory_order_acquire);

                if (hwnd) ::PostMessageW(hwnd, WM_DUWN_WIRED_REFRESH, 0, 0);



                state.control_state = m_wired_control.GetState();

                auto cm = m_wired_control.GetMetrics();

                state.control_events_sent = cm.events_sent;

                state.control_events_failed = cm.events_failed;

                state.control_latency_avg_ms = cm.latency_avg_ms;

                state.control_latency_p50_ms = cm.latency_p50_ms;

                state.control_latency_p95_ms = cm.latency_p95_ms;

                state.control_latency_p99_ms = cm.latency_p99_ms;

                auto dev = m_wired_control.GetDeviceInfo();

                if (!dev.device_name.empty()) state.control_device_name = dev.device_name;

                if (!dev.ios_version.empty()) state.control_ios_version = dev.ios_version;



            }

            constexpr uint32_t filter_bits[] = {1u, 2u, 8u, 4u, 16u};

            const uint32_t filter_caps = m_active_filter_caps.load(std::memory_order_relaxed);

            for (size_t i = 0; i < 5; ++i)

                state.filter_supported[i] = (filter_caps & filter_bits[i]) != 0;



            // Audio device state

            state.audio_buffer_ms      = m.audio_buffer_ms.load(std::memory_order_relaxed);

            state.audio_underrun_count = cur_real_underruns;

            state.audio_device_id      = meta_snap.monitor_device_id;

            state.resolved_audio_device_name = m_wasapi

                ? m_wasapi->ResolvedDeviceName() : L"—";

            // audio_device_name is updated live by SetOnAudioDeviceChanged; keep stable here.

        }

    }

}



video::PipelineTier App::DeterminePipelineTier() const noexcept {

    if (!m_d3d || !m_d3d->IsHardware() || m_d3d->GetAdapterInfo().is_warp) {

        return video::PipelineTier::Emergency;

    }

    if (m_video_decoder && m_video_decoder->GetDecoderInfo().is_hardware &&

        m_video_decoder->GetDecoderInfo().is_d3d11_aware) {

        return video::PipelineTier::FullPerformance;

    }

    return video::PipelineTier::Compatibility;

}



void App::LogCapabilityReport() const noexcept {

    if (!m_d3d) return;



    const auto& ai = m_d3d->GetAdapterInfo();

    const auto tier = DeterminePipelineTier();



    auto wide_to_utf8 = [](std::wstring_view w) -> std::string {

        if (w.empty()) return {};

        int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);

        if (size <= 0) return {};

        std::string s(static_cast<size_t>(size), '\0');

        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);

        return s;

    };



    const char* fl_str = "Unknown";

    switch (ai.feature_level) {

    case D3D_FEATURE_LEVEL_11_1: fl_str = "11.1 (0xb100)"; break;

    case D3D_FEATURE_LEVEL_11_0: fl_str = "11.0 (0xb000)"; break;

    case D3D_FEATURE_LEVEL_10_1: fl_str = "10.1 (0xa100)"; break;

    case D3D_FEATURE_LEVEL_10_0: fl_str = "10.0 (0xa000)"; break;

    }



    DUWN_LOG_INFO("Capability", "============================================================");

    DUWN_LOG_INFO("Capability", "[CAPABILITY REPORT] Duwn Mirror Hardware & Media Pipeline");

    DUWN_LOG_INFO("Capability", "------------------------------------------------------------");

    DUWN_LOG_INFOF("Capability", "GPU ADAPTER:        {}", wide_to_utf8(ai.description));

    DUWN_LOG_INFOF("Capability", "  Vendor ID:        {:#06x} ({}) [Device ID: {:#06x}]",

        ai.vendor_id, m_d3d->VendorName(), ai.device_id);

    DUWN_LOG_INFOF("Capability", "  Driver Type:      {}", ai.is_warp ? "WARP (Software Rasterizer)" : "Hardware");

    DUWN_LOG_INFOF("Capability", "  Feature Level:    D3D_FEATURE_LEVEL_{}", fl_str);

    DUWN_LOG_INFOF("Capability", "  Dedicated VRAM:   {} MB", static_cast<unsigned long long>(ai.dedicated_video_mem / (1024 * 1024)));

    DUWN_LOG_INFOF("Capability", "  Shared System:    {} MB", static_cast<unsigned long long>(ai.shared_system_mem / (1024 * 1024)));

    DUWN_LOG_INFOF("Capability", "  Video Processor:  {}", ai.has_video_support ? "Supported" : "Unsupported");

    DUWN_LOG_INFOF("Capability", "  Direct Outputs:   {}", ai.has_outputs ? "Yes" : "No (Hybrid/DWM Presentation)");

    DUWN_LOG_INFO("Capability", "------------------------------------------------------------");



    if (m_video_decoder) {

        const auto& di = m_video_decoder->GetDecoderInfo();

        DUWN_LOG_INFOF("Capability", "H.264 DECODER:      {}", wide_to_utf8(di.name));

        DUWN_LOG_INFOF("Capability", "  Decoder Type:     {}", di.is_hardware ? "Hardware" : "Software");

        DUWN_LOG_INFOF("Capability", "  D3D11-Aware:      {}", di.is_d3d11_aware ? "Yes" : "No");

        DUWN_LOG_INFOF("Capability", "  Output Format:    DXGI_FORMAT_NV12");

        DUWN_LOG_INFOF("Capability", "  Zero-Copy Status: {}", di.is_zero_copy ? "Yes (Direct GPU Texture)" : "No (System Memory Upload)");

    } else {

        DUWN_LOG_INFO("Capability", "H.264 DECODER:      Not initialized");

    }

    bool hevc_hw = video::MFVideoDecoder::IsCodecSupported(video::VideoCodecType::H265, true);
    bool hevc_sw = video::MFVideoDecoder::IsCodecSupported(video::VideoCodecType::H265, false);
    DUWN_LOG_INFOF("Capability", "HEVC DECODER:       {}",
        hevc_hw ? "Supported (Hardware Zero-Copy)" :
        (hevc_sw ? "Software Only (Non-Zero-Copy / Not Allowed in Prod)" : "Not Installed (HEVC Video Extension Missing)"));

    DUWN_LOG_INFO("Capability", "------------------------------------------------------------");

    DUWN_LOG_INFOF("Capability", "PIPELINE PERFORMANCE TIER:  >>> {} <<<", video::PipelineTierName(tier));

    DUWN_LOG_INFO("Capability", "============================================================");

}



} // namespace duwn::app
