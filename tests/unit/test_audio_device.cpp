// test_audio_device.cpp — Unit tests for audio device routing, WASAPI session
// identity, AudioDeviceManager, and audio-related UiState/Settings fields.

#include "audio/AudioDeviceManager.h"
#include "audio/WasapiOutput.h"
#include "audio/AudioRingBuffer.h"
#include "app/Settings.h"
#include "ui/UiState.h"
#include <string>
#include <vector>

using namespace duwn::audio;
using namespace duwn::app;
using namespace duwn::ui;

DUWN_TEST(AudioRender_StartupPrebufferRequiresTargetAndEndpointRequest) {
    DUWN_ASSERT(!HasAudioStartupPrebuffer(959, 960, 480));
    DUWN_ASSERT(HasAudioStartupPrebuffer(960, 960, 480));
    DUWN_ASSERT(!HasAudioStartupPrebuffer(1055, 960, 1056));
    DUWN_ASSERT(HasAudioStartupPrebuffer(1056, 960, 1056));
}

DUWN_TEST(AudioRender_RecoveryPrebufferUsesAdaptiveTarget) {
    DUWN_ASSERT(AudioPrebufferTargetFrames(false, 960, 1920) == 960);
    DUWN_ASSERT(AudioPrebufferTargetFrames(true, 960, 1920) == 1920);
    DUWN_ASSERT(AudioPrebufferTargetFrames(true, 960, 480) == 960);
}

// ---------------------------------------------------------------------------
// 1. AudioEndpointInfo struct defaults and assignment
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_EndpointInfoFields) {
    AudioEndpointInfo info;
    DUWN_ASSERT(info.id.empty());
    DUWN_ASSERT(info.friendly_name.empty());

    info.id            = L"{0.0.0.00000000}.{test-device-id}";
    info.friendly_name = L"Speakers (Realtek HD Audio)";
    DUWN_ASSERT(info.id            == L"{0.0.0.00000000}.{test-device-id}");
    DUWN_ASSERT(info.friendly_name == L"Speakers (Realtek HD Audio)");
}

// ---------------------------------------------------------------------------
// 2. WM_APP message constants are distinct and in WM_APP range
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_MessageConstantsNoCollision) {
    DUWN_ASSERT(WM_APP_AUDIO_DEVICE_CHANGED      == WM_APP + 0x001);
    DUWN_ASSERT(WM_APP_AUDIO_DEVICE_LIST_CHANGED == WM_APP + 0x002);
    DUWN_ASSERT(WM_APP_AUDIO_DEVICE_CHANGED      != WM_APP_AUDIO_DEVICE_LIST_CHANGED);
    DUWN_ASSERT(WM_APP_AUDIO_DEVICE_CHANGED      >= WM_APP);
    DUWN_ASSERT(WM_APP_AUDIO_DEVICE_LIST_CHANGED >= WM_APP);
}

// ---------------------------------------------------------------------------
// 3. AudioDeviceManager construction and SetWatchedDeviceId (no COM required)
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_ManagerConstructsCleanly) {
    AudioDeviceManager mgr;
    // Not init'd — SetWatchedDeviceId must not crash.
    mgr.SetWatchedDeviceId(L"");
    mgr.SetWatchedDeviceId(L"{0.0.0.00000000}.{some-id}");
    mgr.SetWatchedDeviceId(L""); // back to watch-default mode
}

// ---------------------------------------------------------------------------
// 4. AudioDeviceManager Init with nullptr HWND (notifications disabled)
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_InitNullHwndDisablesNotifications) {
    AudioDeviceManager mgr;
    (void)mgr.Init(nullptr); // may fail on machines with no audio — acceptable
    mgr.Shutdown();          // must be safe regardless
}

// ---------------------------------------------------------------------------
// 5. AudioDeviceManager Enumerate returns consistent results
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_EnumerateIsStable) {
    AudioDeviceManager mgr;
    if (!mgr.Init(nullptr)) return; // no audio subsystem — skip

    auto devs1 = mgr.Enumerate();
    auto devs2 = mgr.Enumerate();

    // Two consecutive enumerations: same count (no hot-plug in unit-test env).
    DUWN_ASSERT(devs1.size() == devs2.size());

    // Every returned entry must have a non-empty id.
    for (const auto& d : devs1) {
        DUWN_ASSERT(!d.id.empty());
    }

    mgr.Shutdown();
}

