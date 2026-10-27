#include "ui/MainComponent.h"

#include "ui/AppLookAndFeel.h"
#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int headerHeight = 58;
constexpr int gap = 10;
constexpr int tempoChoices[] = { 25, 40, 50, 60, 70, 75, 80, 90, 100, 110, 120, 150, 200 };

bool isPianoAssignment (int assignment)
{
    return assignment <= 2; // 0 right, 1 left, 2 split; 3 accompaniment, 4 excluded
}

juce::String toJuce (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

juce::String formatTime (double seconds)
{
    const auto total = static_cast<int> (std::max (0.0, seconds));
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

bool isMidiFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi") || path.endsWithIgnoreCase (".kar");
}

void setupCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
}

juce::String modeName (int mode)
{
    return mode == 1 ? TRANS ("Right hand") : mode == 2 ? TRANS ("Left hand") : TRANS ("Both hands");
}

struct shortcut
{
    explicit shortcut (juce::String keys) : text (std::move (keys)) {}
    juce::String text;
};

juce::PopupMenu::Item operator| (juce::PopupMenu::Item item, const shortcut& keys)
{
    item.shortcutKeyDescription = keys.text;
    return item;
}

juce::String noteList (const std::vector<std::uint8_t>& pitches)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::String text;
    for (const auto pitch : pitches)
        text << (text.isEmpty() ? "" : " ") << names[pitch % 12] << (pitch / 12 - 1);
    return text;
}
} // namespace

// ---------------------------------------------------------------------------

void MainComponent::Splitter::paint (juce::Graphics& g)
{
    const bool hot = isMouseOverOrDragging();
    g.setColour (hot ? theme::accent.withAlpha (0.7f) : theme::panelEdge);
    g.fillRect (getLocalBounds().withSizeKeepingCentre (hot ? 3 : 1, getHeight()));
    // Grip dots in the middle
    g.setColour (hot ? theme::accent : theme::mutedText.withAlpha (0.6f));
    const auto c = getLocalBounds().getCentre().toFloat();
    for (int i = -2; i <= 2; ++i)
        g.fillEllipse (c.x - 1.5f, c.y + static_cast<float> (i) * 7.0f - 1.5f, 3.0f, 3.0f);
}

void MainComponent::Splitter::mouseDown (const juce::MouseEvent&)
{
    startWidth_ = owner_.sidebarCollapsed_ ? 0 : owner_.sidebarWidth_;
}

void MainComponent::Splitter::mouseDrag (const juce::MouseEvent& e)
{
    const int width = startWidth_ - e.getDistanceFromDragStartX();
    if (width < minSidebarWidth / 2)
        owner_.setSidebar (owner_.sidebarWidth_, true, false); // dragged nearly shut: collapse
    else
        owner_.setSidebar (std::clamp (width, minSidebarWidth, owner_.maxSidebarWidth()), false, false);
}

void MainComponent::Splitter::mouseUp (const juce::MouseEvent&)
{
    owner_.setSidebar (owner_.sidebarWidth_, owner_.sidebarCollapsed_, true);
}

void MainComponent::Splitter::mouseDoubleClick (const juce::MouseEvent&)
{
    owner_.setSidebar (defaultSidebarWidth, false, true);
}

// ---------------------------------------------------------------------------

