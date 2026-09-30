add_library(duwn-sync STATIC
    MasterClock.cpp
    AvSynchronizer.cpp
    DriftController.cpp
)

target_include_directories(duwn-sync
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-sync PUBLIC duwn-common)
target_compile_features(duwn-sync PUBLIC cxx_std_20)
