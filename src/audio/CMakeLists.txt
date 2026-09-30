add_library(duwn-audio STATIC
    AudioEngine.cpp
    WasapiOutput.cpp
    AudioDeviceManager.cpp
    AudioRingBuffer.cpp
    AudioConverter.cpp
    AudioClock.cpp
    AudioStatistics.cpp
    AudioBufferController.cpp
    AudioClockServo.cpp
)

target_include_directories(duwn-audio
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-audio
    PUBLIC  duwn-common
    PRIVATE mf
            mfuuid
            mfplat
            avrt      # MMCSS for audio thread priority
            propsys   # PKEY_Device_FriendlyName for endpoint enumeration
)

target_compile_features(duwn-audio PUBLIC cxx_std_20)
