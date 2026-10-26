#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pianoassist::ui
{

enum class RollRole : std::uint8_t
{
    rightHand,
    leftHand,
    backing,
    unknown,
};

/** Everything the piano roll draws for a loaded song. Seconds only: the UI
    never sees ticks. */
struct PianoRollView
{
    struct Note
    {
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        std::uint8_t pitch = 60;
        RollRole role = RollRole::unknown;
        int part = -1;
    };

    struct Part
    {
        std::string label;   ///< e.g. "Track 2 'Right Hand' · ch 1"
        std::string detail;  ///< e.g. "8 notes · C4-C5 · track name"
        RollRole role = RollRole::unknown;
        std::string roleText;
    };

    std::string title;              ///< file name
    std::string summary;            ///< e.g. "Format 1 · 3 tracks · 0:06 · 120 BPM"
    std::vector<std::string> notices; ///< repairs and warnings
    std::vector<Note> notes;        ///< sorted by start
    std::vector<Part> parts;
    std::vector<double> barStarts;
    double durationSeconds = 0.0;
    std::uint8_t lowestPitch = 60;
    std::uint8_t highestPitch = 72;
};

} // namespace pianoassist::ui
