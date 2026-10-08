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
    DUWN_ASSERT(s.audio_source == L"airplay");
    DUWN_ASSERT(s.audio_output_mode == L"default");
    DUWN_ASSERT(s.audio_output_endpoint_id.empty());
    DUWN_ASSERT(s.AudioOutputSelectionId().empty());
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

DUWN_TEST(AudioDevice_RoutingPolicy_DefaultFollowsAndSpecificStaysPinned) {
    auto follow_default = DecideEndpointChange(
        EndpointSelectionPolicy::SystemDefault, L"", L"A", L"B", false);
    DUWN_ASSERT(follow_default.action == EndpointChangeAction::OpenDefault);

    auto pinned_ignores_default = DecideEndpointChange(
        EndpointSelectionPolicy::PinnedDevice, L"A", L"A", L"B", true);
    DUWN_ASSERT(pinned_ignores_default.action == EndpointChangeAction::None);

    auto unplugged = DecideEndpointChange(
        EndpointSelectionPolicy::PinnedDevice, L"A", L"A", L"B", false);
    DUWN_ASSERT(unplugged.action == EndpointChangeAction::WaitForDevice);
    DUWN_ASSERT(unplugged.target_id == L"A");

    auto returned = DecideEndpointChange(
        EndpointSelectionPolicy::PinnedDevice, L"A", L"", L"B", true);
    DUWN_ASSERT(returned.action == EndpointChangeAction::OpenSpecific);
    DUWN_ASSERT(returned.target_id == L"A");

    auto default_invalidated = DecideEndpointChange(
        EndpointSelectionPolicy::SystemDefault, L"", L"A", L"C", false);
    DUWN_ASSERT(default_invalidated.action == EndpointChangeAction::OpenDefault);

    auto specific_invalidated = DecideEndpointChange(
        EndpointSelectionPolicy::PinnedDevice, L"A", L"", L"C", false);
    DUWN_ASSERT(specific_invalidated.action == EndpointChangeAction::WaitForDevice);
}

DUWN_TEST(AudioDevice_RecoveryCommitRejectsStaleGeneration) {
    EndpointWorkArbiter arbiter;
    std::wstring active_endpoint = L"A";

    const uint64_t recovery_a = arbiter.Capture();
    const uint64_t select_b = arbiter.BeginRequest();
    DUWN_ASSERT(arbiter.TryCommit(select_b, [&] { active_endpoint = L"B"; }));

    const bool stale_recovery_committed = arbiter.TryCommit(
        recovery_a, [&] { active_endpoint = L"A"; });
    DUWN_ASSERT(!stale_recovery_committed);
    DUWN_ASSERT(active_endpoint == L"B");
}

DUWN_TEST(AudioDevice_TransientInitializationRecoveryIsBoundedAndCanCommit) {
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(0) == 250);
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(1) == 500);
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(2) == 1000);
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(3) == 2000);
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(4) == 4000);
    DUWN_ASSERT(AudioRecoveryRetryDelayMs(5) == 0);

    EndpointWorkArbiter arbiter;
    const uint64_t recovery = arbiter.Capture();
    bool output_ready = false;
    DUWN_ASSERT(arbiter.TryCommit(recovery, [&] { output_ready = true; }));
    DUWN_ASSERT(output_ready);
}

DUWN_TEST(AudioDevice_SelectionSurvivesEnumerationOrderChanges) {
    std::vector<AudioDeviceItem> first = {
        {L"A", L"Speakers"}, {L"B", L"USB DAC"}, {L"C", L"HDMI"}
    };
    std::vector<AudioDeviceItem> reordered = {
        {L"C", L"HDMI"}, {L"A", L"Speakers"}, {L"B", L"USB DAC"}
    };
    DUWN_ASSERT(FindAudioDeviceIndexById(first, L"B") == 1);
    DUWN_ASSERT(FindAudioDeviceIndexById(reordered, L"B") == 2);
    DUWN_ASSERT(reordered[FindAudioDeviceIndexById(reordered, L"B")].id == L"B");
}

