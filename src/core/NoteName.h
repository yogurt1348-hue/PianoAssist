#pragma once

#include <string>

namespace pianoassist
{

/** Scientific pitch name with MIDI 60 = C4, e.g. 61 -> "C#4". */
std::string noteName (int midiPitch);

/** Russian solfège name without octave, UTF-8, e.g. 60 -> "до", 61 -> "до#". */
std::string solfegeName (int midiPitch);

} // namespace pianoassist
