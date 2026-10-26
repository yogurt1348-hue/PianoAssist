#include "core/NoteName.h"

#include <array>

namespace pianoassist
{

std::string noteName (int midiPitch)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F",
                                                         "F#", "G", "G#", "A", "A#", "B" };
    if (midiPitch < 0 || midiPitch > 127)
        return "?";
    return std::string (names[static_cast<std::size_t> (midiPitch % 12)]) + std::to_string (midiPitch / 12 - 1);
}

} // namespace pianoassist
