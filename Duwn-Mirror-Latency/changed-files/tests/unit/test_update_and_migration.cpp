#include "app/UpdateManager.h"
#include "app/Settings.h"
#include <string>

using namespace duwn::app;

DUWN_TEST(SemanticVersion_ParseValidStrings) {
    auto v1 = SemanticVersion::Parse("0.2.0");
    DUWN_ASSERT(v1.major == 0 && v1.minor == 2 && v1.patch == 0 && v1.build == 0);

    auto v2 = SemanticVersion::Parse("v0.3.10");
    DUWN_ASSERT(v2.major == 0 && v2.minor == 3 && v2.patch == 10 && v2.build == 0);

    auto v3 = SemanticVersion::Parse("1.4.2.105");
    DUWN_ASSERT(v3.major == 1 && v3.minor == 4 && v3.patch == 2 && v3.build == 105);

    auto v4 = SemanticVersion::Parse(L"v2.0.0");
    DUWN_ASSERT(v4.major == 2 && v4.minor == 0 && v4.patch == 0);
}

DUWN_TEST(SemanticVersion_NumericComparisonNotLexicographic) {
    // 0.2.9 vs 0.2.10
    auto v_2_9 = SemanticVersion::Parse("0.2.9");
    auto v_2_10 = SemanticVersion::Parse("0.2.10");
    DUWN_ASSERT(v_2_9 < v_2_10);
    DUWN_ASSERT(v_2_10 > v_2_9);

    // 0.2.10 vs 0.3.0
    auto v_3_0 = SemanticVersion::Parse("0.3.0");
    DUWN_ASSERT(v_2_10 < v_3_0);

    // 0.9.0 vs 1.0.0
    auto v_9_0 = SemanticVersion::Parse("0.9.0");
    auto v_1_0_0 = SemanticVersion::Parse("1.0.0");
    DUWN_ASSERT(v_9_0 < v_1_0_0);

    // Equality
    auto v_same = SemanticVersion::Parse("0.2.0");
    auto v_same2 = SemanticVersion::Parse("v0.2.0");
    DUWN_ASSERT(v_same == v_same2);
}

DUWN_TEST(UpdateManager_ClassifyInstallerStates) {
    SemanticVersion setup_ver{0, 3, 0, 0};

    // State A: Not installed
    SemanticVersion not_inst{0, 0, 0, 0};
    DUWN_ASSERT(UpdateManager::ClassifyInstallerState(not_inst, setup_ver) == InstallerState::NotInstalled);

    // State B: Installed older (0.2.0 < 0.3.0) -> Update
    SemanticVersion older_inst{0, 2, 0, 0};
    DUWN_ASSERT(UpdateManager::ClassifyInstallerState(older_inst, setup_ver) == InstallerState::InstalledOlder);

    // State C: Installed same (0.3.0 == 0.3.0) -> Repair / Maintenance
    SemanticVersion same_inst{0, 3, 0, 0};
    DUWN_ASSERT(UpdateManager::ClassifyInstallerState(same_inst, setup_ver) == InstallerState::InstalledSame);

    // State D: Installed newer (0.4.0 > 0.3.0) -> Block downgrade
    SemanticVersion newer_inst{0, 4, 0, 0};
    DUWN_ASSERT(UpdateManager::ClassifyInstallerState(newer_inst, setup_ver) == InstallerState::InstalledNewer);
}

DUWN_TEST(Settings_ValidationAcceptsValidAndRejectsCorrupted) {
    Settings s{};
    DUWN_ASSERT(Settings::ValidateSettings(s) == true);

    // Invalid bounds
    Settings corrupted = s;
    corrupted.brightness = 200; // max 100
    DUWN_ASSERT(Settings::ValidateSettings(corrupted) == false);

    corrupted = s;
    corrupted.output_width = 100; // min 320
    DUWN_ASSERT(Settings::ValidateSettings(corrupted) == false);

    corrupted = s;
    corrupted.schema_version = 0; // invalid schema
    DUWN_ASSERT(Settings::ValidateSettings(corrupted) == false);
}

DUWN_TEST(Settings_SequentialMigrationV0ToV2) {
    Settings s{};
    s.schema_version = 0;
    s.first_run_completed = false;
    s.brightness = 7;
    s.contrast = 0;
    s.saturation = 2;
    s.hue = 4;
    s.sharpness = 7;

    // Migrate V0 -> V1
    DUWN_ASSERT(Settings::MigrateSettingsV0ToV1(s) == true);
    DUWN_ASSERT(s.schema_version == 1);
    DUWN_ASSERT(s.first_run_completed == true);

    // Migrate V1 -> V2
    DUWN_ASSERT(Settings::MigrateSettingsV1ToV2(s) == true);
    DUWN_ASSERT(s.schema_version == 2);
    DUWN_ASSERT(s.receiver_quality == ReceiverQuality::Auto);
    // Color anomaly should be reset to neutral
    DUWN_ASSERT(s.brightness == 0 && s.contrast == 0 && s.saturation == 0 && s.hue == 0 && s.sharpness == 0);
    DUWN_ASSERT(s.color_preset == ColorPreset::Neutral);

    // Validated
    DUWN_ASSERT(Settings::ValidateSettings(s) == true);
}

DUWN_TEST(Settings_ValidatesStreamingPolicyBounds) {
    Settings s{};
    s.streaming_mode = StreamingMode::Custom;
    s.custom_video_freshness_ms = 5;
    s.custom_video_queue_frames = 1;
    DUWN_ASSERT(Settings::ValidateSettings(s));
    s.custom_video_freshness_ms = 100;
    s.custom_video_queue_frames = 3;
    DUWN_ASSERT(Settings::ValidateSettings(s));
    s.custom_video_queue_frames = 0;
    DUWN_ASSERT(!Settings::ValidateSettings(s));
    s.custom_video_queue_frames = 4;
    DUWN_ASSERT(!Settings::ValidateSettings(s));
    s.custom_video_queue_frames = 2;
    s.custom_video_freshness_ms = 101;
    DUWN_ASSERT(!Settings::ValidateSettings(s));
    s.custom_video_freshness_ms = 25;
    s.streaming_mode = static_cast<StreamingMode>(99);
    DUWN_ASSERT(!Settings::ValidateSettings(s));
}