MainComponent::MainComponent (Intents intents)
    : intents_ (std::move (intents))
{
    // Header
    appName_.setText ("PianoAssist", juce::dontSendNotification);
    appName_.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    addAndMakeVisible (appName_);
    menuBar_.setTitle (TRANS ("Menu"));
    addAndMakeVisible (menuBar_);
    connection_.setTooltip (TRANS ("Piano connection. Click to choose the MIDI input."));
    connection_.onClick = [this] { if (intents_.openSettings) intents_.openSettings (SettingsPage::midiAndAudio); };
    addAndMakeVisible (connection_);
    settingsButton_.setButtonText (TRANS ("Settings"));
    settingsButton_.setTooltip (TRANS ("MIDI and audio, sounds, practice, appearance, diagnostics (Ctrl+,)"));
    settingsButton_.onClick = [this] { if (intents_.openSettings) intents_.openSettings (SettingsPage::midiAndAudio); };
    addAndMakeVisible (settingsButton_);

    // Song and transport
    crumb_.setFont (juce::FontOptions (12.0f));
    title_.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    title_.setMinimumHorizontalScale (0.6f);
    notices_.setFont (juce::FontOptions (13.0f));
    for (auto* c : std::initializer_list<juce::Component*> { &crumb_, &title_, &notices_ })
        addAndMakeVisible (c);

    playButton_.onClick = [this] { if (intents_.playPause) intents_.playPause(); };
    playButton_.setTooltip (TRANS ("Play / Pause (Space)"));
    stopButton_.setButtonText (TRANS ("Stop"));
    stopButton_.setTooltip (TRANS ("Stop and go back to the start"));
    stopButton_.onClick = [this] { if (intents_.stop) intents_.stop(); };
    silenceButton_.setButtonText (TRANS ("Silence"));
    silenceButton_.setTooltip (TRANS ("Cut every sound at once, including notes still fading (Esc)"));
    silenceButton_.onClick = [this] { if (intents_.silence) intents_.silence(); };
    loopButton_.setButtonText (TRANS ("Loop"));
    loopButton_.setClickingTogglesState (false);
    loopButton_.setTooltip (TRANS ("Repeat a range. Drag along the bar numbers above the roll to choose it (Ctrl+L)."));
    loopButton_.onClick = [this] { toggleLoop(); };
    positionLabel_.setFont (juce::FontOptions (14.0f));
    tempoLabel_.setText (TRANS ("Tempo"), juce::dontSendNotification);
    tempoLabel_.setJustificationType (juce::Justification::centredRight);
    for (const auto percent : tempoChoices)
        tempoBox_.addItem (juce::String (percent) + "%", percent);
    tempoBox_.setSelectedId (100, juce::dontSendNotification);
    tempoBox_.setTitle (TRANS ("Tempo"));
    tempoBox_.setTooltip (TRANS ("Playback speed. Slower tempo makes practice easier; the song stays in tune."));
    tempoBox_.onChange = [this] {
        tempo_ = tempoBox_.getSelectedId();
        if (tempo_ > 0 && intents_.tempoChanged)
            intents_.tempoChanged (tempo_);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &playButton_, &stopButton_, &silenceButton_, &loopButton_, &positionLabel_,
                                                              &tempoLabel_, &tempoBox_ })
        addAndMakeVisible (c);

    // Roll, tabs and the empty state
    tabs_.addTab (TRANS ("My parts"), juce::Colours::transparentBlack, 0);
    tabs_.addTab (TRANS ("Other tracks"), juce::Colours::transparentBlack, 1);
    tabs_.setCurrentTabIndex (0, false);
    tabs_.addChangeListener (this);
    tabs_.setTitle (TRANS ("Piano roll view"));
    addChildComponent (tabs_);
    for (auto* roll : { &roll_, &tracksRoll_ })
    {
        roll->onZoomChanged = [this, roll] (double pps) {
            (roll == &roll_ ? tracksRoll_ : roll_).setPixelsPerSecond (pps);
            if (intents_.zoomChanged)
                intents_.zoomChanged (pps);
        };
        roll->onSeekRequested = [this] (double seconds) { if (intents_.seek) intents_.seek (seconds); };
        roll->onLoopSelected = [this] (double start, double end) {
            setLoop (true, start, end);
            if (intents_.loopChanged)
                intents_.loopChanged (true, start, end);
        };
        addChildComponent (roll);
    }
    roll_.setTitle (TRANS ("Piano roll of your parts"));
    tracksRoll_.setTitle (TRANS ("Piano roll of the other tracks"));
    tracksEmpty_.setText (TRANS ("This song has no other tracks: every part is piano."), juce::dontSendNotification);
    tracksEmpty_.setJustificationType (juce::Justification::centred);
    addChildComponent (tracksEmpty_);

    emptyOpenButton_.setButtonText (TRANS ("Open MIDI file..."));
    emptyOpenButton_.setClickingTogglesState (false);
    emptyOpenButton_.setToggleState (true, juce::dontSendNotification); // drawn in the accent colour
    emptyOpenButton_.onClick = [this] { if (intents_.chooseMidiFile) intents_.chooseMidiFile(); };
    addAndMakeVisible (emptyOpenButton_);
    emptyHint_.setText (TRANS ("or drag a .mid file onto this window"), juce::dontSendNotification);
    emptyHint_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyHint_);

    board_.setTitle (TRANS ("Status and notes to play"));
    addAndMakeVisible (board_);
    keyboard_.setTitle (TRANS ("On-screen piano keyboard; click a key to hear it"));
    keyboard_.onKey = [this] (std::uint8_t pitch, bool down) { if (intents_.keyClicked) intents_.keyClicked (pitch, down); };
    addAndMakeVisible (keyboard_);

    // Side panel
    setupCaption (modeCaption_, TRANS ("Mode"));
    setupCaption (timingCaption_, TRANS ("Timing"));
    setupCaption (accompanimentCaption_, TRANS ("Accompaniment"));
    setupCaption (volumeCaption_, TRANS ("Volume"));
    setupCaption (soundCaption_, TRANS ("Sound"));
    modeBoth_.setButtonText (TRANS ("Both"));
    modeRight_.setButtonText (TRANS ("Right"));
    modeLeft_.setButtonText (TRANS ("Left"));
    int index = 0;
    for (auto* button : { &modeBoth_, &modeRight_, &modeLeft_ })
    {
        button->setRadioGroupId (1001);
        button->setClickingTogglesState (true);
        button->setTooltip (TRANS ("Which hand you play (Ctrl+1/2/3). The app plays the rest."));
        button->onClick = [this, index] { setMode (index); };
        ++index;
    }
    modeBoth_.setToggleState (true, juce::dontSendNotification);
    waitToggle_.setButtonText (TRANS ("Wait for my notes"));
    waitToggle_.setTooltip (TRANS ("The song stops at each note or chord of your hand until you play it (Ctrl+P)"));
    waitToggle_.onClick = [this] { if (intents_.practiceChanged) intents_.practiceChanged (waitToggle_.getToggleState()); };
    toleranceBox_.addItem (TRANS ("Strict timing"), 1);
    toleranceBox_.addItem (TRANS ("Normal timing"), 2);
    toleranceBox_.addItem (TRANS ("Easy timing"), 3);
    toleranceBox_.setSelectedId (2, juce::dontSendNotification);
    toleranceBox_.setTitle (TRANS ("Timing tolerance"));
    toleranceBox_.setTooltip (TRANS ("How early or late a note may be played and still count. Strict: 120 ms early / 180 ms late. "
                                     "Normal: 250 / 300 ms. Easy: 400 / 600 ms."));
    toleranceBox_.onChange = [this] { if (intents_.toleranceChanged) intents_.toleranceChanged (toleranceBox_.getSelectedId() - 1); };
    otherPartToggle_.setButtonText (TRANS ("Other hand"));
    otherPartToggle_.setTooltip (TRANS ("In one-hand practice, the app plays the other hand for you"));
    otherPartToggle_.onClick = [this] { if (intents_.playOtherPartChanged) intents_.playOtherPartChanged (otherPartToggle_.getToggleState()); };
    backingToggle_.setButtonText (TRANS ("Other tracks"));
    backingToggle_.setTooltip (TRANS ("Play the band: drums, bass, strings and other non-piano tracks"));
    backingToggle_.onClick = [this] { if (intents_.playBackingChanged) intents_.playBackingChanged (backingToggle_.getToggleState()); };
    tracksButton_.setButtonText (TRANS ("Tracks and instruments..."));
    tracksButton_.setTooltip (TRANS ("Roles, sounds, volume, Mute and Solo of every track, in a separate window"));
    tracksButton_.onClick = [this] { if (intents_.showTracksWindow) intents_.showTracksWindow(); };
    tracksSummary_.setFont (juce::FontOptions (13.5f));
    tracksSummary_.setJustificationType (juce::Justification::topLeft);
    tracksSummary_.setMinimumHorizontalScale (1.0f);

    // The edge between the roll and the side panel
    splitter_.setTitle (TRANS ("Side panel edge"));
    splitter_.setTooltip (TRANS ("Drag to change the side panel width. Double-click for the standard width."));
    addAndMakeVisible (splitter_);
    collapseButton_.setTooltip (TRANS ("Hide or show the side panel"));
    collapseButton_.onClick = [this] { setSidebar (sidebarWidth_, ! sidebarCollapsed_, true); };
    addAndMakeVisible (collapseButton_);

    // Mode of the transport
    modeBadge_.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    modeBadge_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modeBadge_);

    // Roll view controls
    zoomOut_.setButtonText (juce::String::fromUTF8 ("\xe2\x88\x92"));
    zoomOut_.setTooltip (TRANS ("Zoom out (Ctrl+wheel over the roll)"));
    zoomOut_.onClick = [this] { roll_.zoomBy (1.0 / 1.4); tracksRoll_.setPixelsPerSecond (roll_.pixelsPerSecond()); };
    zoomIn_.setButtonText ("+");
    zoomIn_.setTooltip (TRANS ("Zoom in (Ctrl+wheel over the roll)"));
    zoomIn_.onClick = [this] { roll_.zoomBy (1.4); tracksRoll_.setPixelsPerSecond (roll_.pixelsPerSecond()); };
    zoomAll_.setButtonText (TRANS ("Whole song"));
    zoomAll_.setTooltip (TRANS ("Fit the whole song into the view"));
    zoomAll_.onClick = [this] {
        auto& shown = tab_ == 0 ? roll_ : tracksRoll_;
        shown.showWholeSong();
        (tab_ == 0 ? tracksRoll_ : roll_).setPixelsPerSecond (shown.pixelsPerSecond());
    };
    toPlayhead_.setButtonText (TRANS ("To the playhead"));
    toPlayhead_.setTooltip (TRANS ("Scroll back to the playhead and follow it again"));
    toPlayhead_.onClick = [this] {
        roll_.goToPlayhead();
        tracksRoll_.goToPlayhead();
        toPlayhead_.setToggleState (false, juce::dontSendNotification);
    };
    followToggle_.setButtonText (TRANS ("Follow the playhead"));
    followToggle_.setTooltip (TRANS ("The view scrolls with the playhead. It only moves the view, never the music."));
    followToggle_.setToggleState (true, juce::dontSendNotification);
    followToggle_.onClick = [this] {
        follow_ = followToggle_.getToggleState();
        if (follow_)
        {
            roll_.goToPlayhead();
            tracksRoll_.goToPlayhead();
        }
        if (intents_.followChanged)
            intents_.followChanged (follow_);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &zoomOut_, &zoomIn_, &zoomAll_, &toPlayhead_, &followToggle_ })
        addChildComponent (c);
    for (auto* roll : { &roll_, &tracksRoll_ })
        roll->onUserScrolledChanged = [this] (bool away) { toPlayhead_.setToggleState (away && follow_, juce::dontSendNotification); };

    toast_.setFont (juce::FontOptions (14.0f));
    toast_.setJustificationType (juce::Justification::centred);
    addChildComponent (toast_);
    volume_.setRange (0.0, 1.0);
    volume_.setValue (0.8, juce::dontSendNotification);
    volume_.setTitle (TRANS ("Volume"));
    volume_.onValueChange = [this] { if (intents_.volumeChanged) intents_.volumeChanged (static_cast<float> (volume_.getValue())); };
    soundBox_.setTitle (TRANS ("Sound"));
    soundBox_.setTooltip (TRANS ("The instrument that plays the song: built-in sound, a SoundFont or a VST3 instrument. "
                                 "Add more in Settings > Sounds."));
    soundBox_.onChange = [this] {
        const int id = soundBox_.getSelectedId();
        if (id == 900)
        {
            soundBox_.setSelectedId (sound_.selected, juce::dontSendNotification);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr && safe->intents_.openSettings)
                    safe->intents_.openSettings (SettingsPage::sounds);
            });
            return;
        }
        if (id > 0 && id != sound_.selected && intents_.soundChosen)
            intents_.soundChosen (id);
    };
    instrumentWindowButton_.setButtonText (TRANS ("Instrument..."));
    instrumentWindowButton_.setTooltip (TRANS ("Open the VST3 instrument's own window (sounds, presets)"));
    instrumentWindowButton_.onClick = [this] { if (intents_.showInstrumentWindow) intents_.showInstrumentWindow(); };
    ringNote_.setText (TRANS ("Sound may keep fading after a stop. \"Silence\" cuts it at once."), juce::dontSendNotification);
    ringNote_.setFont (juce::FontOptions (12.5f));
    ringNote_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (waitToggle_);
    // The side panel scrolls when the window is too low for all of it.
    for (auto* c : std::initializer_list<juce::Component*> {
             &modeCaption_, &modeBoth_, &modeRight_, &modeLeft_, &timingCaption_, &toleranceBox_,
             &accompanimentCaption_, &otherPartToggle_, &backingToggle_, &tracksButton_, &tracksSummary_, &volumeCaption_,
             &volume_, &soundCaption_, &soundBox_, &instrumentWindowButton_, &ringNote_ })
        sideContent_.addAndMakeVisible (c);
    sideView_.setViewedComponent (&sideContent_, false);
    sideView_.setScrollBarsShown (true, false);
    sideView_.setScrollBarThickness (10);
    addAndMakeVisible (sideView_);

    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

    refreshColours();
    setInputMonitor ({});
    setSound ({});
    setTransport ({});
    setSong (nullptr);
    setSize (1280, 820);
}

