add_library(duwn-wired STATIC
    WiredDeviceManager.cpp
    WiredControlClient.cpp
)
target_include_directories(duwn-wired PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(duwn-wired PUBLIC cxx_std_20)
target_link_libraries(duwn-wired PUBLIC setupapi advapi32 ws2_32)
