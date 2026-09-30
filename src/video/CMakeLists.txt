add_library(duwn-video STATIC
    VideoDecoder.cpp
    HevcRtpAssembler.cpp
    MFVideoDecoder.cpp
    D3D11Device.cpp
    VideoFrame.cpp
    VideoRenderer.cpp
    WarpVideoRenderer.h
    WarpVideoRenderer.cpp
    IVideoRenderer.h
    FrameScheduler.cpp
    VideoStatistics.cpp
    StreamGeometryCoordinator.cpp
    SourceQualityTracker.cpp
)

target_include_directories(duwn-video
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-video
    PUBLIC  duwn-common
    PRIVATE duwn-network
            mf
            mfuuid
            mfplat
            mfreadwrite
            d3d11
            dxgi
            dxguid
            d2d1
)

target_compile_features(duwn-video PUBLIC cxx_std_20)
