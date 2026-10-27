#pragma once

#include <string>
#include <vector>

namespace pianoassist::ui
{

/** The "Sound" chooser. Ids: builtIn, soundFont, firstPlugin + index. */
struct SoundView
{
    static constexpr int builtIn = 1;
    static constexpr int soundFont = 2;
    static constexpr int firstPlugin = 100;

    std::vector<std::string> plugins; ///< VST3 instruments found, by name
    std::string soundFontName;        ///< empty when none is loaded
    int selected = builtIn;
    bool canEdit = false;             ///< a plug-in window is available
    bool scanning = false;
    std::string status;               ///< last result or error, shown as a tooltip/note
    std::string scanProgress;         ///< "Scanning 3 of 12: X.vst3", empty when idle
    std::vector<std::string> folders;        ///< folders scanned for VST3 (standard ones first)
    std::size_t standardFolderCount = 0;     ///< the first N folders cannot be removed
    std::vector<std::string> problems;       ///< plug-ins skipped and why, missing instruments
};

} // namespace pianoassist::ui