MainComponent::~MainComponent()
{
    tabs_.removeChangeListener (this);
    menuBar_.setModel (nullptr);
}

// --- view updates -----------------------------------------------------------

void MainComponent::setTransport (const TransportView& view)
{
    using State = TransportView::State;
    transport_ = view;
    const bool hasSong = view.state != State::noSong;
    playButton_.setEnabled (hasSong && view.audioAvailable);
    stopButton_.setEnabled (hasSong && view.audioAvailable);
    silenceButton_.setEnabled (view.audioAvailable);
    const bool running = view.state == State::playing || view.state == State::waiting;
    playButton_.setButtonText (running ? juce::String::fromUTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a  ") + TRANS ("Pause")
                                       : juce::String::fromUTF8 ("\xe2\x96\xb6  ") + TRANS ("Play"));
    playButton_.setToggleState (hasSong && ! running, juce::dontSendNotification); // accent when it starts playback
    positionLabel_.setText (positionText(), juce::dontSendNotification);
    ringNote_.setColour (juce::Label::textColourId, view.mayRingOut && ! running ? theme::warning : theme::mutedText);

    roll_.setPlayhead (view.positionSeconds, follow_ && running && tab_ == 0);
    tracksRoll_.setPlayhead (view.positionSeconds, follow_ && running && tab_ == 1);
    refreshStatus();
}

juce::String MainComponent::positionText() const
{
    if (! song_)
        return {};
    const auto& bars = song_->barStarts;
    juce::String text;
    if (! bars.empty())
    {
        const auto bar = std::max<std::ptrdiff_t> (
            1, std::upper_bound (bars.begin(), bars.end(), transport_.positionSeconds + 1.0e-6) - bars.begin());
        text << TRANS ("Bar") << " " << static_cast<int> (bar) << " / " << static_cast<int> (bars.size()) << "  ·  ";
    }
    text << formatTime (transport_.positionSeconds) << " / " << formatTime (song_->durationSeconds);
    return text;
}

void MainComponent::setTempo (int percent)
{
    tempo_ = percent;
    if (tempoBox_.indexOfItemId (percent) < 0)
        tempoBox_.addItem (juce::String (percent) + "%", percent);
    tempoBox_.setSelectedId (percent, juce::dontSendNotification);
}

void MainComponent::setLoop (bool enabled, double startSeconds, double endSeconds)
{
    loopEnabled_ = enabled && endSeconds > startSeconds;
    loopStart_ = startSeconds;
    loopEnd_ = endSeconds;
    roll_.setLoop (loopEnabled_, startSeconds, endSeconds);
    tracksRoll_.setLoop (loopEnabled_, startSeconds, endSeconds);
    loopButton_.setToggleState (loopEnabled_, juce::dontSendNotification);
    loopButton_.setButtonText (loopEnabled_ ? TRANS ("Loop") + " " + juce::String::fromUTF8 ("\xe2\x9c\x93") : TRANS ("Loop"));
    menuItemsChanged();
}

void MainComponent::toggleLoop()
{
    if (! song_)
        return;
    if (! loopEnabled_ && loopEnd_ <= loopStart_)
    {
        // No range yet: the current bar and the three after it.
        const auto& bars = song_->barStarts;
        double start = 0.0, end = std::min (song_->durationSeconds, 8.0);
        if (! bars.empty())
        {
            auto it = std::upper_bound (bars.begin(), bars.end(), transport_.positionSeconds + 1.0e-6);
            const auto index = static_cast<std::size_t> (std::max<std::ptrdiff_t> (0, (it - bars.begin()) - 1));
            start = bars[index];
            end = index + 4 < bars.size() ? bars[index + 4] : song_->durationSeconds;
        }
        loopStart_ = start;
        loopEnd_ = end;
    }
    setLoop (! loopEnabled_, loopStart_, loopEnd_);
    if (intents_.loopChanged)
        intents_.loopChanged (loopEnabled_, loopStart_, loopEnd_);
}

