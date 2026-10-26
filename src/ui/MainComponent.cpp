#include "ui/MainComponent.h"

namespace pianoassist::ui
{
namespace
{
const juce::Colour bannerColour { 0xffb3541e };
const juce::Colour panelColour { 0xff2a3238 };

void setupCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (15.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
}

void setupValue (juce::Label& label)
{
    label.setFont (juce::FontOptions (22.0f, juce::Font::bold));
}

juce::String toJuce (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}
} // namespace

MainComponent::MainComponent (Intents intents)
    : intents_ (std::move (intents))
{
    inputLabel_.setText ("MIDI input:", juce::dontSendNotification);
    addAndMakeVisible (inputLabel_);

    inputBox_.setTextWhenNothingSelected ("Choose your piano");
    inputBox_.setTextWhenNoChoicesAvailable ("No MIDI inputs found");
    inputBox_.setTitle ("MIDI input device");
    inputBox_.onChange = [this] {
        const auto index = inputBox_.getSelectedItemIndex();
        if (index >= 0 && static_cast<std::size_t> (index) < comboIdentifiers_.size() && intents_.selectInput)
            intents_.selectInput (comboIdentifiers_[static_cast<std::size_t> (index)]);
    };
    addAndMakeVisible (inputBox_);

    rescanButton_.onClick = [this] { if (intents_.rescanInputs) intents_.rescanInputs(); };
    addAndMakeVisible (rescanButton_);
    logButton_.onClick = [this] { if (intents_.openLogFolder) intents_.openLogFolder(); };
    addAndMakeVisible (logButton_);

    statusLabel_.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (statusLabel_);

    bannerLabel_.setColour (juce::Label::backgroundColourId, bannerColour);
    bannerLabel_.setColour (juce::Label::textColourId, juce::Colours::white);
    bannerLabel_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    bannerLabel_.setJustificationType (juce::Justification::centred);
    addChildComponent (bannerLabel_);

    setupCaption (lastNoteCaption_, "Last note");
    setupCaption (heldCaption_, "Held keys");
    setupCaption (sustainCaption_, "Sustain pedal");
    setupCaption (rateCaption_, "Events per second");
    for (auto* value : { &lastNoteValue_, &heldValue_, &sustainValue_, &rateValue_ })
        setupValue (*value);
    for (auto* label : { &lastNoteCaption_, &lastNoteValue_, &heldCaption_, &heldValue_, &sustainCaption_,
                         &sustainValue_, &rateCaption_, &rateValue_ })
        addAndMakeVisible (*label);

    songTitle_.setText ("No song loaded", juce::dontSendNotification);
    songTitle_.setJustificationType (juce::Justification::centred);
    songTitle_.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (songTitle_);
    songHint_.setText ("Opening MIDI files arrives in a later build. Press keys to test your piano above.",
                       juce::dontSendNotification);
    songHint_.setJustificationType (juce::Justification::centred);
    songHint_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (songHint_);

    setInputMonitor ({});
    setSize (1024, 640);
}

void MainComponent::rebuildDeviceList (const InputMonitorView& view)
{
    std::vector<std::string> identifiers;
    for (const auto& device : view.devices)
        identifiers.push_back (device.identifier);

    if (identifiers != comboIdentifiers_)
    {
        comboIdentifiers_ = identifiers;
        inputBox_.clear (juce::dontSendNotification);
        int itemId = 1;
        for (const auto& device : view.devices)
            inputBox_.addItem (toJuce (device.name), itemId++);
    }

    int selected = -1;
    for (std::size_t i = 0; i < comboIdentifiers_.size(); ++i)
        if (comboIdentifiers_[i] == view.selectedIdentifier)
            selected = static_cast<int> (i);
    if (inputBox_.getSelectedItemIndex() != selected)
        inputBox_.setSelectedItemIndex (selected, juce::dontSendNotification);
}

void MainComponent::setInputMonitor (const InputMonitorView& view)
{
    view_ = view;
    rebuildDeviceList (view);

    statusLabel_.setText (toJuce (view.statusText), juce::dontSendNotification);
    const bool ok = view.status == InputMonitorView::Status::connected;
    statusLabel_.setColour (juce::Label::textColourId, ok ? juce::Colours::lightgreen : juce::Colours::orange);

    bannerLabel_.setText (toJuce (view.bannerText), juce::dontSendNotification);
    bannerLabel_.setVisible (! view.bannerText.empty());

    lastNoteValue_.setText (view.lastNote.empty() ? "-" : toJuce (view.lastNote), juce::dontSendNotification);
    heldValue_.setText (view.heldNotes.empty() ? "-" : toJuce (view.heldNotes), juce::dontSendNotification);
    sustainValue_.setText (view.sustainDown ? "Down" : "Up", juce::dontSendNotification);
    rateValue_.setText (juce::String (view.eventsPerSecond, 0), juce::dontSendNotification);
    resized();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    g.setColour (panelColour);
    g.fillRoundedRectangle (lastNoteCaption_.getBounds().getUnion (rateValue_.getBounds()).expanded (12).toFloat(), 8.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto bar = area.removeFromTop (32);
    inputLabel_.setBounds (bar.removeFromLeft (90));
    logButton_.setBounds (bar.removeFromRight (140));
    bar.removeFromRight (8);
    rescanButton_.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (8);
    inputBox_.setBounds (bar);

    area.removeFromTop (6);
    statusLabel_.setBounds (area.removeFromTop (24));

    if (bannerLabel_.isVisible())
    {
        area.removeFromTop (6);
        bannerLabel_.setBounds (area.removeFromTop (36));
    }

    area.removeFromTop (24);
    auto monitor = area.removeFromTop (70).reduced (12, 0);
    const auto columnWidth = monitor.getWidth() / 4;
    const std::pair<juce::Label*, juce::Label*> columns[] = { { &lastNoteCaption_, &lastNoteValue_ },
                                                              { &heldCaption_, &heldValue_ },
                                                              { &sustainCaption_, &sustainValue_ },
                                                              { &rateCaption_, &rateValue_ } };
    for (const auto& [caption, value] : columns)
    {
        auto column = monitor.removeFromLeft (columnWidth);
        caption->setBounds (column.removeFromTop (24));
        value->setBounds (column);
    }

    auto song = area.withSizeKeepingCentre (area.getWidth(), 80);
    songTitle_.setBounds (song.removeFromTop (36));
    songHint_.setBounds (song);
    repaint();
}

} // namespace pianoassist::ui
