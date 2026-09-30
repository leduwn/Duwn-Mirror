#pragma once

#include "ui/resource.h"
#include <windows.h>

namespace duwn::app {

inline HICON LoadAppIcon(HWND hwnd, bool use_small) noexcept {
    const UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : ::GetDpiForSystem();
    const int metric = use_small ? SM_CXSMICON : SM_CXICON;
    const int size = ::GetSystemMetricsForDpi(metric, dpi);
    return static_cast<HICON>(::LoadImageW(
        ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, size, size, LR_SHARED));
}

inline void SetAppWindowIcons(HWND hwnd) noexcept {
    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(LoadAppIcon(hwnd, false)));
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(LoadAppIcon(hwnd, true)));
}

} // namespace duwn::app
