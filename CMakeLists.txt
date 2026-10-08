cmake_minimum_required(VERSION 3.28)

project(DuwnMirror
    VERSION 1.1.5
    DESCRIPTION "Wireless AirPlay and USB device connection for Windows"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories — keep build artefacts together
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Platform guard
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "Duwn Mirror targets Windows only.")
endif()

# ---------------------------------------------------------------------------
# Windows SDK / WINAPI version
# Target: Windows 10 22H2+ (0x0A00000A = 10.0.19045)
# ---------------------------------------------------------------------------
add_compile_definitions(
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# ---------------------------------------------------------------------------
# Compiler options (MSVC)
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        /W4             # high warning level
        /WX             # warnings as errors in our own code
        /permissive-    # strict standard conformance
        /MP             # parallel compilation
        /utf-8          # source/execution charset UTF-8
        /Zc:__cplusplus # correct __cplusplus macro
    )

    # Disable specific noisy warnings
    add_compile_options(
        /wd4100  # unreferenced formal parameter (WinUI generated code)
        /wd4324  # structure padding due to alignment specifier
    )

    # Release: optimise for speed, enable whole-program opt
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<CONFIG:Release>:/GL>)
    add_link_options($<$<CONFIG:Release>:/LTCG>)
endif()

# ---------------------------------------------------------------------------
# Version & Build Information
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(DUWN_GIT_COMMIT_HASH "unknown")
set(DUWN_GIT_COMMIT_SHORT "unknown")
set(DUWN_GIT_BRANCH "unknown")
set(DUWN_GIT_DIRTY 0)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DUWN_GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DUWN_GIT_COMMIT_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DUWN_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DUWN_GIT_STATUS_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(DUWN_GIT_STATUS_RAW)
        set(DUWN_GIT_DIRTY 1)
    else()
        set(DUWN_GIT_DIRTY 0)
    endif()
endif()

string(TIMESTAMP DUWN_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)

configure_file(
    "${CMAKE_SOURCE_DIR}/src/common/Version.h.in"
    "${CMAKE_SOURCE_DIR}/src/common/Version.h"
    @ONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/app/version.rc.in"
    "${CMAKE_BINARY_DIR}/generated/version.rc"
    @ONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/capture/virtualcam/version.rc.in"
    "${CMAKE_BINARY_DIR}/generated/virtualcam-version.rc"
    @ONLY
)
add_custom_target(duwn-version-update
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P "${CMAKE_SOURCE_DIR}/tools/UpdateVersion.cmake"
    BYPRODUCTS "${CMAKE_SOURCE_DIR}/src/common/Version.h"
    COMMENT "Checking Git HEAD and updating Version.h if changed"
)


# ---------------------------------------------------------------------------
# Dependencies (vcpkg / find_package)
# ---------------------------------------------------------------------------

# fmt — structured logging
find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
    message(STATUS "fmt not found via vcpkg; will use bundled fallback or disable")
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src/common)
add_subdirectory(src/network)
add_subdirectory(src/video)
add_subdirectory(src/audio)
add_subdirectory(src/sync)
add_subdirectory(src/airplay)
add_subdirectory(src/wired)
add_subdirectory(src/capture)
add_subdirectory(src/ui)
add_subdirectory(src/direct)
add_subdirectory(src/app)

# Tests
option(DUWN_BUILD_TESTS "Build unit and integration tests" ON)
if(DUWN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# OBS plugin — optional, requires OBS SDK
option(DUWN_BUILD_OBS_PLUGIN "Build OBS Studio source plugin" OFF)
if(DUWN_BUILD_OBS_PLUGIN)
    add_subdirectory(obs-plugin)
endif()
