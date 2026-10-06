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

# Duwn Mirror DirectShow Virtual Camera Filter (for OBS Studio / TikTok Live Studio webcam capture)
add_library(duwn-virtualcam SHARED
    virtualcam/DuwnVirtualCam.cpp
    virtualcam/DllMain.cpp
    virtualcam/duwn-virtualcam.def
    "${CMAKE_BINARY_DIR}/generated/virtualcam-version.rc"
)

add_dependencies(duwn-virtualcam duwn-version-update)

target_include_directories(duwn-virtualcam
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-virtualcam
    PRIVATE
        duwn-capture
        duwn-common
        strmiids
        d3d11
        dxgi
        shlwapi
        ole32
        oleaut32
        winmm
)

target_compile_features(duwn-virtualcam PRIVATE cxx_std_20)

set_target_properties(duwn-virtualcam PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