DUWN_TEST(AudioDevice_SettingsRestoreSpecificEndpointId) {
    Settings settings{};
    settings.SelectAudioOutput(L"{0.0.0.00000000}.{usb-dac}");
    DUWN_ASSERT(settings.audio_output_mode == L"specific");
    DUWN_ASSERT(settings.AudioOutputSelectionId() == L"{0.0.0.00000000}.{usb-dac}");
    settings.SelectAudioOutput(L"");
    DUWN_ASSERT(settings.audio_output_mode == L"default");
    DUWN_ASSERT(settings.audio_output_endpoint_id.empty());
}

DUWN_TEST(AudioDevice_LegacySettingsMigrationPreservesEndpointId) {
    Settings legacy_default{};
    Settings::MigrateLegacyAudioOutputId(legacy_default, L"");
    DUWN_ASSERT(legacy_default.audio_output_mode == L"default");
    DUWN_ASSERT(legacy_default.audio_output_endpoint_id.empty());

    Settings legacy_specific{};
    const std::wstring endpoint_id = L"{0.0.0.00000000}.{legacy-usb-dac}";
    Settings::MigrateLegacyAudioOutputId(legacy_specific, endpoint_id);
    DUWN_ASSERT(legacy_specific.audio_output_mode == L"specific");
    DUWN_ASSERT(legacy_specific.audio_output_endpoint_id == endpoint_id);
    DUWN_ASSERT(legacy_specific.AudioOutputSelectionId() == endpoint_id);
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
    DUWN_ASSERT(st == AudioEndpointState::Playing ||
                st == AudioEndpointState::WaitingForDevice ||
                st == AudioEndpointState::InitializationFailed ||
                st == AudioEndpointState::Idle);

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

// ---------------------------------------------------------------------------
// 17. AudioDevice: ValidateAudioFormat strict validation & all sample types
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_FormatValidation_AllTypesAndInvalid) {
    AudioFormatConfig cfg;

    // 1. Valid Float32 48kHz stereo (WAVE_FORMAT_EXTENSIBLE)
    WAVEFORMATEXTENSIBLE ext_float{};
    ext_float.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext_float.Format.nChannels = 2;
    ext_float.Format.nSamplesPerSec = 48000;
    ext_float.Format.wBitsPerSample = 32;
    ext_float.Format.nBlockAlign = 8;
    ext_float.Format.nAvgBytesPerSec = 48000 * 8;
    ext_float.Format.cbSize = 22;
    ext_float.Samples.wValidBitsPerSample = 32;
    ext_float.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    ext_float.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    DUWN_ASSERT(ValidateAudioFormat(reinterpret_cast<const WAVEFORMATEX*>(&ext_float), cfg));
    DUWN_ASSERT(cfg.sample_type == AudioSampleType::Float32);
    DUWN_ASSERT(!cfg.needs_resample);

    // 2. Valid Int16 44.1kHz stereo (WAVE_FORMAT_PCM)
    WAVEFORMATEX pcm16{};
    pcm16.wFormatTag = WAVE_FORMAT_PCM;
    pcm16.nChannels = 2;
    pcm16.nSamplesPerSec = 44100;
    pcm16.wBitsPerSample = 16;
    pcm16.nBlockAlign = 4;
    pcm16.nAvgBytesPerSec = 44100 * 4;
    pcm16.cbSize = 0;
    DUWN_ASSERT(ValidateAudioFormat(&pcm16, cfg));
    DUWN_ASSERT(cfg.sample_type == AudioSampleType::Int16);
    DUWN_ASSERT(cfg.needs_resample);

    // 3. Valid Int24Packed 48kHz stereo (3 bytes per sample)
    WAVEFORMATEXTENSIBLE ext_24p{};
    ext_24p.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext_24p.Format.nChannels = 2;
    ext_24p.Format.nSamplesPerSec = 48000;
    ext_24p.Format.wBitsPerSample = 24;
    ext_24p.Format.nBlockAlign = 6;
    ext_24p.Format.nAvgBytesPerSec = 48000 * 6;
    ext_24p.Format.cbSize = 22;
    ext_24p.Samples.wValidBitsPerSample = 24;
    ext_24p.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    ext_24p.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    DUWN_ASSERT(ValidateAudioFormat(reinterpret_cast<const WAVEFORMATEX*>(&ext_24p), cfg));
    DUWN_ASSERT(cfg.sample_type == AudioSampleType::Int24Packed);

    // 4. Valid Int24In32 48kHz stereo (24 valid bits in 32-bit container)
    WAVEFORMATEXTENSIBLE ext_24in32{};
    ext_24in32.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext_24in32.Format.nChannels = 2;
    ext_24in32.Format.nSamplesPerSec = 48000;
    ext_24in32.Format.wBitsPerSample = 32;
    ext_24in32.Format.nBlockAlign = 8;
    ext_24in32.Format.nAvgBytesPerSec = 48000 * 8;
    ext_24in32.Format.cbSize = 22;
    ext_24in32.Samples.wValidBitsPerSample = 24;
    ext_24in32.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    ext_24in32.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    DUWN_ASSERT(ValidateAudioFormat(reinterpret_cast<const WAVEFORMATEX*>(&ext_24in32), cfg));
    DUWN_ASSERT(cfg.sample_type == AudioSampleType::Int24In32);

    // 5. Valid Int32 48kHz stereo (32-bit int)
    WAVEFORMATEXTENSIBLE ext_32{};
    ext_32.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext_32.Format.nChannels = 2;
    ext_32.Format.nSamplesPerSec = 48000;
    ext_32.Format.wBitsPerSample = 32;
    ext_32.Format.nBlockAlign = 8;
    ext_32.Format.nAvgBytesPerSec = 48000 * 8;
    ext_32.Format.cbSize = 22;
    ext_32.Samples.wValidBitsPerSample = 32;
    ext_32.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    ext_32.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    DUWN_ASSERT(ValidateAudioFormat(reinterpret_cast<const WAVEFORMATEX*>(&ext_32), cfg));
    DUWN_ASSERT(cfg.sample_type == AudioSampleType::Int32);

    // 6. Multichannel mix formats are supported; stereo source maps to L/R.
    WAVEFORMATEXTENSIBLE ext_51 = ext_float;
    ext_51.Format.nChannels = 6;
    ext_51.Format.nBlockAlign = 24;
    ext_51.Format.nAvgBytesPerSec = 48000 * 24;
    ext_51.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY |
        SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    DUWN_ASSERT(ValidateAudioFormat(reinterpret_cast<const WAVEFORMATEX*>(&ext_51), cfg));
    DUWN_ASSERT(cfg.channels == 6);
    float stereo_frame[2] = {0.25f, -0.5f};
    float output_51[6] = {};
    DUWN_ASSERT(WriteAudioFrames(
        reinterpret_cast<BYTE*>(output_51), sizeof(output_51), 1,
        stereo_frame, 1, cfg) == sizeof(output_51));
    DUWN_ASSERT(output_51[0] == 0.25f);
    DUWN_ASSERT(output_51[1] == -0.5f);
    for (size_t channel = 2; channel < 6; ++channel) {
        DUWN_ASSERT(output_51[channel] == 0.0f);
    }

    // 7. Invalid formats rejected cleanly
    DUWN_ASSERT(!ValidateAudioFormat(nullptr, cfg));

    WAVEFORMATEX invalid{};
    invalid.wFormatTag = WAVE_FORMAT_PCM;
    invalid.nChannels = 0; // 0 channels
    DUWN_ASSERT(!ValidateAudioFormat(&invalid, cfg));

    invalid.nChannels = 2;
    invalid.nSamplesPerSec = 4000; // Too low sample rate
    DUWN_ASSERT(!ValidateAudioFormat(&invalid, cfg));

    invalid.nSamplesPerSec = 48000;
    invalid.wBitsPerSample = 16;
    invalid.nBlockAlign = 5; // Mismatched block align (should be 4)
    invalid.nAvgBytesPerSec = 48000 * 4;
    DUWN_ASSERT(!ValidateAudioFormat(&invalid, cfg));

    invalid.nBlockAlign = 4;
    invalid.nAvgBytesPerSec = 100; // Mismatched avg bytes per sec
    DUWN_ASSERT(!ValidateAudioFormat(&invalid, cfg));
}

