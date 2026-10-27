#pragma once

#include "ui/InputMonitorView.h"
#include "ui/KeyboardStrip.h"
#include "ui/NextNotesBoard.h"
#include "ui/PianoRollComponent.h"
#include "ui/PianoRollView.h"
#include "ui/PracticeView.h"
#include "ui/RollLegend.h"
#include "ui/SoundView.h"
#include "ui/TracksComponent.h"
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
                            private juce::ChangeListener,
                            private juce::Timer
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
        std::function<void (bool enabled, double startSeconds, double endSeconds)> loopChanged;
        std::function<void (float gain)> volumeChanged;
        std::function<void (double pixelsPerSecond)> zoomChanged;

        std::function<void (bool practising)> practiceChanged;
        std::function<void (int mode)> modeChanged;
        std::function<void (int tolerance)> toleranceChanged;
        std::function<void (bool play)> playOtherPartChanged;
        std::function<void (bool play)> playBackingChanged;
        std::function<void (bool on)> countInChanged;
        std::function<void()> showTracksWindow;
        /** The side panel was resized or collapsed (saved for the next start). */
        std::function<void (int width, bool collapsed)> sidebarChanged;
        std::function<void (bool follow)> followChanged;
        std::function<void()> openSongFolder;
        std::function<void()> showLibrary;
        std::function<void()> openLibraryFolder;
        std::function<void()> checkForUpdates;
        std::function<void (std::uint8_t pitch, bool down)> keyClicked;

        std::function<void (int soundId)> soundChosen; ///< SoundView ids
        std::function<void()> showInstrumentWindow;
        std::function<void (bool light)> themeChanged;
        std::function<void (double scale)> uiScaleChanged;

        // Piano roll view: never change what sounds or what is checked.
        std::function<void (std::size_t part, bool visible)> partVisibleChanged;
        std::function<void (int part)> selectPart;
        std::function<void (std::size_t part)> showOnlyPart;
        std::function<void()> showAllParts;
        std::function<void()> clearSolos;
        std::function<void (bool byTrack)> colourByTrackChanged;
        std::function<void (bool hide)> hideDimmedChanged;
        std::function<void (bool pianoOnly)> pianoOnlyChanged;
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
    void setLoop (bool enabled, double startSeconds, double endSeconds);
    /** The tracks as the tracks window shows them; the side panel shows a summary. */
    void setTracks (const std::vector<TrackRowView>& rows, int selected = -1);
    /** Roll colours by hand or by track, faint tracks hidden, piano parts only. */
    void setViewOptions (bool colourByTrack, bool hideDimmed, bool pianoOnly);
    /** Side panel width and state, and whether the roll follows the playhead. */
    void setLayoutChoices (int sidebarWidth, bool collapsed, bool follow);
    /** A short message that goes away by itself. */
    void showToast (const juce::String& text, int showForMs = 7000);
    void setRecentFiles (std::vector<std::string> utf8Paths);
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
    /** The draggable edge between the roll and the side panel. */
    class Splitter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Splitter (MainComponent& owner) : owner_ (owner) { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

    private:
        MainComponent& owner_;
        int startWidth_ = 0;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { showTab (tabs_.getCurrentTabIndex()); }
    void showTab (int index);
    void refreshStatus();
    void refreshColours();
    void refreshRollStyles();
    void refreshSoundBoxes();
    void layoutSidebar (juce::Rectangle<int> area);
    void fillSoundBox (juce::ComboBox& box, bool withDefault, bool withActions) const;
    void timerCallback() override;
    void setSidebar (int width, bool collapsed, bool notify);
    int maxSidebarWidth() const;
    void refreshModeBadge();
    void setMode (int mode);
    juce::String positionText() const;

    Intents intents_;
    InputMonitorView input_;
    std::shared_ptr<const PianoRollView> song_;
    TransportView transport_;
    PracticeView practice_;
    SoundView sound_;
    std::vector<TrackRowView> tracks_;
    int selectedPart_ = -1;
    bool colourByTrack_ = false, hideDimmed_ = false, pianoOnly_ = false;
    bool loopEnabled_ = false;
    double loopStart_ = 0.0, loopEnd_ = 0.0;
    void toggleLoop();
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
    juce::TextButton loopButton_;
    juce::Label positionLabel_;
    juce::Label tempoLabel_;
    juce::ComboBox tempoBox_;

    // Roll area with tabs, and the empty state
    juce::TabbedButtonBar tabs_ { juce::TabbedButtonBar::TabsAtTop };
    int tab_ = 0;
    PianoRollComponent roll_;
    PianoRollComponent tracksRoll_;
    juce::Label tracksEmpty_;
    RollLegend legend_;
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
    juce::Label tracksSummary_;
    juce::Viewport sideView_;
    juce::Component sideContent_;
    juce::Slider volume_ { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComboBox soundBox_;
    juce::TextButton instrumentWindowButton_;
    juce::Label ringNote_;

    juce::Rectangle<int> headerArea_, sidebarArea_, rollPanel_;

    // Side panel width: dragged on the splitter, collapsed with the arrow.
    static constexpr int defaultSidebarWidth = 300;
    static constexpr int minSidebarWidth = 260;
    static constexpr int minRollWidth = 560;
    int sidebarWidth_ = defaultSidebarWidth;
    bool sidebarCollapsed_ = false;
    Splitter splitter_ { *this };
    juce::TextButton collapseButton_;

    // Mode of the transport and the roll's view
    juce::Label modeBadge_;
    juce::TextButton zoomOut_, zoomIn_, zoomAll_, toPlayhead_;
    juce::ToggleButton followToggle_;
    bool follow_ = true;
    juce::Label toast_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pianoassist::ui