void MainComponent::setTracks (const std::vector<TrackRowView>& rows)
{
    tracks_ = rows;
    tracksSummary_.setText (TracksComponent::summary (tracks_), juce::dontSendNotification);
    int backingCount = 0;
    for (const auto& r : tracks_)
        backingCount += r.assignment == 3 ? 1 : 0;
    tabs_.setTabName (1, backingCount > 0 ? TRANS ("Other tracks") + " (" + juce::String (backingCount) + ")" : TRANS ("Other tracks"));
    resized();
}

void MainComponent::setLayoutChoices (int sidebarWidth, bool collapsed, bool follow)
{
    follow_ = follow;
    followToggle_.setToggleState (follow, juce::dontSendNotification);
    setSidebar (sidebarWidth, collapsed, false);
}

void MainComponent::showToast (const juce::String& text)
{
    toast_.setText (text, juce::dontSendNotification);
    toast_.setVisible (text.isNotEmpty());
    toast_.toFront (false);
    resized();
    startTimer (7000);
}

void MainComponent::timerCallback()
{
    stopTimer();
    toast_.setVisible (false);
}

int MainComponent::maxSidebarWidth() const
{
    return std::max (minSidebarWidth, getWidth() - minRollWidth);
}

void MainComponent::setSidebar (int width, bool collapsed, bool notify)
{
    sidebarWidth_ = std::clamp (width, minSidebarWidth, 1200);
    sidebarCollapsed_ = collapsed;
    collapseButton_.setButtonText (juce::String::fromUTF8 (collapsed ? "\xe2\x80\xb9" : "\xe2\x80\xba"));
    resized();
    menuItemsChanged();
    if (notify && intents_.sidebarChanged)
        intents_.sidebarChanged (sidebarWidth_, sidebarCollapsed_);
}

void MainComponent::refreshModeBadge()
{
    const bool waiting = practice_.practising;
    modeBadge_.setText (waiting ? TRANS ("Practice with waiting") : TRANS ("Free playback"), juce::dontSendNotification);
    modeBadge_.setColour (juce::Label::textColourId, waiting ? theme::onAccent : theme::text);
    modeBadge_.setColour (juce::Label::backgroundColourId, waiting ? theme::accent : theme::control);
    modeBadge_.setTooltip (waiting ? TRANS ("The song stops at your notes until you play them")
                                   : TRANS ("The song plays on; nothing is checked"));
}

void MainComponent::setRecentFiles (std::vector<std::string> utf8Paths)
{
    recent_ = std::move (utf8Paths);
    recentButtons_.clear();
    for (std::size_t i = 0; i < std::min<std::size_t> (recent_.size(), 4); ++i)
    {
        const juce::File file (toJuce (recent_[i]));
        auto* button = recentButtons_.add (new juce::TextButton (file.getFileNameWithoutExtension()));
        button->setTooltip (file.getFullPathName());
        button->onClick = [this, file] { if (intents_.openMidiFile) intents_.openMidiFile (file); };
        addChildComponent (button);
    }
    menuItemsChanged();
    setSong (song_);
}

void MainComponent::setSound (const SoundView& view)
{
    sound_ = view;
    refreshSoundBoxes();
    instrumentWindowButton_.setEnabled (view.canEdit);
    soundBox_.setTooltip (view.status.empty() ? TRANS ("The instrument that plays the song") : toJuce (view.status));
}

void MainComponent::fillSoundBox (juce::ComboBox& box, bool withDefault, bool withActions) const
{
    box.clear (juce::dontSendNotification);
    if (withDefault)
        box.addItem (TRANS ("Main sound"), 1000);
    box.addItem (TRANS ("Built-in sound"), SoundView::builtIn);
    if (! sound_.soundFontName.empty())
        box.addItem ("SoundFont: " + toJuce (sound_.soundFontName), SoundView::soundFont);
    if (! sound_.plugins.empty())
        box.addSectionHeading (TRANS ("VST3 instruments"));
    for (std::size_t i = 0; i < sound_.plugins.size(); ++i)
        box.addItem (toJuce (sound_.plugins[i]), SoundView::firstPlugin + static_cast<int> (i));
    if (withActions)
    {
        box.addSeparator();
        box.addItem (TRANS ("Add sounds..."), 900);
    }
}

void MainComponent::refreshSoundBoxes()
{
    fillSoundBox (soundBox_, false, true);
    soundBox_.setSelectedId (sound_.selected, juce::dontSendNotification);
}

void MainComponent::setPractice (const PracticeView& view)
{
    practice_ = view;
    waitToggle_.setToggleState (view.practising, juce::dontSendNotification);
    modeBoth_.setToggleState (view.mode == 0, juce::dontSendNotification);
    modeRight_.setToggleState (view.mode == 1, juce::dontSendNotification);
    modeLeft_.setToggleState (view.mode == 2, juce::dontSendNotification);
    keyboard_.setPracticePitches (view.practising ? view.required : std::vector<std::uint8_t> {}, view.wrong);
    toleranceBox_.setSelectedId (view.tolerance + 1, juce::dontSendNotification);
    otherPartToggle_.setToggleState (view.playOtherPart, juce::dontSendNotification);
    otherPartToggle_.setEnabled (view.mode != 0);
    backingToggle_.setToggleState (view.playBacking, juce::dontSendNotification);
    board_.setView (view);
    refreshModeBadge();
    refreshStatus();
}

void MainComponent::setMode (int mode)
{
    if (intents_.modeChanged && mode != practice_.mode)
        intents_.modeChanged (mode);
}

void MainComponent::setInputMonitor (const InputMonitorView& view)
{
    input_ = view;
    using Status = InputMonitorView::Status;
    juce::String text;
    switch (view.status)
    {
        case Status::connected:
        {
            juce::String device;
            for (const auto& d : view.devices)
                if (d.identifier == view.selectedIdentifier)
                    device = toJuce (d.name);
            text = (device.isEmpty() ? TRANS ("Piano") : device) + " " + TRANS ("connected");
            break;
        }
        case Status::noDevices:   text = TRANS ("No piano found"); break;
        case Status::notSelected: text = TRANS ("Choose your piano"); break;
        case Status::waiting:     text = TRANS ("Waiting for the piano"); break;
        case Status::lost:        text = TRANS ("Piano disconnected"); break;
        case Status::openFailed:  text = TRANS ("Piano busy in another program"); break;
    }
    connection_.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x8f  ") + text);
    refreshColours();
    roll_.setHeldInputPitches (view.heldPitches);
    keyboard_.setHeldPitches (view.heldPitches);
    refreshStatus();
}