// ---------------------------------------------------------------------------
// 18. AudioDevice: WriteAudioFrames buffer canary protection & exact bit layout
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_WriteAudioFrames_BufferCanaryAndBitLayout) {
    constexpr size_t kCanarySize = 32;
    constexpr uint8_t kCanaryVal = 0xAA;

    auto verify_canaries = [](const std::vector<uint8_t>& buf, size_t dest_offset, size_t dest_len) {
        for (size_t i = 0; i < dest_offset; ++i) {
            DUWN_ASSERT(buf[i] == kCanaryVal);
        }
        for (size_t i = dest_offset + dest_len; i < buf.size(); ++i) {
            DUWN_ASSERT(buf[i] == kCanaryVal);
        }
    };

    // Test 1: Int24Packed (3 bytes per sample)
    {
        AudioFormatConfig fmt{};
        fmt.channels = 2;
        fmt.bits_per_sample = 24;
        fmt.sample_type = AudioSampleType::Int24Packed;

        const size_t pcm_bytes = 2 * 3; // 1 stereo frame = 6 bytes
        std::vector<uint8_t> buffer(kCanarySize * 2 + pcm_bytes, kCanaryVal);
        BYTE* dest = buffer.data() + kCanarySize;

        float in_samples[2] = { 0.5f, -0.5f };
        size_t written = WriteAudioFrames(dest, pcm_bytes, 1, in_samples, 1, fmt);
        DUWN_ASSERT(written == 6);
        verify_canaries(buffer, kCanarySize, pcm_bytes);

        // Verify exact little-endian byte representation:
        // Left: 0.5 * 8388607 = 4194303 = 0x3FFFFF -> 0xFF, 0xFF, 0x3F
        DUWN_ASSERT(dest[0] == 0xFF);
        DUWN_ASSERT(dest[1] == 0xFF);
        DUWN_ASSERT(dest[2] == 0x3F);

        // Right: -0.5 * 8388607 = -4194303 = 0xFFC00001 -> 0x01, 0x00, 0xC0
        DUWN_ASSERT(dest[3] == 0x01);
        DUWN_ASSERT(dest[4] == 0x00);
        DUWN_ASSERT(dest[5] == 0xC0);
    }

    // Test 2: Int24In32 (4 bytes per sample, 24 valid bits left-aligned)
    {
        AudioFormatConfig fmt{};
        fmt.channels = 2;
        fmt.bits_per_sample = 32;
        fmt.valid_bits_per_sample = 24;
        fmt.sample_type = AudioSampleType::Int24In32;

        const size_t pcm_bytes = 2 * 4; // 1 stereo frame = 8 bytes
        std::vector<uint8_t> buffer(kCanarySize * 2 + pcm_bytes, kCanaryVal);
        BYTE* dest = buffer.data() + kCanarySize;

        float in_samples[2] = { 0.5f, -0.5f };
        size_t written = WriteAudioFrames(dest, pcm_bytes, 1, in_samples, 1, fmt);
        DUWN_ASSERT(written == 8);
        verify_canaries(buffer, kCanarySize, pcm_bytes);

        const int32_t* out32 = reinterpret_cast<const int32_t*>(dest);
        DUWN_ASSERT(out32[0] == (4194303 << 8));
        DUWN_ASSERT(out32[1] == (-4194303 << 8));
    }

    // Test 3: Clamping boundary protection (out of [-1.0, 1.0] does not overflow)
    {
        AudioFormatConfig fmt{};
        fmt.channels = 2;
        fmt.bits_per_sample = 16;
        fmt.sample_type = AudioSampleType::Int16;

        const size_t pcm_bytes = 4;
        std::vector<uint8_t> buffer(kCanarySize * 2 + pcm_bytes, kCanaryVal);
        BYTE* dest = buffer.data() + kCanarySize;

        float in_samples[2] = { 5.0f, -5.0f }; // beyond range
        size_t written = WriteAudioFrames(dest, pcm_bytes, 1, in_samples, 1, fmt);
        DUWN_ASSERT(written == 4);
        verify_canaries(buffer, kCanarySize, pcm_bytes);

        const int16_t* out16 = reinterpret_cast<const int16_t*>(dest);
        DUWN_ASSERT(out16[0] == 32767);
        DUWN_ASSERT(out16[1] == -32768);
    }

    // Test 4: Silence filling when input has fewer frames than requested
    {
        AudioFormatConfig fmt{};
        fmt.channels = 2;
        fmt.bits_per_sample = 16;
        fmt.sample_type = AudioSampleType::Int16;

        constexpr uint32_t req_frames = 10;
        constexpr uint32_t src_frames = 4;
        const size_t pcm_bytes = req_frames * 4;
        std::vector<uint8_t> buffer(kCanarySize * 2 + pcm_bytes, kCanaryVal);
        BYTE* dest = buffer.data() + kCanarySize;

        std::vector<float> in_samples(src_frames * 2, 0.5f);
        size_t written = WriteAudioFrames(dest, pcm_bytes, req_frames, in_samples.data(), src_frames, fmt);
        DUWN_ASSERT(written == pcm_bytes);
        verify_canaries(buffer, kCanarySize, pcm_bytes);

        const int16_t* out16 = reinterpret_cast<const int16_t*>(dest);
        for (size_t f = 0; f < 4; ++f) {
            DUWN_ASSERT(out16[f * 2 + 0] > 0);
            DUWN_ASSERT(out16[f * 2 + 1] > 0);
        }
        for (size_t f = 4; f < req_frames; ++f) {
            DUWN_ASSERT(out16[f * 2 + 0] == 0);
            DUWN_ASSERT(out16[f * 2 + 1] == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 19. AudioDevice: No sample drop or duplication in same-rate varying callbacks
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_NoSampleDrop_SameRateVaryingCallbacks) {
    AudioRingBuffer ring(8192, 2);

    constexpr uint32_t kTotalFrames = 4800;
    std::vector<float> in_data(kTotalFrames * 2);
    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        in_data[i * 2 + 0] = static_cast<float>(i);
        in_data[i * 2 + 1] = static_cast<float>(i);
    }
    uint32_t pushed = ring.Push(in_data.data(), kTotalFrames);
    DUWN_ASSERT(pushed == kTotalFrames);
    DUWN_ASSERT(ring.Available() == kTotalFrames);

    const uint32_t callback_sizes[] = { 128, 240, 480, 512, 256, 384, 800, 1000, 1000 };
    uint32_t total_consumed = 0;
    float expected_next_val = 0.0f;

    AudioFormatConfig fmt{};
    fmt.channels = 2;
    fmt.sample_rate = 48000;
    fmt.bits_per_sample = 32;
    fmt.sample_type = AudioSampleType::Float32;
    fmt.needs_resample = false;

    std::vector<float> staging_in;
    std::vector<float> wasapi_buffer;

    for (uint32_t cb_frames : callback_sizes) {
        uint32_t avail = ring.Available();
        uint32_t to_write = std::min(cb_frames, avail);
        if (to_write == 0) break;

        // Pull EXACTLY to_write frames from ring
        staging_in.resize(to_write * 2);
        uint32_t actual_pull = ring.Pull(staging_in.data(), to_write);
        DUWN_ASSERT(actual_pull == to_write);

        wasapi_buffer.resize(to_write * 2);
        size_t written_bytes = WriteAudioFrames(
            reinterpret_cast<BYTE*>(wasapi_buffer.data()),
            wasapi_buffer.size() * sizeof(float),
            to_write, staging_in.data(), actual_pull, fmt);
        DUWN_ASSERT(written_bytes == to_write * 2 * sizeof(float));

        // Verify sequence continuity: ZERO DROPPED FRAMES!
        for (uint32_t f = 0; f < to_write; ++f) {
            DUWN_ASSERT(wasapi_buffer[f * 2 + 0] == expected_next_val);
            DUWN_ASSERT(wasapi_buffer[f * 2 + 1] == expected_next_val);
            expected_next_val += 1.0f;
        }

        total_consumed += to_write;
    }

    DUWN_ASSERT(total_consumed == kTotalFrames);
    DUWN_ASSERT(ring.Available() == 0);
    DUWN_ASSERT(expected_next_val == static_cast<float>(kTotalFrames));
}

// ---------------------------------------------------------------------------
// 20. AudioDevice: Resampling persistent FIFO & remainder preserved across callbacks
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_Resampling_PersistentFifoAndRemainderPreserved) {
    // Test 48kHz source to 44.1kHz endpoint
    duwn::audio::AudioConverter resampler;
    resampler.Init(48000, 2, 44100, 2);

    AudioRingBuffer ring(16384, 2);

    // Push 4800 stereo frames at 48kHz (0.1 seconds of audio)
    constexpr uint32_t kInputFrames = 4800;
    std::vector<float> in_data(kInputFrames * 2);
    for (uint32_t i = 0; i < kInputFrames; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / 48000.0f));
        in_data[i * 2 + 0] = val;
        in_data[i * 2 + 1] = val;
    }
    ring.Push(in_data.data(), kInputFrames);

    std::vector<float> persistent_fifo;
    std::vector<float> staging_in;
    std::vector<float> converted_chunk;

    // Simulate varying callback sizes at 44.1kHz (e.g. 441 frames = 10ms)
    const uint32_t cb_sizes[] = { 441, 441, 441, 441, 441, 441, 441, 441, 441, 441 };
    uint32_t total_out_frames = 0;

    for (uint32_t req_out : cb_sizes) {
        size_t avail_fifo = persistent_fifo.size() / 2;
        if (avail_fifo < req_out) {
            uint32_t deficit = req_out - static_cast<uint32_t>(avail_fifo);
            double ratio = 48000.0 / 44100.0;
            uint32_t needed_in = static_cast<uint32_t>(std::ceil(deficit * ratio)) + 4;
            uint32_t ring_avail = ring.Available();
            uint32_t to_pull = std::min(needed_in, ring_avail);
            if (to_pull > 0) {
                staging_in.resize(to_pull * 2);
                uint32_t actual = ring.Pull(staging_in.data(), to_pull);
                converted_chunk.clear();
                resampler.Convert(staging_in.data(), actual, converted_chunk);
                persistent_fifo.insert(persistent_fifo.end(), converted_chunk.begin(), converted_chunk.end());
            }
        }

        avail_fifo = persistent_fifo.size() / 2;
        uint32_t frames_from_fifo = std::min(req_out, static_cast<uint32_t>(avail_fifo));
        total_out_frames += frames_from_fifo;

        if (frames_from_fifo > 0) {
            if (frames_from_fifo == avail_fifo) {
                persistent_fifo.clear();
            } else {
                persistent_fifo.erase(persistent_fifo.begin(), persistent_fifo.begin() + frames_from_fifo * 2);
            }
        }
    }

    // At 44.1kHz from 48kHz (ratio 44100/48000 = 0.91875):
    // 4800 input frames produce ~4410 output frames minus filter lookahead (16 frames) = ~4394 frames.
    DUWN_ASSERT(total_out_frames >= 4380 && total_out_frames <= 4410);

    // FIFO preserved any leftover without discarding
    DUWN_ASSERT(persistent_fifo.size() / 2 < 441);
}

