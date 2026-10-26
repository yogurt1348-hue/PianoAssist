#pragma once

#include "ui/InputMonitorView.h"
#include "ui/KeyboardStrip.h"
#include "ui/PianoRollComponent.h"
#include "ui/PianoRollView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

namespace pianoassist::ui
{

/** Main window content: input bar, input monitor, song bar, part list,
    piano roll and keyboard. Draws view models and posts user intents only. */
class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    struct Intents
    {
        std::function<void (const std::string& identifier)> selectInput;
        std::function<void()> rescanInputs;
        std::function<void()> openLogFolder;
        std::function<void()> chooseMidiFile;
        std::function<void (const juce::File& file)> openMidiFile;
        std::function<void (std::size_t part, bool visible)> partVisibilityChanged;
        std::function<void (double pixelsPerSecond)> zoomChanged;
    };

    explicit MainComponent (Intents intents);

    void setInputMonitor (const InputMonitorView& view);
    void setSong (std::shared_ptr<const PianoRollView> song);
    void setSongError (const std::string& message);
    void setPixelsPerSecond (double pixelsPerSecond) { roll_.setPixelsPerSecond (pixelsPerSecond); }

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void rebuildDeviceList (const InputMonitorView& view);
    void rebuildPartList();

    Intents intents_;
    InputMonitorView view_;
    std::shared_ptr<const PianoRollView> song_;

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

    juce::TextButton openButton_ { "Open MIDI file..." };
    juce::Label songTitle_;
    juce::Label songSummary_;
    juce::Label songNotices_;
    juce::Label emptyHint_;

    juce::Label partsHeader_;
    juce::OwnedArray<juce::ToggleButton> partToggles_;
    juce::OwnedArray<juce::Label> partDetails_;

    PianoRollComponent roll_;
    KeyboardStrip keyboard_;

    std::vector<std::string> comboIdentifiers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
