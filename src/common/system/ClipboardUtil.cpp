#include "ClipboardUtil.h"
#include <windows.h>

namespace duwn::system {

bool ClipboardUtil::SetText(std::wstring_view text) {
    if (!::OpenClipboard(nullptr)) {
        return false;
    }

    if (!::EmptyClipboard()) {
        ::CloseClipboard();
        return false;
    }

    size_t byte_len = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL h_mem = ::GlobalAlloc(GMEM_MOVEABLE, byte_len);
    if (!h_mem) {
        ::CloseClipboard();
        return false;
    }

    void* mem_ptr = ::GlobalLock(h_mem);
    if (!mem_ptr) {
        ::GlobalFree(h_mem);
        ::CloseClipboard();
        return false;
    }

    memcpy(mem_ptr, text.data(), text.length() * sizeof(wchar_t));
    reinterpret_cast<wchar_t*>(mem_ptr)[text.length()] = L'\0';
    ::GlobalUnlock(h_mem);

    if (!::SetClipboardData(CF_UNICODETEXT, h_mem)) {
        ::GlobalFree(h_mem);
        ::CloseClipboard();
        return false;
    }

    ::CloseClipboard();
    return true;
}

} // namespace duwn::system
