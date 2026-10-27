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

/** Settings pages, in the order the settings window shows them. */
enum class SettingsPage
{
    midiAndAudio,
    sounds,
    practice,
    appearance,
    diagnostics,
};

/** Main window content, built around the song: a header with the menus and
    the piano's connection, the transport, a large piano roll, a status strip
    that says what happens now (and which notes to play), the keyboard, and a
    side panel with hands, accompaniment, tracks and volume. Draws view models
    and posts user intents only. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::MenuBarModel,
                            private juce::ChangeListener
{
public:
    struct Intents
    {
        std::function<void()> chooseMidiFile;
        std::function<void (const juce::File& file)> openMidiFile;
        std::function<void()> closeSong;
        std::function<void()> quit;
        std::function<void (SettingsPage page)> openSettings;
        std::function<void()> showHelp;
        std::function<void()> showAbout;
        std::function<void()> copyDiagnostics;
        std::function<void()> saveDiagnostics;
        std::function<void()> openLogFolder;

        std::function<void()> playPause;
        std::function<void()> stop;
        std::function<void()> silence;
        std::function<void (double seconds)> seek;
        std::function<void (int percent)> tempoChanged;
        std::function<void (float gain)> volumeChanged;
        std::function<void (double pixelsPerSecond)> zoomChanged;

        std::function<void (bool practising)> practiceChanged;
        std::function<void (int mode)> modeChanged;
        std::function<void (int tolerance)> toleranceChanged;
        std::function<void (bool play)> playOtherPartChanged;
        std::function<void (bool play)> playBackingChanged;
        std::function<void (std::size_t part, int assignment)> partAssignmentChanged;
        std::function<void (std::size_t part, bool muted)> partMuteChanged;
        std::function<void (std::size_t part, int soundId)> partSoundChanged; ///< SoundView ids; 0 = default sound
        std::function<void (std::uint8_t pitch, bool down)> keyClicked;

        std::function<void (int soundId)> soundChosen; ///< SoundView ids
        std::function<void()> showInstrumentWindow;
        std::function<void (bool light)> themeChanged;
        std::function<void (double scale)> uiScaleChanged;
    };

    explicit MainComponent (Intents intents);
    ~MainComponent() override;

    void setInputMonitor (const InputMonitorView& view);
    void setSong (std::shared_ptr<const PianoRollView> song);
    /** Shows "Loading <name>..." until setSong or setSongError. */
    void setLoading (const std::string& fileName);
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
    void setTempo (int percent);
    void setRecentFiles (std::vector<std::string> utf8Paths);
    /** Per-part sound choice shown in the tracks list (SoundView ids, 0 = default). */
    void setPartSounds (std::vector<int> soundIds);
    void setAppearance (bool light, double scale);

    bool keyPressed (const juce::KeyPress& key) override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Menus: File, View, Practice, Help.
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

private:
    struct PartRow
    {
        std::size_t part = 0;
        std::unique_ptr<juce::Component> head; ///< label (piano part) or play toggle (other track)
        std::unique_ptr<juce::ComboBox> role;
        std::unique_ptr<juce::ComboBox> sound;
        std::unique_ptr<juce::Label> detail;
    };

    /** The tracks list inside the side panel's scrollable area. */
    class TrackList final : public juce::Component
    {
    public:
        explicit TrackList (MainComponent& owner) : owner_ (owner) {}
        void resized() override;
        int preferredHeight() const;
        std::vector<PartRow> rows;
        juce::Label pianoHeader, otherHeader;

    private:
        MainComponent& owner_;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { showTab (tabs_.getCurrentTabIndex()); }
    void rebuildPartList();
    void showTab (int index);
    void refreshStatus();
    void refreshColours();
    void refreshSoundBoxes();
    void layoutSidebar (juce::Rectangle<int> area);
    juce::ComboBox* makeRoleBox (std::size_t part, int assignment);
    juce::ComboBox* makeSoundBox (std::size_t part);
    void fillSoundBox (juce::ComboBox& box, bool withDefault, bool withActions) const;
    void setMode (int mode);
    juce::String positionText() const;

    Intents intents_;
    InputMonitorView input_;
    std::shared_ptr<const PianoRollView> song_;
    TransportView transport_;
    PracticeView practice_;
    SoundView sound_;
    std::vector<int> partSounds_;
    std::vector<bool> muted_;
    std::vector<std::string> recent_;
    juce::String loadingName_;
    juce::String error_;
    bool dragOver_ = false;
    bool light_ = false;
    double scale_ = 1.0;
    int tempo_ = 100;

    // Header
    juce::Label appName_;
    juce::MenuBarComponent menuBar_ { this };
    juce::TextButton connection_;
    juce::TextButton settingsButton_;

    // Song and transport
    juce::Label crumb_;
    juce::Label title_;
    juce::Label notices_;
    juce::TextButton playButton_, stopButton_, silenceButton_;
    juce::Label positionLabel_;
    juce::Label tempoLabel_;
    juce::ComboBox tempoBox_;

    // Roll area with tabs, and the empty state
    juce::TabbedButtonBar tabs_ { juce::TabbedButtonBar::TabsAtTop };
    int tab_ = 0;
    PianoRollComponent roll_;
    PianoRollComponent tracksRoll_;
    juce::Label tracksEmpty_;
    juce::TextButton emptyOpenButton_;
    juce::Label emptyHint_;
    juce::OwnedArray<juce::TextButton> recentButtons_;

    NextNotesBoard board_;
    KeyboardStrip keyboard_;

    // Side panel
    juce::Label modeCaption_, accompanimentCaption_, volumeCaption_, soundCaption_, timingCaption_;
    juce::TextButton modeBoth_, modeRight_, modeLeft_;
    juce::ToggleButton waitToggle_;
    juce::ComboBox toleranceBox_;
    juce::ToggleButton otherPartToggle_;
    juce::ToggleButton backingToggle_;
    juce::TextButton tracksButton_;
    bool tracksOpen_ = true;
    juce::Viewport tracksView_;
    TrackList trackList_ { *this };
    juce::Slider volume_ { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComboBox soundBox_;
    juce::TextButton instrumentWindowButton_;
    juce::Label ringNote_;

    juce::Rectangle<int> headerArea_, sidebarArea_, rollPanel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
