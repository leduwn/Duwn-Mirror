add_library(duwn-capture STATIC
    SharedTexture.cpp
    CaptureServer.cpp
)

target_include_directories(duwn-capture
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-capture PUBLIC duwn-common d3d11 dxgi)
target_compile_features(duwn-capture PUBLIC cxx_std_20)
