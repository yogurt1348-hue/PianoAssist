#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace pianoassist::ui
{

/** 88-key keyboard (A0-C8) along the bottom. Held keys are green; in
    practice the keys to play next are yellow and wrong presses flash red. */
class KeyboardStrip final : public juce::Component
{
public:
    void setHeldPitches (std::vector<std::uint8_t> pitches);
    void setPracticePitches (std::vector<std::uint8_t> required, std::vector<std::uint8_t> wrong);
    void paint (juce::Graphics& g) override;

private:
    std::vector<std::uint8_t> held_;
    std::vector<std::uint8_t> required_;
    std::vector<std::uint8_t> wrong_;
};

} // namespace pianoassist::ui
