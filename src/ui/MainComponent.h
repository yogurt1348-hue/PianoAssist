#pragma once

#include "ui/InputMonitorView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

namespace pianoassist::ui
{

/** Main window content: device bar, input monitor and the song area (empty
    until phase 2). Draws view models and posts user intents only. */
class MainComponent final : public juce::Component
{
public:
    struct Intents
    {
        std::function<void (const std::string& identifier)> selectInput;
        std::function<void()> rescanInputs;
        std::function<void()> openLogFolder;
    };

    explicit MainComponent (Intents intents);

    void setInputMonitor (const InputMonitorView& view);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildDeviceList (const InputMonitorView& view);

    Intents intents_;
    InputMonitorView view_;

    juce::Label inputLabel_;
    juce::ComboBox inputBox_;
    juce::TextButton rescanButton_ { "Rescan" };
    juce::TextButton logButton_ { "Open log folder" };
    juce::Label statusLabel_;
    juce::Label bannerLabel_;

    juce::Label lastNoteCaption_, lastNoteValue_;
    juce::Label heldCaption_, heldValue_;
    juce::Label sustainCaption_, sustainValue_;
    juce::Label rateCaption_, rateValue_;

    juce::Label songTitle_;
    juce::Label songHint_;

    std::vector<std::string> comboIdentifiers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
