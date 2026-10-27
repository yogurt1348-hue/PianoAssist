#pragma once

#include "ui/PracticeView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoassist::ui
{

/** The status strip under the roll: what is happening now in one line (no
    file, waiting for notes, paused...), and while practising the notes to
    play in big chips coloured by hand, with the next steps after them. */
class NextNotesBoard final : public juce::Component
{
public:
    enum class Tone
    {
        normal,
        waiting, ///< the song waits for the learner
        warning,
        error,
        good,
    };

    void setView (const PracticeView& view);
    /** Headline and an optional second line; chips show only when showNotes. */
    void setMessage (const juce::String& headline, const juce::String& detail, Tone tone, bool showNotes);
    void paint (juce::Graphics& g) override;

private:
    PracticeView view_;
    juce::String headline_;
    juce::String detail_;
    Tone tone_ = Tone::normal;
    bool showNotes_ = false;
};

} // namespace pianoassist::ui
