#include "ui/MainComponent.h"

namespace pianoassist::ui
{

MainComponent::MainComponent()
{
    title_.setText ("No song loaded", juce::dontSendNotification);
    title_.setJustificationType (juce::Justification::centred);
    title_.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (title_);

    hint_.setText ("Connect your MIDI piano by USB. Opening MIDI files arrives in a later build.",
                   juce::dontSendNotification);
    hint_.setJustificationType (juce::Justification::centred);
    hint_.setFont (juce::FontOptions (16.0f));
    addAndMakeVisible (hint_);

    setSize (1024, 640);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    const auto centre = area.withSizeKeepingCentre (area.getWidth(), 96);
    auto top = centre;
    title_.setBounds (top.removeFromTop (40));
    hint_.setBounds (top);
}

} // namespace pianoassist::ui
