#pragma once
// ThreadUtil — RAII helpers for Windows thread management.
// Prefer std::jthread for new code. These helpers set thread names visible
// in the Visual Studio debugger and Windows Performance Analyzer.

#include <string_view>
#include <thread>
#include <windows.h>

namespace duwn::threading {

// Set the name of the current thread (visible in VS debugger + WPA).
// Call from inside the thread, not from the spawning thread.
void SetCurrentThreadName(std::wstring_view name) noexcept;

// Bump the current thread's priority for time-sensitive threads.
// Use THREAD_PRIORITY_ABOVE_NORMAL for render/audio, not REALTIME.
// REALTIME requires SeIncreaseBasePriorityPrivilege and can starve the system.
void SetCurrentThreadPriority(int priority) noexcept;

} // namespace duwn::threading