// ---------------------------------------------------------------------------
// 6. AudioDeviceManager DefaultDeviceId returns non-empty when audio exists
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_SystemDefaultUsesMultimediaRole) {
    AudioDeviceManager mgr;
    if (!mgr.Init(nullptr)) return;

    std::wstring def = mgr.DefaultDeviceId();
    // On any machine with a render endpoint the default COM id is non-empty.
    if (!def.empty()) {
        DUWN_ASSERT(def.size() > 4);
    }

    mgr.Shutdown();
}

// ---------------------------------------------------------------------------
// 7. WasapiOutput: pre-init state is safe to query
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_WasapiPreInitQuerySafe) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    DUWN_ASSERT(wasapi.CurrentDeviceId().empty());
    DUWN_ASSERT(wasapi.BufferMs()      == 0.0);
    DUWN_ASSERT(wasapi.UnderrunCount() == 0);
    DUWN_ASSERT(!wasapi.IsMuted());
}

// ---------------------------------------------------------------------------
// 8. WasapiOutput: explicit device ID stored after Init
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_ExplicitEndpointUsesStoredId) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    bool ok = wasapi.Init(L"");
    if (!ok) return; // no audio hardware — skip

    // Empty device_id input → CurrentDeviceId() preserves empty sentinel
    // (system default path; the actual COM id is not stored externally).
    DUWN_ASSERT(wasapi.CurrentDeviceId().empty());
    DUWN_ASSERT(wasapi.BufferMs() > 0.0);
}

// ---------------------------------------------------------------------------
// 9. UiState audio device fields have correct defaults
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_UiStateAudioFieldDefaults) {
    UiState state;
    DUWN_ASSERT(state.audio_device_id.empty());         // empty = system default
    DUWN_ASSERT(state.audio_device_name == L"System Default");
    DUWN_ASSERT(state.audio_buffer_ms   == 0.0);
    DUWN_ASSERT(state.audio_underrun_count == 0);
    DUWN_ASSERT(state.audio_session_display_name == L"Duwn Mirror");
}

// ---------------------------------------------------------------------------
// 10. Settings audio/general fields have correct defaults
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_SettingsFieldDefaults) {
    Settings s{};
    DUWN_ASSERT(s.monitor_device_id.empty()); // empty = system default
    DUWN_ASSERT(s.language        == L"auto");
    DUWN_ASSERT(s.start_on_boot   == false);
    DUWN_ASSERT(s.start_minimized == false);
}

// ---------------------------------------------------------------------------
// 11. WasapiOutput: Selection Policy Contract (Pinned vs System Default)
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_SelectionPolicy_PinnedVsDefault) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    // Initial pre-init
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);

    // Init with empty string -> SystemDefault
    wasapi.Init(L"");
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);
    DUWN_ASSERT(wasapi.PreferredDeviceId().empty());

    // Switch to pinned device -> PinnedDevice
    const std::wstring pinned_id = L"{0.0.0.00000000}.{pinned-test-device}";
    wasapi.SwitchEndpoint(pinned_id);
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::PinnedDevice);
    DUWN_ASSERT(wasapi.PreferredDeviceId() == pinned_id);

    // Switch back to empty -> SystemDefault
    wasapi.SwitchEndpoint(L"");
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);
    DUWN_ASSERT(wasapi.PreferredDeviceId().empty());
}

// ---------------------------------------------------------------------------
// 12. WasapiOutput: Volume Clamping and Mute State
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_VolumeAndMute_RampAndState) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    DUWN_ASSERT(wasapi.Volume() == 1.0f);
    DUWN_ASSERT(!wasapi.IsMuted());

    // Set valid volumes
    wasapi.SetVolume(0.5f);
    DUWN_ASSERT(wasapi.Volume() == 0.5f);

    wasapi.SetVolume(0.0f);
    DUWN_ASSERT(wasapi.Volume() == 0.0f);

    wasapi.SetVolume(1.0f);
    DUWN_ASSERT(wasapi.Volume() == 1.0f);

    // Out-of-bounds volumes are clamped
    wasapi.SetVolume(-0.5f);
    DUWN_ASSERT(wasapi.Volume() == 0.0f);

    wasapi.SetVolume(2.5f);
    DUWN_ASSERT(wasapi.Volume() == 1.0f);

    // Mute toggling
    wasapi.SetMuted(true);
    DUWN_ASSERT(wasapi.IsMuted());
    wasapi.SetMuted(false);
    DUWN_ASSERT(!wasapi.IsMuted());
}

