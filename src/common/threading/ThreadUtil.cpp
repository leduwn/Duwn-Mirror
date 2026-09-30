#include "ThreadUtil.h"
#include <windows.h>
#include <processthreadsapi.h>

namespace duwn::threading {

void SetCurrentThreadName(std::wstring_view name) noexcept {
    // SetThreadDescription available on Windows 10 1607+.
    ::SetThreadDescription(::GetCurrentThread(), name.data());
}

void SetCurrentThreadPriority(int priority) noexcept {
    ::SetThreadPriority(::GetCurrentThread(), priority);
}

} // namespace duwn::threading
