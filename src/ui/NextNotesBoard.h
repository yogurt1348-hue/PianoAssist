#pragma once

#include "ui/PracticeView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoassist::ui
{

/** Large "what to play now" board: the current step's notes in big chips,
    coloured by hand, with the next few steps beside them. */
class NextNotesBoard final : public juce::Component
{
public:
    void setView (const PracticeView& view);
    void paint (juce::Graphics& g) override;

private:
    PracticeView view_;
};

} // namespace pianoassist::ui
