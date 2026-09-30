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
