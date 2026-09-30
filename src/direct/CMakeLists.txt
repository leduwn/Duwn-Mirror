add_library(duwn-direct STATIC
    DirectNegotiator.cpp
    DirectFrameAssembler.cpp
    DirectUdpTransport.cpp
    DirectSession.cpp
    DirectReceiver.cpp
    DirectPipelineBridge.cpp
    quality/DirectQualityController.cpp
    ios/capture/CaptureBackendFactory.cpp
)
target_include_directories(duwn-direct PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(duwn-direct PUBLIC cxx_std_20)
target_link_libraries(duwn-direct PUBLIC ws2_32 duwn-video duwn-common)
