cmake_minimum_required(VERSION 3.28)

# Use CTest + a lightweight test runner.
# For Milestone 0 we use a minimal hand-rolled test runner to avoid
# adding an external test framework dependency before vcpkg is confirmed.
# Milestone 2: migrate to Catch2 or GoogleTest via vcpkg.

# test_main.cpp #includes the individual test files (unity build).
# Only test_main.cpp is compiled — no separate compilation units.
add_executable(duwn-unit-tests
    unit/test_main.cpp
    ../src/app/Settings.cpp
    ../src/app/UpdateManager.cpp
)

target_include_directories(duwn-unit-tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-unit-tests
    PRIVATE duwn-common
            duwn-direct
            duwn-network
            duwn-audio
            duwn-video
            duwn-airplay
            duwn-wired
            duwn-ui
            msi
            crypt32
            wintrust
)

target_compile_features(duwn-unit-tests PRIVATE cxx_std_20)

add_test(NAME duwn-unit-tests COMMAND duwn-unit-tests)
