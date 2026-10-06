find_package(Git QUIET)
set(DUWN_GIT_COMMIT_HASH "unknown")
set(DUWN_GIT_COMMIT_SHORT "unknown")
set(DUWN_GIT_BRANCH "unknown")
set(DUWN_GIT_DIRTY 0)

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE DUWN_GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE DUWN_GIT_COMMIT_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE DUWN_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${SOURCE_DIR}"
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

set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_VERSION "1.1.1")

configure_file(
    "${SOURCE_DIR}/src/common/Version.h.in"
    "${SOURCE_DIR}/src/common/Version.h"
    @ONLY
)
