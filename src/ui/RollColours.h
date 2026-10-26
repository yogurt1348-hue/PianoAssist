#pragma once

#include "ui/PianoRollView.h"

#include <juce_graphics/juce_graphics.h>

namespace pianoassist::ui
{

inline juce::Colour roleColour (RollRole role)
{
    switch (role)
    {
        case RollRole::rightHand: return juce::Colour (0xff4a90e2);
        case RollRole::leftHand:  return juce::Colour (0xfff5a623);
        case RollRole::backing:   return juce::Colour (0xff7f8c8d);
        case RollRole::unknown:   break;
    }
    return juce::Colour (0xffb0b0b0);
}

inline bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

} // namespace pianoassist::ui
