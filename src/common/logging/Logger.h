#pragma once
// Logger — lightweight structured logger for DUWN Mirror.
// Thread-safe. Rotating daily log files.
// HOT PATH: video/audio per-frame paths must NOT call Logger directly.
// Use metrics/atomics for per-frame data; log session events only.

#include <string_view>
#include <source_location>
#include <format>

namespace duwn {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

class Logger {
public:
    // Call once at startup. Resolves %LOCALAPPDATA%\Duwn Mirror\Logs\duwn-mirror.log.
    // If customLogDir is provided, uses that directory instead.
    static void Initialize(std::wstring_view customLogDir = {});

    // Get the current resolved log directory
    static std::wstring GetLogDirectory();

    // Minimum level for console + file output. Default: Info.
    static void SetLevel(LogLevel level) noexcept;

    // Core log function. Do not call directly; use macros below.
    static void Write(LogLevel level,
                      std::string_view component,
                      std::string_view message,
                      std::source_location loc = std::source_location::current());

    static void Flush();
    static void Shutdown();
};

} // namespace duwn

// Convenience macros — component is a string literal identifying the subsystem.
// These evaluate to no-ops if the level is filtered out.
#define DUWN_LOG_TRACE(component, msg) \
    ::duwn::Logger::Write(::duwn::LogLevel::Trace, (component), (msg))
#define DUWN_LOG_DEBUG(component, msg) \
    ::duwn::Logger::Write(::duwn::LogLevel::Debug, (component), (msg))
#define DUWN_LOG_INFO(component, msg)  \
    ::duwn::Logger::Write(::duwn::LogLevel::Info,  (component), (msg))
#define DUWN_LOG_WARN(component, msg)  \
    ::duwn::Logger::Write(::duwn::LogLevel::Warn,  (component), (msg))
#define DUWN_LOG_ERROR(component, msg) \
    ::duwn::Logger::Write(::duwn::LogLevel::Error, (component), (msg))

// Format variant (C++20 std::format)
#define DUWN_LOG_DEBUGF(component, fmt, ...) \
    ::duwn::Logger::Write(::duwn::LogLevel::Debug, (component), std::format((fmt), __VA_ARGS__))
#define DUWN_LOG_INFOF(component, fmt, ...) \
    ::duwn::Logger::Write(::duwn::LogLevel::Info,  (component), std::format((fmt), __VA_ARGS__))
#define DUWN_LOG_WARNF(component, fmt, ...) \
    ::duwn::Logger::Write(::duwn::LogLevel::Warn,  (component), std::format((fmt), __VA_ARGS__))
#define DUWN_LOG_ERRORF(component, fmt, ...) \
    ::duwn::Logger::Write(::duwn::LogLevel::Error, (component), std::format((fmt), __VA_ARGS__))
