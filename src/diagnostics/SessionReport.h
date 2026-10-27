#pragma once

#include "diagnostics/RotatingLog.h"

#include <string>
#include <vector>

namespace pianoassist::diagnostics
{

/** What the app knows about its environment at startup. Device names are
    driver-provided display names, never paths or user content. */
struct SessionInfo
{
    std::string appVersion;
    std::string osName;
    std::vector<std::string> midiInputNames;
    std::string selectedMidiInput; ///< empty when none is selected
    std::string audioDevice;       ///< empty until audio is opened
};

void logSessionStart (RotatingLog& log, const SessionInfo& info);

} // namespace pianoassist::diagnostics
