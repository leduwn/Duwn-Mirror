// Runs the same policy cases as the Windows suite without a Windows SDK.
// Build with C++20, -pthread, -Isrc, and warnings enabled.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace portable_tests {
using Test = std::pair<const char*, void (*)()>;
inline std::vector<Test>& Registry() { static std::vector<Test> tests; return tests; }
struct Register {
    Register(const char* name, void (*fn)()) { Registry().emplace_back(name, fn); }
};
}
#define DUWN_TEST(name) \
    static void test_##name(); \
    static portable_tests::Register reg_##name{#name, test_##name}; \
    static void test_##name()
#define DUWN_ASSERT(condition) \
    do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

#include "../unit/test_streaming_policy.cpp"

int main() {
    int failed = 0;
    for (const auto& [name, fn] : portable_tests::Registry()) {
        try { fn(); std::printf("[PASS] %s\n", name); }
        catch (const std::exception& e) {
            std::printf("[FAIL] %s: %s\n", name, e.what());
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}
