add_library(duwn-ui STATIC
    Theme.h
    UiState.h
    resource.h
    UiRenderer.h
    UiRenderer.cpp
    MainWindowView.h
    MainWindowView.cpp
    Loc.h
    Loc.cpp
)

target_include_directories(duwn-ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-ui
    PUBLIC d2d1
           dwrite
           dwmapi
           windowscodecs
)

target_compile_features(duwn-ui PUBLIC cxx_std_20)
