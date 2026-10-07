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
#include <deque>
#include <condition_variable>
#include <thread>
#include <vector>

namespace duwn {

namespace {

constexpr uintmax_t MAX_LOG_SIZE = 10 * 1024 * 1024; // 10 MiB per file
constexpr int MAX_BACKUP_FILES = 4;                 // Main log + 4 backups = 5 files total (~50 MiB)

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

struct AsyncLogQueue {
    std::mutex              mutex;
    std::condition_variable cv;
    std::condition_variable cv_flush;
    std::deque<std::string> queue;
    bool                    stop_requested{false};
    std::jthread            worker;
    static constexpr size_t kMaxQueueSize = 1024;
    std::atomic<uint64_t>   dropped_count{0};
    std::atomic<uint64_t>   write_delay_ms{0};
    bool                    started{false};
};

AsyncLogQueue g_async_queue;

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

    // Rotate existing backups: oldest backup removed, .3 -> .4, .2 -> .3, .1 -> .2, main -> .1
    fs::path oldest = g_log.logDir / std::format(L"duwn-mirror.{}.log", MAX_BACKUP_FILES);
    fs::remove(oldest, ec);
    // Clean up any legacy backups exceeding MAX_BACKUP_FILES from previous versions
    for (int i = MAX_BACKUP_FILES + 1; i <= 10; ++i) {
        fs::path stray = g_log.logDir / std::format(L"duwn-mirror.{}.log", i);
        fs::remove(stray, ec);
    }

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

void StartAsyncWorkerIfNeeded() {
    std::lock_guard lock{g_async_queue.mutex};
    if (g_async_queue.started) return;
    g_async_queue.stop_requested = false;
    g_async_queue.worker = std::jthread([](std::stop_token st) {
        std::stop_callback cb(st, [] {
            g_async_queue.cv.notify_all();
        });
        while (!st.stop_requested()) {
            std::vector<std::string> batch;
            {
                std::unique_lock q_lock{g_async_queue.mutex};
                g_async_queue.cv.wait(q_lock, [&] {
                    return !g_async_queue.queue.empty() || st.stop_requested() || g_async_queue.stop_requested;
                });
                if (g_async_queue.queue.empty() && (st.stop_requested() || g_async_queue.stop_requested)) {
                    break;
                }
                while (!g_async_queue.queue.empty()) {
                    batch.push_back(std::move(g_async_queue.queue.front()));
                    g_async_queue.queue.pop_front();
                }
                g_async_queue.cv_flush.notify_all();
            }

            if (!batch.empty()) {
                uint64_t delay = g_async_queue.write_delay_ms.load(std::memory_order_relaxed);
                if (delay > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                }
                std::lock_guard file_lock{g_log.mutex};
                RotateLogsIfNeededLocked();
                for (const auto& msg : batch) {
                    if (g_log.file.is_open()) {
                        g_log.file << msg;
                    }
                }
                if (g_log.file.is_open()) {
                    g_log.file.flush();
                }
            }
        }
    });
    g_async_queue.started = true;
}

void StopAsyncWorker() {
    {
        std::lock_guard lock{g_async_queue.mutex};
        g_async_queue.stop_requested = true;
    }
    g_async_queue.cv.notify_all();
    if (g_async_queue.worker.joinable()) {
        g_async_queue.worker.request_stop();
        g_async_queue.worker.join();
    }
    std::lock_guard lock{g_async_queue.mutex};
    g_async_queue.started = false;
}

} // namespace

void Logger::TestReset(std::wstring_view testDir) {
    Shutdown();
    {
        std::lock_guard lock{g_log.mutex};
        g_log.logDir.clear();
        g_log.mainLogPath.clear();
    }
    Initialize(testDir);
}

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

    StartAsyncWorkerIfNeeded();

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

    ::OutputDebugStringA(line.c_str());

    {
        std::lock_guard lock{g_log.mutex};
        if (!g_log.initialised) return;
    }

    StartAsyncWorkerIfNeeded();
    {
        std::lock_guard lock{g_async_queue.mutex};
        if (g_async_queue.queue.size() >= AsyncLogQueue::kMaxQueueSize) {
            g_async_queue.queue.pop_front();
            g_async_queue.dropped_count.fetch_add(1, std::memory_order_relaxed);
        }
        g_async_queue.queue.push_back(std::move(line));
    }
    g_async_queue.cv.notify_one();
}

void Logger::Flush() {
    {
        std::unique_lock lock{g_async_queue.mutex};
        if (g_async_queue.started) {
            g_async_queue.cv_flush.wait(lock, [&] { return g_async_queue.queue.empty(); });
        }
    }
    std::lock_guard lock{g_log.mutex};
    if (g_log.file.is_open()) g_log.file.flush();
}

void Logger::Shutdown() {
    Flush();
    StopAsyncWorker();
    std::lock_guard lock{g_log.mutex};
    if (g_log.file.is_open()) {
        auto line = std::format("[{}] [INFO ] [Logger  ] Session ended\n", Timestamp());
        g_log.file << line;
        g_log.file.flush();
        g_log.file.close();
    }
    g_log.initialised = false;
}

void Logger::SetTestWriteDelay(std::chrono::milliseconds delay) {
    g_async_queue.write_delay_ms.store(delay.count(), std::memory_order_relaxed);
}

uint64_t Logger::GetDroppedLogCount() {
    return g_async_queue.dropped_count.load(std::memory_order_relaxed);
}

} // namespace duwn
