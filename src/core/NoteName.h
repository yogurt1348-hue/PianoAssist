#pragma once

#include <string>

namespace pianoassist
{

/** Scientific pitch name with MIDI 60 = C4, e.g. 61 -> "C#4". */
std::string noteName (int midiPitch);

} // namespace pianoassist