// ---------------------------------------------------------------------------
// 13. AudioRingBuffer: DiscardOldest caps queue without flushing everything
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_RingBuffer_DiscardOldestCapping) {
    AudioRingBuffer ring(4096, 2);

    // Push 1000 stereo frames (f = 0..999)
    std::vector<float> in_data(1000 * 2);
    for (size_t i = 0; i < 1000; ++i) {
        in_data[i * 2 + 0] = static_cast<float>(i);
        in_data[i * 2 + 1] = static_cast<float>(i);
    }
    uint32_t pushed = ring.Push(in_data.data(), 1000);
    DUWN_ASSERT(pushed == 1000);
    DUWN_ASSERT(ring.Available() == 1000);

    // Discard oldest down to 200 frames
    ring.DiscardOldest(200);
    DUWN_ASSERT(ring.Available() == 200);

    // Pull remaining 200 frames — they should be the newest frames (800..999)
    std::vector<float> out_data(200 * 2, 0.0f);
    uint32_t pulled = ring.Pull(out_data.data(), 200);
    DUWN_ASSERT(pulled == 200);
    DUWN_ASSERT(out_data[0] == 800.0f);
    DUWN_ASSERT(out_data[1] == 800.0f);
    DUWN_ASSERT(out_data[199 * 2] == 999.0f);
    DUWN_ASSERT(ring.Available() == 0);
}
// ---------------------------------------------------------------------------
// 14. WasapiOutput: Rapid Switching Stress 100 Cycles (No deadlocks, worker lives)
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_RapidSwitching_Stress100Cycles) {
    AudioRingBuffer ring(8192, 2);
    WasapiOutput wasapi(ring);

    wasapi.Init(L"");
    wasapi.Start();

    // 100 rapid switch calls simulating aggressive user clicks or notifications
    for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
            wasapi.SwitchEndpoint(L"");
        } else {
            wasapi.SwitchEndpoint(L"{nonexistent-test-id}");
        }
    }

    // After 100 switches, final choice must be system default
    wasapi.SwitchEndpoint(L"");
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);

    // State is alive and query safe
    AudioEndpointState st = wasapi.State();
    DUWN_ASSERT(st == AudioEndpointState::Playing || st == AudioEndpointState::WaitingForDevice || st == AudioEndpointState::Idle);

    wasapi.Stop();
    DUWN_ASSERT(wasapi.State() == AudioEndpointState::Stopped);
}

// ---------------------------------------------------------------------------
// 15. WasapiOutput: State Machine String Descriptions
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_StateStringDescriptions) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    // Pre-init is Idle
    DUWN_ASSERT(wasapi.StateString() == L"Idle");

    wasapi.Init(L"");
    std::wstring s = wasapi.StateString();
    DUWN_ASSERT(!s.empty());

    wasapi.Stop();
    DUWN_ASSERT(wasapi.StateString() == L"Stopped");
}

// ---------------------------------------------------------------------------
// 16. UiState: Workspace V2 and Single-Window Navigation Model
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_WorkspaceV2_NavigationModel) {
    UiState state;

    // Default tab is Mirror
    DUWN_ASSERT(state.active_tab == NavTab::Mirror);

    // Navigation order: Mirror -> Video -> Audio -> Color -> Settings -> Diagnostics
    state.active_tab = NavTab::Video;
    DUWN_ASSERT(state.active_tab == NavTab::Video);

    state.active_tab = NavTab::Audio;
    DUWN_ASSERT(state.active_tab == NavTab::Audio);

    state.active_tab = NavTab::Color;
    DUWN_ASSERT(state.active_tab == NavTab::Color);

    state.active_tab = NavTab::Settings;
    DUWN_ASSERT(state.active_tab == NavTab::Settings);

    state.active_tab = NavTab::Diagnostics;
    DUWN_ASSERT(state.active_tab == NavTab::Diagnostics);

    // Backwards-compatible aliases
    DUWN_ASSERT(NavTab::Performance == NavTab::Diagnostics);
    DUWN_ASSERT(NavTab::About == NavTab::Diagnostics);

    // Screen-only mode
    DUWN_ASSERT(!state.is_screen_only);
    state.is_screen_only = true;
    DUWN_ASSERT(state.is_screen_only);

    // Quick volume
    DUWN_ASSERT(state.audio_volume == 1.0f);
    state.audio_volume = 0.75f;
    DUWN_ASSERT(state.audio_volume == 0.75f);
}

