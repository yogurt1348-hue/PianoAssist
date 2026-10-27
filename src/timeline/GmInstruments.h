#pragma once

#include <string_view>

namespace pianoassist::timeline
{

/** General MIDI program name for 0-127, e.g. 33 -> "Electric Bass (finger)";
    "Piano (no program)" for -1 and "Drums" for channel 10 parts. */
std::string_view gmProgramName (int program, bool drumChannel = false) noexcept;

} // namespace pianoassist::timeline
