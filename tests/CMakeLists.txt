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
            duwn-capture
            msi
            crypt32
            wintrust
)

target_compile_features(duwn-unit-tests PRIVATE cxx_std_20)

add_test(NAME duwn-unit-tests COMMAND duwn-unit-tests)
set_tests_properties(duwn-unit-tests PROPERTIES TIMEOUT 300)
add_executable(duwn-capture-consumer
    capture_consumer_harness.cpp
)
target_include_directories(duwn-capture-consumer
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(duwn-capture-consumer
    PRIVATE duwn-capture
            d3d11
            dxgi
)
target_compile_features(duwn-capture-consumer PRIVATE cxx_std_20)
add_executable(duwn-virtualcam-test
    virtualcam_test.cpp
)
target_include_directories(duwn-virtualcam-test
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(duwn-virtualcam-test
    PRIVATE strmiids
            ole32
            oleaut32
)
target_compile_features(duwn-virtualcam-test PRIVATE cxx_std_20)

add_executable(duwn-capture-persistence-test
    test_capture_and_persistence.cpp
    ../src/app/OutputWindow.cpp
    ../src/app/Settings.cpp
)
target_include_directories(duwn-capture-persistence-test
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(duwn-capture-persistence-test
    PRIVATE duwn-common
            duwn-video
            duwn-audio
            duwn-capture
            duwn-ui
            d3d11
            dxgi
            mfplat
            mfuuid
)
target_compile_features(duwn-capture-persistence-test PRIVATE cxx_std_20)
add_test(NAME duwn-capture-persistence-test COMMAND duwn-capture-persistence-test)