void MainComponent::setLoading (const std::string& fileName)
{
    loadingName_ = toJuce (fileName);
    error_.clear();
    refreshStatus();
}

void MainComponent::setSongError (const std::string& message)
{
    loadingName_.clear();
    error_ = toJuce (message);
    refreshStatus();
}

void MainComponent::setSong (std::shared_ptr<const PianoRollView> song)
{
    const bool changed = song != song_;
    song_ = std::move (song);
    if (changed)
    {
        loadingName_.clear();
        if (song_)
            error_.clear();
    }
    roll_.setSong (song_.get());
    tracksRoll_.setSong (song_.get());

    const bool hasSong = song_ != nullptr;
    title_.setText (hasSong ? juce::File::createFileWithoutCheckingPath (toJuce (song_->title)).getFileNameWithoutExtension()
                            : TRANS ("No MIDI file open"),
                    juce::dontSendNotification);
    crumb_.setText ((hasSong ? TRANS ("Practice") + "  ·  " + toJuce (song_->summary) : TRANS ("Start")).toUpperCase(),
                    juce::dontSendNotification);
    juce::String notices;
    if (song_ && song_->needsPartCheck)
        notices = TRANS ("Hands were guessed from pitch: check the parts marked ? in Tracks and instruments.");
    if (song_)
        for (const auto& notice : song_->notices)
            notices << (notices.isEmpty() ? "" : "  ") << toJuce (notice);
    notices_.setText (notices, juce::dontSendNotification);

    if (song_)
        for (std::size_t i = 0; i < song_->parts.size(); ++i)
        {
            const bool piano = isPianoAssignment (song_->parts[i].assignment);
            roll_.setPartVisible (i, piano);
            tracksRoll_.setPartVisible (i, ! piano);
        }
    if (changed)
        setLoop (false, 0.0, 0.0);
    if (! song_)
        setTracks ({});
    menuItemsChanged();
    showTab (tab_);
}

void MainComponent::setAppearance (bool light, double scale)
{
    light_ = light;
    scale_ = scale;
    menuItemsChanged();
}

void MainComponent::lookAndFeelChanged()
{
    refreshColours();
    repaint();
}

void MainComponent::refreshColours()
{
    for (auto* label : { &crumb_, &positionLabel_, &tempoLabel_, &emptyHint_, &tracksEmpty_, &modeCaption_, &timingCaption_,
                         &accompanimentCaption_, &volumeCaption_, &soundCaption_ })
        label->setColour (juce::Label::textColourId, theme::mutedText);
    for (auto* label : { &appName_, &title_, &tracksSummary_ })
        label->setColour (juce::Label::textColourId, theme::text);
    notices_.setColour (juce::Label::textColourId, theme::warning);
    if (! transport_.mayRingOut)
        ringNote_.setColour (juce::Label::textColourId, theme::mutedText);
    const bool ok = input_.status == InputMonitorView::Status::connected;
    const bool bad = input_.status == InputMonitorView::Status::lost || input_.status == InputMonitorView::Status::openFailed
                     || input_.status == InputMonitorView::Status::noDevices;
    connection_.setColour (juce::TextButton::textColourOffId, ok ? theme::good : bad ? theme::danger : theme::warning);
    connection_.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    toast_.setColour (juce::Label::textColourId, theme::text);
    toast_.setColour (juce::Label::backgroundColourId, theme::control);
    toast_.setColour (juce::Label::outlineColourId, theme::good);
    refreshModeBadge();
    repaint();
}

// --- status strip -----------------------------------------------------------

void MainComponent::refreshStatus()
{
    using Tone = NextNotesBoard::Tone;
    using State = TransportView::State;
    using Status = InputMonitorView::Status;
    const auto mode = TRANS ("Mode") + ": " + modeName (practice_.mode);

    if (loadingName_.isNotEmpty())
        return board_.setMessage (TRANS ("Loading") + " " + loadingName_ + "...", {}, Tone::normal, false);
    if (error_.isNotEmpty())
        return board_.setMessage (TRANS ("Could not open the file"), error_, Tone::error, false);
    if (! song_)
        return board_.setMessage (TRANS ("Open a MIDI file to start"),
                                  TRANS ("File > Open MIDI file (Ctrl+O), or drag a .mid file onto the window."), Tone::normal,
                                  false);
    if (! transport_.audioAvailable)
        return board_.setMessage (TRANS ("No sound output"), toJuce (transport_.audioText) + "  " + TRANS ("Choose an output in Settings > MIDI and audio."),
                                  Tone::error, false);

    const bool noPiano = input_.status != Status::connected;
    const auto pianoHint = input_.status == Status::lost ? TRANS ("Piano disconnected: reconnect the USB cable, it will be picked up automatically.")
                         : input_.status == Status::openFailed ? TRANS ("Another program is using the piano. Close it, then choose the piano in Settings.")
                         : TRANS ("Connect the piano (USB TO HOST) to play along. You can still listen.");

    if (practice_.countInBeat > 0)
        return board_.setMessage (TRANS ("Count-in") + ": " + juce::String (practice_.countInBeat) + " / " + juce::String (practice_.countInBeats),
                                  TRANS ("Get ready: your notes come next."), Tone::waiting, practice_.practising);

    switch (transport_.state)
    {
        case State::noSong:
        case State::stopped:
            if (noPiano && practice_.practising)
                return board_.setMessage (TRANS ("No piano connected"), pianoHint, Tone::warning, false);
            return board_.setMessage (TRANS ("Ready. Press Play (Space)."),
                                      mode + "  ·  " + (practice_.practising ? TRANS ("the song waits for your notes")
                                                                             : TRANS ("listening, not waiting for notes")),
                                      Tone::normal, practice_.practising);
        case State::waiting:
            return board_.setMessage (TRANS ("Waiting for your notes") + (practice_.required.empty() ? juce::String()
                                                                                                    : ": " + noteList (practice_.required)),
                                      noPiano ? pianoHint : mode, noPiano ? Tone::warning : Tone::waiting, true);
        case State::playing:
            if (practice_.practising && practice_.accompanimentOnly)
                return board_.setMessage (TRANS ("Accompaniment only"), TRANS ("Get ready: your notes come next."),
                                          Tone::normal, true);
            if (practice_.practising)
                return board_.setMessage (TRANS ("Play along"), mode + "  ·  " + TRANS ("next notes:"), Tone::normal, true);
            return board_.setMessage (TRANS ("Playing"), mode + "  ·  " + TRANS ("tick \"Wait for my notes\" to practise"),
                                      Tone::normal, false);
        case State::paused:
            return board_.setMessage (TRANS ("Paused"),
                                      TRANS ("Sound may keep fading. \"Silence\" cuts it. Press Play to continue."),
                                      Tone::normal, practice_.practising);
        case State::completed:
            return board_.setMessage (TRANS ("Finished!"), TRANS ("Press Play to start again, or Stop to go back to the start."),
                                      Tone::good, false);
    }
}

