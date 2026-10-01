#include "Logger.h"
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <array>
#include <chrono>
#include <format>
#include <regex>

namespace duwn {

namespace {

constexpr uintmax_t MAX_LOG_SIZE = 5 * 1024 * 1024; // 5MB
constexpr int MAX_BACKUP_FILES = 5;

struct LogState {
    std::mutex            mutex;
    std::ofstream         file;
    std::filesystem::path logDir;
    std::filesystem::path mainLogPath;
    LogLevel              minLevel = LogLevel::Info;
    bool                  initialised = false;
    std::string           userProfileStr;
};

LogState g_log;

constexpr std::string_view LevelName(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string Timestamp() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

std::wstring GetDefaultLogDir() {
    PWSTR path = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::wstring res(path);
        ::CoTaskMemFree(path);
        return res + L"\\Duwn Mirror\\Logs";
    }
    return L"logs";
}

std::string RedactSensitiveData(std::string_view input) {
    std::string result(input);

    // 1. Sanitize user profile path
    if (!g_log.userProfileStr.empty() && g_log.userProfileStr.length() > 3) {
        size_t pos = 0;
        while ((pos = result.find(g_log.userProfileStr, pos)) != std::string::npos) {
            result.replace(pos, g_log.userProfileStr.length(), "%USERPROFILE%");
            pos += 13;
        }
    }

    // 2. Redact 40-character hex UDIDs: replace with ...XXXX (last 4 chars)
    static const std::regex udid40_regex(R"(\b([0-9a-fA-F]{36})([0-9a-fA-F]{4})\b)");
    result = std::regex_replace(result, udid40_regex, "...$2");

    // 3. Redact 25-character (8-16 hyphenated) UDIDs: replace with ...XXXX (last 4 chars)
    static const std::regex udid25_regex(R"(\b([0-9a-fA-F]{8}-[0-9a-fA-F]{12})([0-9a-fA-F]{4})\b)");
    result = std::regex_replace(result, udid25_regex, "...$2");

    // 4. Mask sensitive key/secret parameters: key=..., secret=..., etc.
    static const std::regex secret_regex(R"((pin|key|secret|token|password|auth_tag|aes_key|pk)\s*[:=]\s*([^\s,;]+))", std::regex_constants::icase);
    result = std::regex_replace(result, secret_regex, "$1=***REDACTED***");

    return result;
}

void RotateLogsIfNeededLocked() {
    namespace fs = std::filesystem;
    if (!g_log.file.is_open()) return;

    std::error_code ec;
    auto size = fs::file_size(g_log.mainLogPath, ec);
    if (ec || size < MAX_LOG_SIZE) return;

    // Flush and close current file
    g_log.file.flush();
    g_log.file.close();

    // Rotate existing backups: .5 removed, .4 -> .5, ... .1 -> .2, main -> .1
    fs::path oldest = g_log.logDir / std::format(L"duwn-mirror.{}.log", MAX_BACKUP_FILES);
    fs::remove(oldest, ec);

    for (int i = MAX_BACKUP_FILES - 1; i >= 1; --i) {
        fs::path src = g_log.logDir / std::format(L"duwn-mirror.{}.log", i);
        fs::path dst = g_log.logDir / std::format(L"duwn-mirror.{}.log", i + 1);
        if (fs::exists(src, ec)) {
            fs::remove(dst, ec);
            ::MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        }
    }

    fs::path first_backup = g_log.logDir / L"duwn-mirror.1.log";
    fs::remove(first_backup, ec);
    BOOL moved = ::MoveFileExW(g_log.mainLogPath.c_str(), first_backup.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);

    // Reopen main file: if move succeeded, start fresh; if move failed, append so logs are NOT lost!
    if (moved) {
        g_log.file.open(g_log.mainLogPath, std::ios::out | std::ios::trunc);
    } else {
        g_log.file.open(g_log.mainLogPath, std::ios::out | std::ios::app);
    }
}

} // namespace

void Logger::Initialize(std::wstring_view customLogDir) {
    std::lock_guard lock{g_log.mutex};
    if (g_log.initialised) return;
    namespace fs = std::filesystem;

    fs::path dir = customLogDir.empty() ? fs::path(GetDefaultLogDir()) : fs::path(customLogDir);
    g_log.logDir = dir;

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        auto err_msg = std::format(
            L"[Logger] Failed to create log directory '{}': {} (error {})\n",
            dir.wstring(),
            std::wstring(ec.message().begin(), ec.message().end()),
            ec.value());
        ::OutputDebugStringW(err_msg.c_str());
    }

    // Cache user profile for redaction
    char user_prof[MAX_PATH] = {0};
    DWORD len = ::GetEnvironmentVariableA("USERPROFILE", user_prof, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        g_log.userProfileStr = user_prof;
    }

    g_log.mainLogPath = dir / L"duwn-mirror.log";
    g_log.file.open(g_log.mainLogPath, std::ios::app | std::ios::out);
    if (!g_log.file.is_open()) {
        DWORD win_err = ::GetLastError();
        auto err_msg = std::format(
            L"[Logger] Failed to open log file '{}': system error {:#010x}\n",
            g_log.mainLogPath.wstring(),
            win_err);
        ::OutputDebugStringW(err_msg.c_str());
    } else {
        g_log.initialised = true;
    }

    auto start_line = std::format("[{}] [INFO ] [Logger  ] Session started. Log file: {}\n",
        Timestamp(), g_log.mainLogPath.string());
    if (g_log.file.is_open()) {
        g_log.file << start_line;
        g_log.file.flush();
    }
    ::OutputDebugStringA(start_line.c_str());
}

std::wstring Logger::GetLogDirectory() {
    std::lock_guard lock{g_log.mutex};
    if (!g_log.logDir.empty()) {
        return g_log.logDir.wstring();
    }
    return GetDefaultLogDir();
}

void Logger::SetLevel(LogLevel level) noexcept {
    g_log.minLevel = level;
}

void Logger::Write(LogLevel level, std::string_view component,
                   std::string_view message, std::source_location /*loc*/) {
    if (level < g_log.minLevel) return;

    std::string clean_msg = RedactSensitiveData(message);
    auto line = std::format("[{}] [{}] [{:<8}] {}\n",
        Timestamp(), LevelName(level), component, clean_msg);

    std::lock_guard lock{g_log.mutex};
    RotateLogsIfNeededLocked();

    if (g_log.file.is_open()) {
        g_log.file << line;
        g_log.file.flush();
    }
    ::OutputDebugStringA(line.c_str());
}

void Logger::Flush() {
    std::lock_guard lock{g_log.mutex};
    if (g_log.file.is_open()) g_log.file.flush();
}

void Logger::Shutdown() {
    std::lock_guard lock{g_log.mutex};
    if (g_log.file.is_open()) {
        auto line = std::format("[{}] [INFO ] [Logger  ] Session ended\n", Timestamp());
        g_log.file << line;
        g_log.file.close();
    }
}

} // namespace duwn