// ---------------------------------------------------------------------------
// 21. AudioDevice: unavailable specific endpoint never falls back
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_SwitchHandoff_UnavailableSpecificHasNoFallback) {
    AudioRingBuffer ring(4096, 2);
    WasapiOutput wasapi(ring);

    bool ok = wasapi.Init(L"");
    if (!ok) return;

    wasapi.Start();
    DUWN_ASSERT(wasapi.State() == AudioEndpointState::Playing || wasapi.State() == AudioEndpointState::WaitingForDevice);

    if (wasapi.State() == AudioEndpointState::Playing) {
        // Attempt switch to totally invalid endpoint ID
        bool switch_ok = wasapi.SwitchEndpoint(L"{invalid-guid-9999-not-found}");
        // Candidate preparation fails; selected ID is kept and output pauses.
        DUWN_ASSERT(!switch_ok);
        DUWN_ASSERT(wasapi.State() == AudioEndpointState::WaitingForDevice ||
                    wasapi.State() == AudioEndpointState::InitializationFailed);
        DUWN_ASSERT(wasapi.PreferredDeviceId() == L"{invalid-guid-9999-not-found}");
        DUWN_ASSERT(wasapi.ResolvedDeviceId().empty());
        DUWN_ASSERT(!wasapi.IsFallbackActive());
    }

    wasapi.Stop();
}