void MainComponent::showTab (int index)
{
    tab_ = index;
    if (tabs_.getCurrentTabIndex() != index)
        tabs_.setCurrentTabIndex (index, false);
    const bool hasSong = song_ != nullptr;
    const bool mine = index == 0;
    tabs_.setVisible (hasSong);
    roll_.setVisible (hasSong && mine);
    tracksRoll_.setVisible (hasSong && ! mine);
    bool anyOther = false;
    if (song_)
        for (const auto& part : song_->parts)
            anyOther = anyOther || part.assignment == 3;
    tracksEmpty_.setVisible (hasSong && ! mine && ! anyOther);
    emptyOpenButton_.setVisible (! hasSong);
    emptyHint_.setVisible (! hasSong);
    for (auto* button : recentButtons_)
        button->setVisible (! hasSong);
    for (auto* c : std::initializer_list<juce::Component*> { &zoomOut_, &zoomIn_, &zoomAll_, &toPlayhead_, &followToggle_ })
        c->setVisible (hasSong);
    resized(); // hides the view controls that do not fit
}

// --- layout -----------------------------------------------------------------

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
    g.setColour (theme::header);
    g.fillRect (headerArea_);
    g.setColour (theme::panelEdge);
    g.drawHorizontalLine (headerArea_.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
    g.setColour (theme::header);
    g.fillRect (sidebarArea_);
    g.setColour (theme::panelEdge);
    g.drawVerticalLine (sidebarArea_.getX(), static_cast<float> (sidebarArea_.getY()), static_cast<float> (getHeight()));

    g.setColour (theme::panel);
    g.fillRoundedRectangle (rollPanel_.toFloat(), 10.0f);
    g.setColour (dragOver_ ? theme::accent : theme::panelEdge);
    g.drawRoundedRectangle (rollPanel_.toFloat().reduced (0.5f), 10.0f, dragOver_ ? 2.0f : 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    headerArea_ = area.removeFromTop (headerHeight);
    {
        auto header = headerArea_.reduced (20, 10);
        appName_.setBounds (header.removeFromLeft (130));
        settingsButton_.setBounds (header.removeFromRight (120));
        header.removeFromRight (10);
        // The menus keep their full width (about 340); the connection takes the rest.
        const int menuWidth = std::min (380, header.getWidth());
        const int connectionWidth = std::clamp (header.getWidth() - menuWidth - 10, 0, 300);
        connection_.setBounds (header.removeFromRight (connectionWidth));
        menuBar_.setBounds (header.removeFromLeft (menuWidth).withSizeKeepingCentre (menuWidth, 36));
    }

    // Side panel: as wide as chosen, but the roll keeps its minimum width.
    // Collapsed (or no room), its essentials move into a row above the roll.
    const bool roomForPanel = area.getWidth() >= minSidebarWidth + minRollWidth;
    const bool panel = roomForPanel && ! sidebarCollapsed_;
    const int width = std::clamp (sidebarWidth_, minSidebarWidth, std::max (minSidebarWidth, area.getWidth() - minRollWidth));
    sidebarArea_ = panel ? area.removeFromRight (width) : juce::Rectangle<int>();
    auto edge = area.removeFromRight (roomForPanel ? 14 : 0);
    splitter_.setVisible (panel);
    splitter_.setBounds (edge);
    collapseButton_.setVisible (roomForPanel);
    collapseButton_.setBounds (edge.getX() - 4, area.getY() + 12, 22, 34);
    collapseButton_.toFront (false);
    layoutSidebar (sidebarArea_);
    const bool narrow = ! panel;

    auto main = area.reduced (20, 14);
    main.removeFromRight (4);
    crumb_.setBounds (main.removeFromTop (18));
    {
        auto titleRow = main.removeFromTop (34);
        modeBadge_.setBounds (titleRow.removeFromRight (std::min (240, titleRow.getWidth() / 3)).reduced (0, 4));
        titleRow.removeFromRight (10);
        title_.setBounds (titleRow);
    }
    notices_.setVisible (notices_.getText().isNotEmpty());
    if (notices_.isVisible())
        notices_.setBounds (main.removeFromTop (20));
    main.removeFromTop (6);

    // Transport: buttons and "Wait for my notes", then position and tempo;
    // on a narrow window the second half goes to its own row.
    auto bar = main.removeFromTop (38);
    const bool twoRows = bar.getWidth() < 900;
    playButton_.setBounds (bar.removeFromLeft (112));
    bar.removeFromLeft (8);
    stopButton_.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (8);
    silenceButton_.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    loopButton_.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (14);
    waitToggle_.setBounds (bar.removeFromLeft (std::min (210, bar.getWidth())));
    if (twoRows)
    {
        main.removeFromTop (6);
        bar = main.removeFromTop (32);
    }
    else
        bar.removeFromLeft (10);
    tempoBox_.setBounds (bar.removeFromRight (96));
    tempoLabel_.setBounds (bar.removeFromRight (64));
    positionLabel_.setBounds (bar);
    main.removeFromTop (gap);

    // A low window (or a large interface size) keeps the roll usable:
    // the status strip gets compact, then the keyboard goes.
    int keyboardHeight = 92, boardHeight = 80;
    const int spare = main.getHeight() - 2 * gap - 180 - (narrow ? 42 : 0);
    if (spare < keyboardHeight + boardHeight)
    {
        boardHeight = 62;
        keyboardHeight = spare - boardHeight >= 56 ? std::min (92, spare - boardHeight) : 0;
    }
    keyboard_.setVisible (keyboardHeight > 0);
    if (keyboardHeight > 0)
    {
        keyboard_.setBounds (main.removeFromBottom (keyboardHeight));
        main.removeFromBottom (gap);
    }
    board_.setBounds (main.removeFromBottom (boardHeight));
    main.removeFromBottom (gap);

    // Hands, accompaniment and the tracks button live in the side panel, or
    // in a row above the roll when the panel is hidden.
    for (auto* c : std::initializer_list<juce::Component*> { &modeBoth_, &modeRight_, &modeLeft_, &otherPartToggle_, &backingToggle_,
                                                              &tracksButton_ })
    {
        auto& parent = narrow ? static_cast<juce::Component&> (*this) : sideContent_;
        if (c->getParentComponent() != &parent)
            parent.addAndMakeVisible (c);
        c->setVisible (c != &tracksButton_ || song_ != nullptr);
    }
    if (narrow)
    {
        // No side panel: hands, accompaniment and tracks in a row above the roll.
        auto row = main.removeFromTop (32);
        for (auto* b : { &modeBoth_, &modeRight_, &modeLeft_ })
        {
            b->setBounds (row.removeFromLeft (76));
            row.removeFromLeft (4);
        }
        row.removeFromLeft (10);
        otherPartToggle_.setBounds (row.removeFromLeft (std::min (150, row.getWidth())));
        backingToggle_.setBounds (row.removeFromLeft (std::min (170, row.getWidth())));
        tracksButton_.setVisible (song_ != nullptr && row.getWidth() >= 120);
        tracksButton_.setBounds (row.removeFromRight (std::min (230, row.getWidth())));
        main.removeFromTop (gap);
    }

    rollPanel_ = main;
    auto inner = rollPanel_.reduced (10);
    if (song_)
    {
        auto tabsRow = inner.removeFromTop (32);
        // View controls on the right of the tabs. With little room the less
        // needed ones step aside (they stay in the View menu); none is squeezed.
        const int tabsWidth = std::min (300, tabsRow.getWidth() / 2);
        auto controls = tabsRow.removeFromRight (tabsRow.getWidth() - tabsWidth - 8);
        const int room = controls.getWidth() - 2 * 34 - 10;
        const bool showPlayhead = room >= 156, showAll = room >= 156 + 146, showFollow = room >= 156 + 146 + 206;
        toPlayhead_.setVisible (showPlayhead);
        zoomAll_.setVisible (showAll);
        followToggle_.setVisible (showFollow);
        if (showFollow)
            followToggle_.setBounds (controls.removeFromRight (200));
        controls.removeFromRight (6);
        if (showPlayhead)
            toPlayhead_.setBounds (controls.removeFromRight (150));
        controls.removeFromRight (6);
        if (showAll)
            zoomAll_.setBounds (controls.removeFromRight (140));
        controls.removeFromRight (6);
        zoomIn_.setBounds (controls.removeFromRight (34));
        controls.removeFromRight (4);
        zoomOut_.setBounds (controls.removeFromRight (34));
        tabs_.setBounds (tabsRow.withWidth (tabsWidth));
        inner.removeFromTop (6);
    }
    roll_.setBounds (inner);
    tracksRoll_.setBounds (inner);
    tracksEmpty_.setBounds (inner.withSizeKeepingCentre (inner.getWidth(), 40));
    toast_.setBounds (rollPanel_.withSizeKeepingCentre (std::min (rollPanel_.getWidth() - 40, 640), 40).withY (rollPanel_.getBottom() - 60));

    auto centre = inner.withSizeKeepingCentre (std::min (inner.getWidth(), 360), 220);
    emptyOpenButton_.setBounds (centre.removeFromTop (54).reduced (40, 0));
    emptyHint_.setBounds (centre.removeFromTop (34));
    centre.removeFromTop (8);
    for (auto* button : recentButtons_)
    {
        button->setBounds (centre.removeFromTop (30).reduced (60, 0));
        centre.removeFromTop (4);
    }
    repaint();
}

void MainComponent::layoutSidebar (juce::Rectangle<int> area)
{
    const bool visible = ! area.isEmpty();
    sideView_.setVisible (visible);
    if (! visible)
        return;
    sideView_.setBounds (area);
    constexpr int neededHeight = 520; // everything, with the summary a few lines high
    const bool scrolls = area.getHeight() < neededHeight;
    sideContent_.setSize (area.getWidth() - (scrolls ? sideView_.getScrollBarThickness() : 0), std::max (area.getHeight(), neededHeight));

    auto side = sideContent_.getLocalBounds().reduced (18, 16);
    modeCaption_.setBounds (side.removeFromTop (20));
    side.removeFromTop (4);
    auto modes = side.removeFromTop (38);
    const int third = (modes.getWidth() - 8) / 3;
    modeBoth_.setBounds (modes.removeFromLeft (third));
    modes.removeFromLeft (4);
    modeRight_.setBounds (modes.removeFromLeft (third));
    modes.removeFromLeft (4);
    modeLeft_.setBounds (modes);
    side.removeFromTop (12);
    timingCaption_.setBounds (side.removeFromTop (20));
    toleranceBox_.setBounds (side.removeFromTop (30));
    side.removeFromTop (14);

    accompanimentCaption_.setBounds (side.removeFromTop (20));
    otherPartToggle_.setBounds (side.removeFromTop (28));
    backingToggle_.setBounds (side.removeFromTop (28));
    side.removeFromTop (10);
    tracksButton_.setBounds (side.removeFromTop (34));
    side.removeFromTop (6);
    // Bottom block, then the summary takes what is left (scrolled out of sight is fine: the window has it all).
    auto bottom = side.removeFromBottom (std::min (side.getHeight(), 190));
    tracksSummary_.setVisible (song_ != nullptr && side.getHeight() > 30);
    tracksSummary_.setBounds (side);
    volumeCaption_.setBounds (bottom.removeFromTop (20));
    volume_.setBounds (bottom.removeFromTop (28));
    bottom.removeFromTop (8);
    soundCaption_.setBounds (bottom.removeFromTop (20));
    soundBox_.setBounds (bottom.removeFromTop (30));
    bottom.removeFromTop (6);
    instrumentWindowButton_.setBounds (bottom.removeFromTop (28).removeFromLeft (150));
    bottom.removeFromTop (8);
    ringNote_.setBounds (bottom);
}

// --- keyboard, menus, drag and drop ------------------------------------------

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey && intents_.playPause && playButton_.isEnabled())
    {
        intents_.playPause();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && intents_.silence)
    {
        intents_.silence();
        return true;
    }
    const auto ctrl = key.getModifiers().isCommandDown();
    if (! ctrl)
        return false;
    const auto code = key.getKeyCode();
    if (code == 'O' && intents_.chooseMidiFile)
        intents_.chooseMidiFile();
    else if (code == 'P')
        waitToggle_.triggerClick();
    else if (code >= '1' && code <= '3')
        setMode (code - '1');
    else if (code == juce::KeyPress::tabKey)
        showTab (1 - tab_);
    else if (code == 'T')
        toleranceBox_.setSelectedId (toleranceBox_.getSelectedId() % 3 + 1, juce::sendNotificationSync);
    else if (code == 'B' && intents_.showLibrary)
        intents_.showLibrary();
    else if (code == ',' && intents_.openSettings)
        intents_.openSettings (SettingsPage::midiAndAudio);
    else if (code == 'L')
        toggleLoop();
    else if (code == 'W' && intents_.closeSong)
        intents_.closeSong();
    else
        return false;
    return true;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { TRANS ("File"), TRANS ("View"), TRANS ("Practice"), TRANS ("Help") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;
    const auto call = [] (const std::function<void()>& f) { return [f] { if (f) f(); }; };
    const bool hasSong = song_ != nullptr;
    switch (index)
    {
        case 0: // File
        {
            menu.addItem (juce::PopupMenu::Item (TRANS ("Open MIDI file...")).setAction (call (intents_.chooseMidiFile))| shortcut ("Ctrl+O"));
            juce::PopupMenu recent;
            for (const auto& path : recent_)
            {
                const juce::File file (toJuce (path));
                recent.addItem (file.getFileName(), [this, file] { if (intents_.openMidiFile) intents_.openMidiFile (file); });
            }
            menu.addSubMenu (TRANS ("Recent files"), recent, ! recent_.empty());
            menu.addItem (juce::PopupMenu::Item (TRANS ("Song library...")).setAction (call (intents_.showLibrary))| shortcut ("Ctrl+B"));
            menu.addItem (TRANS ("Open song folder"), hasSong, false, call (intents_.openSongFolder));
            menu.addItem (TRANS ("Open library folder"), call (intents_.openLibraryFolder));
            menu.addItem (juce::PopupMenu::Item (TRANS ("Close file")).setEnabled (hasSong).setAction (call (intents_.closeSong))| shortcut ("Ctrl+W"));
            menu.addSeparator();
            menu.addItem (juce::PopupMenu::Item (TRANS ("Settings...")).setAction ([this] {
                if (intents_.openSettings) intents_.openSettings (SettingsPage::midiAndAudio); })| shortcut ("Ctrl+,"));
            menu.addSeparator();
            menu.addItem (TRANS ("Exit"), call (intents_.quit));
            break;
        }
        case 1: // View
        {
            menu.addItem (TRANS ("My parts"), hasSong, tab_ == 0, [this] { showTab (0); });
            menu.addItem (TRANS ("Other tracks"), hasSong, tab_ == 1, [this] { showTab (1); });
            menu.addItem (TRANS ("Tracks and instruments..."), hasSong, false, call (intents_.showTracksWindow));
            menu.addItem (TRANS ("Side panel"), true, ! sidebarCollapsed_, [this] { setSidebar (sidebarWidth_, ! sidebarCollapsed_, true); });
            menu.addItem (TRANS ("Standard side panel width"), true, false, [this] { setSidebar (defaultSidebarWidth, false, true); });
            menu.addSeparator();
            menu.addItem (TRANS ("Zoom in"), hasSong, false, [this] { zoomIn_.triggerClick(); });
            menu.addItem (TRANS ("Zoom out"), hasSong, false, [this] { zoomOut_.triggerClick(); });
            menu.addItem (TRANS ("Whole song"), hasSong, false, [this] { zoomAll_.triggerClick(); });
            menu.addItem (TRANS ("To the playhead"), hasSong, false, [this] { toPlayhead_.triggerClick(); });
            menu.addItem (TRANS ("Follow the playhead"), true, follow_, [this] { followToggle_.triggerClick(); });
            menu.addSeparator();
            juce::PopupMenu themes;
            themes.addItem (TRANS ("Dark"), true, ! light_, [this] { if (intents_.themeChanged) intents_.themeChanged (false); });
            themes.addItem (TRANS ("Light"), true, light_, [this] { if (intents_.themeChanged) intents_.themeChanged (true); });
            menu.addSubMenu (TRANS ("Theme"), themes);
            juce::PopupMenu scales;
            for (const double s : { 0.85, 1.0, 1.15, 1.25, 1.5, 1.75, 2.0 })
                scales.addItem (juce::String (juce::roundToInt (s * 100)) + "%", true, std::abs (s - scale_) < 0.01,
                                [this, s] { if (intents_.uiScaleChanged) intents_.uiScaleChanged (s); });
            menu.addSubMenu (TRANS ("Interface size"), scales);
            menu.addItem (TRANS ("Appearance settings..."), [this] { if (intents_.openSettings) intents_.openSettings (SettingsPage::appearance); });
            break;
        }
        case 2: // Practice
        {
            const bool running = transport_.state == TransportView::State::playing || transport_.state == TransportView::State::waiting;
            menu.addItem (juce::PopupMenu::Item (running ? TRANS ("Pause") : TRANS ("Play")).setEnabled (hasSong).setAction (call (intents_.playPause))| shortcut (TRANS ("Space")));
            menu.addItem (juce::PopupMenu::Item (TRANS ("Stop")).setEnabled (hasSong).setAction (call (intents_.stop)));
            menu.addItem (juce::PopupMenu::Item (TRANS ("Silence")).setAction (call (intents_.silence))| shortcut ("Esc"));
            menu.addSeparator();
            menu.addItem (juce::PopupMenu::Item (TRANS ("Wait for my notes")).setTicked (practice_.practising).setAction ([this] { waitToggle_.triggerClick(); })| shortcut ("Ctrl+P"));
            juce::PopupMenu modes;
            for (int m = 0; m < 3; ++m)
                modes.addItem (juce::PopupMenu::Item (modeName (m)).setTicked (practice_.mode == m).setAction ([this, m] { setMode (m); })| shortcut ("Ctrl+" + juce::String (m + 1)));
            menu.addSubMenu (TRANS ("Hands"), modes);
            juce::PopupMenu timing;
            for (int t = 0; t < 3; ++t)
                timing.addItem (toleranceBox_.getItemText (t), true, practice_.tolerance == t, [this, t] { toleranceBox_.setSelectedId (t + 1, juce::sendNotificationSync); });
            menu.addSubMenu (TRANS ("Timing"), timing);
            juce::PopupMenu tempo;
            for (const auto percent : tempoChoices)
                tempo.addItem (juce::String (percent) + "%", true, tempo_ == percent, [this, percent] { tempoBox_.setSelectedId (percent, juce::sendNotificationSync); });
            menu.addSubMenu (TRANS ("Tempo"), tempo);
            menu.addSeparator();
            menu.addItem (juce::PopupMenu::Item (TRANS ("Loop")).setEnabled (hasSong).setTicked (loopEnabled_).setAction ([this] { toggleLoop(); }) | shortcut ("Ctrl+L"));
            menu.addItem (TRANS ("Clear loop"), hasSong && loopEnd_ > loopStart_, false, [this] {
                setLoop (false, 0.0, 0.0);
                if (intents_.loopChanged) intents_.loopChanged (false, 0.0, 0.0); });
            menu.addItem (TRANS ("Count-in before playing (one bar)"), true, practice_.countIn, [this] {
                if (intents_.countInChanged) intents_.countInChanged (! practice_.countIn); });
            menu.addSeparator();
            menu.addItem (TRANS ("Play other hand"), practice_.mode != 0, practice_.playOtherPart, [this] { otherPartToggle_.triggerClick(); });
            menu.addItem (TRANS ("Play other tracks"), true, practice_.playBacking, [this] { backingToggle_.triggerClick(); });
            menu.addSeparator();
            menu.addItem (TRANS ("Practice and timing settings..."), [this] { if (intents_.openSettings) intents_.openSettings (SettingsPage::practice); });
            break;
        }
        case 3: // Help
        {
            menu.addItem (TRANS ("How to practise"), call (intents_.showHelp));
            menu.addItem (TRANS ("Diagnostics..."), [this] { if (intents_.openSettings) intents_.openSettings (SettingsPage::diagnostics); });
            menu.addItem (TRANS ("Copy diagnostic report"), call (intents_.copyDiagnostics));
            menu.addItem (TRANS ("Save diagnostic report..."), call (intents_.saveDiagnostics));
            menu.addItem (TRANS ("Open log folder"), call (intents_.openLogFolder));
            menu.addSeparator();
            menu.addItem (TRANS ("Check for updates..."), call (intents_.checkForUpdates));
            menu.addItem (TRANS ("About PianoAssist"), call (intents_.showAbout));
            break;
        }
        default:
            break;
    }
    return menu;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isMidiFile (files[0]);
}

void MainComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver_ = true;
    repaint();
}

void MainComponent::fileDragExit (const juce::StringArray&)
{
    dragOver_ = false;
    repaint();
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver_ = false;
    repaint();
    if (files.size() == 1 && intents_.openMidiFile)
        intents_.openMidiFile (juce::File (files[0]));
}

} // namespace pianoassist::ui
