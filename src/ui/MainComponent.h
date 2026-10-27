#pragma once

#include "ui/InputMonitorView.h"
#include "ui/KeyboardStrip.h"
#include "ui/NextNotesBoard.h"
#include "ui/PianoRollComponent.h"
#include "ui/PianoRollView.h"
#include "ui/PracticeView.h"
#include "ui/SoundView.h"
#include "ui/TransportView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

namespace pianoassist::ui
{

/** Main window content: input bar, input monitor, song bar, part list,
    piano roll and keyboard. Draws view models and posts user intents only. */
class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    struct Intents
    {
        std::function<void (const std::string& identifier)> selectInput;
        std::function<void()> rescanInputs;
        std::function<void()> openLogFolder;
        std::function<void()> copyDiagnostics;
        std::function<void()> saveDiagnostics;
        std::function<void()> chooseMidiFile;
        std::function<void (const juce::File& file)> openMidiFile;
        std::function<void (std::size_t part, bool muted)> partMuteChanged; ///< "Other tracks" tab
        std::function<void (double pixelsPerSecond)> zoomChanged;
        std::function<void()> playPause;
        std::function<void()> stop;
        std::function<void()> silence;
        std::function<void (double seconds)> seek;
        std::function<void (float gain)> volumeChanged;
        std::function<void (bool practising)> practiceChanged;
        std::function<void (int mode)> modeChanged;
        std::function<void (int tolerance)> toleranceChanged;
        std::function<void (std::uint8_t pitch, bool down)> keyClicked;
        std::function<void (bool play)> playOtherPartChanged;
        std::function<void (bool play)> playBackingChanged;
        std::function<void (std::size_t part, int assignment)> partAssignmentChanged;
        std::function<void (int soundId)> soundChosen; ///< SoundView ids
        std::function<void()> loadSoundFont;
        std::function<void()> addPluginFile;
        std::function<void()> scanPlugins;
        std::function<void()> showInstrumentWindow;
    };

    explicit MainComponent (Intents intents);

    void setInputMonitor (const InputMonitorView& view);
    void setSong (std::shared_ptr<const PianoRollView> song);
    void setSongError (const std::string& message);
    void setPixelsPerSecond (double pixelsPerSecond)
    {
        roll_.setPixelsPerSecond (pixelsPerSecond);
        tracksRoll_.setPixelsPerSecond (pixelsPerSecond);
    }
    void setTransport (const TransportView& view);
    void setPractice (const PracticeView& view);
    void setSound (const SoundView& view);
    void setVolume (float gain) { volume_.setValue (gain, juce::dontSendNotification); }

    bool keyPressed (const juce::KeyPress& key) override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { showTab (tabs_.getCurrentTabIndex()); }
    void rebuildDeviceList (const InputMonitorView& view);
    void rebuildPartList();
    void showTab (int index);
    juce::ComboBox* makeRoleBox (std::size_t part, int assignment, juce::Colour colour);

    Intents intents_;
    InputMonitorView view_;
    std::shared_ptr<const PianoRollView> song_;

    juce::Label inputLabel_;
    juce::ComboBox inputBox_;
    juce::TextButton rescanButton_ { "Rescan" };
    juce::TextButton logButton_ { "Diagnostics..." };
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

    // Practice tab: the piano parts and their hands.
    juce::Label partsHeader_;
    struct PartRow
    {
        std::size_t part = 0;
        std::unique_ptr<juce::Component> head; ///< label (piano part) or play toggle (other track)
        std::unique_ptr<juce::ComboBox> role;
        std::unique_ptr<juce::Label> detail;
    };
    std::vector<PartRow> pianoRows_;
    // Other tracks tab: backing parts with a play toggle each.
    juce::Label tracksHeader_;
    juce::Label tracksEmpty_;
    std::vector<PartRow> trackRows_;
    std::vector<bool> muted_;
    juce::TabbedButtonBar tabs_ { juce::TabbedButtonBar::TabsAtTop };
    int tab_ = 0;
    std::vector<juce::Rectangle<int>> panels_;

    juce::TextButton playButton_ { "Play" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton silenceButton_ { "Silence" };
    juce::Slider volume_ { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label volumeLabel_;
    juce::Label timeLabel_;
    juce::Label soundLabel_;
    juce::ComboBox soundBox_;
    juce::TextButton instrumentWindowButton_ { "Instrument..." };
    SoundView sound_;
    juce::Label transportNote_;
    TransportView transport_;

    juce::ToggleButton practiceToggle_ { "Practice: wait for my notes" };
    juce::ComboBox modeBox_;
    juce::ComboBox toleranceBox_;
    juce::ToggleButton otherPartToggle_ { "Play other hand" };
    juce::ToggleButton backingToggle_ { "Play backing" };
    juce::Label gateLabel_;
    NextNotesBoard board_;

    PianoRollComponent roll_;
    PianoRollComponent tracksRoll_;
    KeyboardStrip keyboard_;

    std::vector<std::string> comboIdentifiers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
