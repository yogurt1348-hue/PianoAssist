#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace pianoassist::ui
{

/** 88-key keyboard (A0-C8) along the bottom. Held keys are green; in
    practice the keys to play next are yellow and wrong presses flash red.
    White keys are labelled (C D E ... and до ре ми); clicking a key sounds it. */
class KeyboardStrip final : public juce::Component
{
public:
    void setHeldPitches (std::vector<std::uint8_t> pitches);
    void setPracticePitches (std::vector<std::uint8_t> required, std::vector<std::uint8_t> wrong);

    std::function<void (std::uint8_t pitch, bool down)> onKey;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    std::optional<std::uint8_t> pitchAt (juce::Point<float> point) const;
    void press (std::optional<std::uint8_t> pitch);

    std::vector<std::uint8_t> held_;
    std::vector<std::uint8_t> required_;
    std::vector<std::uint8_t> wrong_;
    std::optional<std::uint8_t> clicked_;
};

} // namespace pianoassist::ui
