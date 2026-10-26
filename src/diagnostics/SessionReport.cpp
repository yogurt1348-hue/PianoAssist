#include "diagnostics/SessionReport.h"

namespace pianoassist::diagnostics
{

void logSessionStart (RotatingLog& log, const SessionInfo& info)
{
    std::string inputs;
    for (const auto& name : info.midiInputNames)
        inputs += (inputs.empty() ? "" : "; ") + name;

    log.write (LogLevel::info, "session", "Session started",
               {
                   { "appVersion", info.appVersion },
                   { "os", info.osName },
                   { "midiInputs", inputs.empty() ? "(none)" : inputs },
                   { "selectedMidiInput", info.selectedMidiInput.empty() ? "(none)" : info.selectedMidiInput },
                   { "audioDevice", info.audioDevice.empty() ? "(not opened)" : info.audioDevice },
               });
}

} // namespace pianoassist::diagnostics