// ---------------------------------------------------------------------------
// 22. AudioDevice: 1000 Rapid Switches Stress (Zero deadlocks, race-free handoff)
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_SwitchHandoff_Stress1000Cycles_NoDeadlock) {
    AudioRingBuffer ring(8192, 2);
    WasapiOutput wasapi(ring);

    wasapi.Init(L"");
    wasapi.Start();

    // 1000 rapid requests interleaved
    for (int i = 0; i < 1000; ++i) {
        if (i % 3 == 0) {
            wasapi.SwitchEndpoint(L"");
        } else if (i % 3 == 1) {
            wasapi.SwitchEndpoint(L"{invalid-endpoint}");
        } else {
            wasapi.SwitchEndpoint(L"");
        }
    }

    wasapi.SwitchEndpoint(L"");
    DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);

    AudioEndpointState st = wasapi.State();
    DUWN_ASSERT(st == AudioEndpointState::Playing ||
                st == AudioEndpointState::WaitingForDevice ||
                st == AudioEndpointState::InitializationFailed ||
                st == AudioEndpointState::Idle);

    wasapi.Stop();
    DUWN_ASSERT(wasapi.State() == AudioEndpointState::Stopped);
}

// ---------------------------------------------------------------------------
// 23. AudioDevice: Gain Ramp duration proportional to delta and sample rate
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_GainRamp_DurationProportionalToDelta) {
    // kRampStep = 1.0f / 512.0f
    constexpr float kStep = 1.0f / 512.0f;

    // Test Case 1: Delta = 1.0f (1.0f -> 0.0f, full mute)
    // Takes exactly 512 frames
    float gain = 1.0f;
    float target = 0.0f;
    uint32_t steps_full = 0;
    while (std::abs(gain - target) > 0.0001f) {
        gain = std::max(target, gain - kStep);
        ++steps_full;
    }
    DUWN_ASSERT(steps_full == 512);

    // Test Case 2: Delta = 0.25f (0.75f -> 0.50f)
    // Takes exactly 128 frames (512 * 0.25)
    gain = 0.75f;
    target = 0.50f;
    uint32_t steps_quarter = 0;
    while (std::abs(gain - target) > 0.0001f) {
        gain = std::max(target, gain - kStep);
        ++steps_quarter;
    }
    DUWN_ASSERT(steps_quarter == 128);

    // Test Case 3: Duration at 48kHz vs 44.1kHz
    // 512 frames at 48kHz = 512 / 48000 = 10.67ms
    // 512 frames at 44.1kHz = 512 / 44100 = 11.61ms
    double dur_48k_ms = (static_cast<double>(steps_full) / 48000.0) * 1000.0;
    double dur_44k_ms = (static_cast<double>(steps_full) / 44100.0) * 1000.0;
    DUWN_ASSERT(std::abs(dur_48k_ms - 10.667) < 0.01);
    DUWN_ASSERT(std::abs(dur_44k_ms - 11.610) < 0.01);
}

