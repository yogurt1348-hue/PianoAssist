#pragma once

#include "ui/PianoRollView.h"

#include "ui/AppLookAndFeel.h"

namespace pianoassist::ui
{

inline juce::Colour roleColour (RollRole role)
{
    switch (role)
    {
        case RollRole::rightHand: return theme::rightHand;
        case RollRole::leftHand:  return theme::leftHand;
        case RollRole::backing:   return theme::backing;
        case RollRole::unknown:   break;
    }
    return theme::backing.brighter (0.3f);
}

inline bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

} // namespace pianoassist::ui
