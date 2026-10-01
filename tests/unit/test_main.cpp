// Minimal test runner — no external framework required.
// Each test file registers tests via DUWN_TEST macro.
// Expand to Catch2/GoogleTest in Milestone 2.

#include <cstdio>
#include <vector>
#include <functional>
#include <string_view>
#include <stdexcept>
#include <cmath>
#include <windows.h>

// Common headers needed by test files
#include "common/clock/MonotonicClock.h"
#include "common/metrics/Metrics.h"

namespace test_framework {

struct TestCase {
    std::string_view        name;
    std::function<void()>   fn;
    bool                    passed{false};
    std::string             failure_msg;
};

static std::vector<TestCase>& Registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(std::string_view name, std::function<void()> fn) {
        Registry().push_back({name, std::move(fn)});
    }
};

void AssertTrue(bool cond, const char* expr, const char* file, int line);

} // namespace test_framework

#define DUWN_TEST(name) \
    static void test_##name(); \
    static test_framework::Registrar reg_##name{#name, test_##name}; \
    static void test_##name()

#define DUWN_ASSERT(cond) \
    test_framework::AssertTrue((cond), #cond, __FILE__, __LINE__)

#include "test_rtp_packet.cpp"
#include "test_jitter_buffer.cpp"
#include "test_audio_ring_buffer.cpp"
#include "test_audio_converter.cpp"
#include "test_audio_control.cpp"
#include "test_rtp_clock.cpp"
#include "test_monotonic_clock.cpp"
#include "test_video_geometry.cpp"
#include "test_wired_device.cpp"
#include "test_dual_mode_localization.cpp"
#include "test_frame_scheduler.cpp"
#include "test_streaming_policy.cpp"
#include "test_streaming_scheduler.cpp"
#include "test_pipeline_tier.cpp"
#include "test_video_ipc_ring.cpp"
#include "test_ui_state.cpp"
#include "test_productization.cpp"
#include "test_warp_renderer.cpp"
#include "test_audio_device.cpp"
#include "test_output_window.cpp"
#include "test_localization.cpp"
#include "test_cpu_capabilities.cpp"
#include "test_connection_telemetry.cpp"
#include "test_rtp_priority_benchmark.cpp"
#include "test_60s_benchmark.cpp"
#include "test_firewall_verification.cpp"
#include "test_update_and_migration.cpp"
#include "test_apple_model_database.cpp"
#include "test_preview_window.cpp"
#include "test_hevc_rtp.cpp"
#include "test_network_compatibility.cpp"
#include "test_source_quality.cpp"
#include "test_connection_timeline.cpp"
#include "test_sidecar_cache.cpp"
#include "test_direct_mode.cpp"
#include "test_direct_receiver.cpp"
#include "test_direct_encoder.cpp"
#include "test_direct_e2e_prototype.cpp"
#include "test_direct_quality_envelope.cpp"
#include "test_direct_adaptive_controller.cpp"
#include "test_direct_hevc_path.cpp"
#include "test_direct_capture_architecture.cpp"
#include "test_metadata_concurrency.cpp"

#include <objbase.h>

namespace test_framework {

void AssertTrue(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        char buf[512];
        snprintf(buf, sizeof(buf), "FAILED: %s at %s:%d", expr, file, line);
        throw std::runtime_error(buf);
    }
}

} // namespace test_framework

int main() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Process-level watchdog: abort if test process hangs (e.g. deadlocked thread in join/mutex)
    std::thread suite_watchdog([]() {
        std::this_thread::sleep_for(std::chrono::seconds(90));
        fprintf(stderr, "\n[FATAL] Test process watchdog timeout (90s) exceeded! Aborting deadlocked process.\n");
        fflush(stderr);
        std::_Exit(2);
    });
    suite_watchdog.detach();

    // Init MonotonicClock for tests that need timing
    duwn::clock::MonotonicClock::Initialize();

    int passed = 0, failed = 0;
    for (auto& tc : test_framework::Registry()) {
        try {
            printf("[RUN ] %.*s\n", static_cast<int>(tc.name.size()), tc.name.data());
            fflush(stdout);
            tc.fn();
            tc.passed = true;
            printf("[PASS] %.*s\n", static_cast<int>(tc.name.size()), tc.name.data());
            fflush(stdout);
            ++passed;
        } catch (const std::exception& e) {
            tc.passed = false;
            printf("[FAIL] %.*s: %s\n",
                static_cast<int>(tc.name.size()), tc.name.data(), e.what());
            fflush(stdout);
            ++failed;
        }
    }
    printf("\n%d passed, %d failed\n", passed, failed);
    ::CoUninitialize();
    return failed == 0 ? 0 : 1;
}