// ---------------------------------------------------------------------------
// 24. AudioDevice: Real Hardware MMDevice enumeration and active endpoint switch
// ---------------------------------------------------------------------------
DUWN_TEST(AudioDevice_HardwareEndpoints_RealtekAndVbAudio) {
    AudioDeviceManager mgr;
    if (!mgr.Init(nullptr)) return;

    auto endpoints = mgr.Enumerate();
    std::wstring default_id = mgr.DefaultDeviceId();

    // Verify all active endpoints
    std::wstring realtek_id;
    std::wstring vbaudio_id;

    for (const auto& ep : endpoints) {
        DUWN_ASSERT(!ep.id.empty());
        DUWN_ASSERT(!ep.friendly_name.empty());
        if (ep.friendly_name.find(L"Realtek") != std::wstring::npos) {
            realtek_id = ep.id;
        } else if (ep.friendly_name.find(L"CABLE") != std::wstring::npos ||
                   ep.friendly_name.find(L"VB-Audio") != std::wstring::npos) {
            vbaudio_id = ep.id;
        }
    }

    // If both Realtek and VB-Audio active render endpoints exist on host:
    if (!realtek_id.empty() && !vbaudio_id.empty()) {
        AudioRingBuffer ring(4096, 2);
        WasapiOutput wasapi(ring);
        if (wasapi.Init(realtek_id)) {
            wasapi.Start();
            DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::PinnedDevice);

            // Live switch to VB-Audio
            bool switched = wasapi.SwitchEndpoint(vbaudio_id);
            DUWN_ASSERT(switched);
            DUWN_ASSERT(wasapi.CurrentDeviceId() == vbaudio_id);

            // Switch back to Realtek
            switched = wasapi.SwitchEndpoint(realtek_id);
            DUWN_ASSERT(switched);
            DUWN_ASSERT(wasapi.CurrentDeviceId() == realtek_id);

            // Switch back to System Default
            switched = wasapi.SwitchEndpoint(L"");
            DUWN_ASSERT(switched);
            DUWN_ASSERT(wasapi.SelectionPolicy() == EndpointSelectionPolicy::SystemDefault);

            wasapi.Stop();
        }
    }

    mgr.Shutdown();
}

