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
};

} // namespace pianoassist::ui
