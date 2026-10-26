#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace pianoassist::ui
{

/** 88-key keyboard (A0-C8) along the bottom; keys you press light up. */
class KeyboardStrip final : public juce::Component
{
public:
    void setHeldPitches (std::vector<std::uint8_t> pitches);
    void paint (juce::Graphics& g) override;

private:
    std::vector<std::uint8_t> held_;
};

} // namespace pianoassist::ui
