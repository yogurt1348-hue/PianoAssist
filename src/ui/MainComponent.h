#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoassist::ui
{

/** Phase 0 empty state. Later phases replace the body with the piano roll;
    the component only draws view models and posts user intents. */
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label title_;
    juce::Label hint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
