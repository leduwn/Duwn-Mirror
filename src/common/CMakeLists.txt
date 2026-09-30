add_library(duwn-common STATIC
    clock/MonotonicClock.cpp
    logging/Logger.cpp
    metrics/Metrics.cpp
    system/CpuCapabilities.cpp
    system/ClipboardUtil.cpp
    telemetry/ConnectionTelemetry.cpp
    telemetry/ConnectionTimeline.cpp
    telemetry/LatencyTelemetry.cpp
    threading/ThreadUtil.cpp
)

target_include_directories(duwn-common
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
)

target_compile_features(duwn-common PUBLIC cxx_std_20)

# fmt if available
if(fmt_FOUND)
    target_link_libraries(duwn-common PUBLIC fmt::fmt)
    target_compile_definitions(duwn-common PUBLIC DUWN_HAS_FMT)
endif()
