find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DUWN_BRAND_SIZES 16 20 24 32 40 48 64 128 256 512)
set(DUWN_BRAND_OUTPUTS "${CMAKE_SOURCE_DIR}/assets/app_icon.ico")
foreach(size IN LISTS DUWN_BRAND_SIZES)
    list(APPEND DUWN_BRAND_OUTPUTS "${CMAKE_SOURCE_DIR}/assets/logo_${size}x${size}.png")
endforeach()
add_custom_command(
    OUTPUT ${DUWN_BRAND_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate-brand-assets.py"
    DEPENDS "${CMAKE_SOURCE_DIR}/assets/logo_master_clean.png"
            "${CMAKE_SOURCE_DIR}/tools/generate-brand-assets.py"
    COMMENT "Generate and validate DUWN brand assets"
    VERBATIM
)
add_custom_target(duwn-brand-assets DEPENDS ${DUWN_BRAND_OUTPUTS})

add_executable(duwn-mirror WIN32
    main.cpp
    App.cpp
    MainWindow.cpp
    OutputWindow.cpp
    PreviewWindow.cpp
    Settings.cpp
    CrashHandler.cpp
    DiagnosticsCollector.cpp
    UpdateManager.cpp
    resources.rc
    "${CMAKE_BINARY_DIR}/generated/version.rc"
    duwn-mirror.manifest
)
add_dependencies(duwn-mirror duwn-brand-assets)

target_include_directories(duwn-mirror
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(duwn-mirror
    PRIVATE duwn-common
            duwn-direct
            duwn-network
            duwn-airplay
            duwn-wired
            duwn-video
            duwn-audio
            duwn-sync
            duwn-capture
            duwn-ui
            d2d1
            dwrite
            dwmapi
            mf
            mfuuid
            mfplat
            d3d11
            dxgi
            ole32
            oleaut32
            dbghelp
            msi
            crypt32
            wintrust
)

target_compile_features(duwn-mirror PRIVATE cxx_std_20)

# Set subsystem to Windows (no console in release; console in debug for logging)
set_target_properties(duwn-mirror PROPERTIES
    WIN32_EXECUTABLE TRUE
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Manifest embedding (MSVC only)
# Adding duwn-mirror.manifest to sources causes MSVC to pass it to mt.exe via
# the linker's /MANIFEST:EMBED step. The linker merges it with any auto-generated
# manifest (compatibility/DPI nodes take precedence over linker defaults).
# Verification after build: mt.exe -inputresource:duwn-mirror.exe;#1 -out:extracted.manifest
# or: sigcheck -m duwn-mirror.exe
# The embedded manifest must contain the PerMonitorV2 dpiAwareness node and the
# Windows 10/11 supportedOS GUID {8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}.
if(MSVC)
    target_link_options(duwn-mirror PRIVATE "/MANIFEST:EMBED")
endif()

# Stage private duwn-airplay runtime into output directory
add_custom_command(TARGET duwn-mirror POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/runtime/duwn-airplay"
            "$<TARGET_FILE_DIR:duwn-mirror>/duwn-airplay"
    COMMENT "Stage duwn-airplay private runtime into output directory"
    VERBATIM
)
