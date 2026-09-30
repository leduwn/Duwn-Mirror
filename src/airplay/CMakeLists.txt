add_library(duwn-airplay STATIC
    AirPlayEngine.cpp
    AirPlayProcess.cpp
    AppleModelDatabase.cpp
    ControlIpc.cpp
    ControlIpcServer.cpp
    SessionState.cpp
    SidecarVerificationCache.cpp
    StreamMetadata.cpp
)

target_include_directories(duwn-airplay
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-airplay
    PUBLIC  duwn-common
            duwn-network
            advapi32
)

target_compile_features(duwn-airplay PUBLIC cxx_std_20)
