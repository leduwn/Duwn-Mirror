#pragma once

#include "ui/UiState.h"
#include "Settings.h"
#include <string>

namespace duwn::app {

class DiagnosticsCollector {
public:
    static std::wstring BuildReport(const ui::UiState& state, const Settings& settings);
};

} // namespace duwn::app
