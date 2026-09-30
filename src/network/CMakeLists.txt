add_library(duwn-network STATIC
    RtpReceiver.cpp
    RtpClock.cpp
    JitterBuffer.cpp
    PacketStatistics.cpp
    NetworkEnvironment.cpp
    NetworkChangeMonitor.cpp
    BleBeaconPublisher.cpp
)

target_include_directories(duwn-network
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-network
    PUBLIC  duwn-common
    PRIVATE ws2_32 iphlpapi ole32 oleaut32 avrt windowsapp
)

target_compile_features(duwn-network PUBLIC cxx_std_20)
