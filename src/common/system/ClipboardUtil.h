#pragma once

#include <string_view>

namespace duwn::system {

class ClipboardUtil {
public:
    static bool SetText(std::wstring_view text);
};

} // namespace duwn::system
