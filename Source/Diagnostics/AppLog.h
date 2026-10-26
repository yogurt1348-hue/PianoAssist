// AppLog — where Cadenza writes its diagnostic log, shared by the app start-up
// code (which opens the log) and the UI (which opens / exports it).
//
//   %APPDATA%/Cadenza/cadenza.log           this run
//   %APPDATA%/Cadenza/cadenza-previous.log  the run before (kept so a crash or
//                                           a closed app does not lose the log)

#pragma once

#include <juce_core/juce_core.h>

namespace cadenza::diagnostics
{
inline juce::File logDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Cadenza");
}

inline juce::File currentLogFile() { return logDirectory().getChildFile("cadenza.log"); }
inline juce::File previousLogFile() { return logDirectory().getChildFile("cadenza-previous.log"); }

// Moves the last run's log aside (replacing the older one) before a new run starts.
inline void rotateLogs()
{
    const auto current = currentLogFile();
    if (!current.existsAsFile()) return;
    previousLogFile().deleteFile();
    if (!current.moveFileTo(previousLogFile())) current.deleteFile();
}

// Writes one plain-text report (system info + this run's log + the previous
// run's log) into `folder` and returns it, or an empty File on failure.
inline juce::File exportLogReport(const juce::File& folder, const juce::String& appVersion)
{
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto out = folder.getChildFile("PianoAssist-log-" + stamp + ".txt");

    juce::String text;
    text << "PianoAssist / Cadenza diagnostic report\n"
         << "Created:  " << juce::Time::getCurrentTime().toString(true, true, true, true) << "\n"
         << "Version:  " << appVersion << "\n"
         << "OS:       " << juce::SystemStats::getOperatingSystemName()
         << (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : "") << "\n"
         << "CPU:      " << juce::SystemStats::getCpuModel() << ", " << juce::SystemStats::getNumCpus() << " cores\n"
         << "Memory:   " << juce::SystemStats::getMemorySizeInMegabytes() << " MB\n";

    auto appendFile = [&text](const juce::String& title, const juce::File& f) {
        text << "\n==================== " << title << " (" << f.getFullPathName() << ") ====================\n";
        text << (f.existsAsFile() ? f.loadFileAsString() : juce::String("(not found)\n"));
    };
    appendFile("This run", currentLogFile());
    appendFile("Previous run", previousLogFile());

    if (!folder.createDirectory() || !out.replaceWithText(text)) return {};
    return out;
}
}
