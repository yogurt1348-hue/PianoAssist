#include "ui/MainComponent.h"

#include "ui/AppLookAndFeel.h"
#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int headerHeight = 58;
constexpr int sidebarWidth = 300;
constexpr int gap = 10;
constexpr int tempoChoices[] = { 25, 40, 50, 60, 70, 75, 80, 90, 100, 110, 120, 150, 200 };

bool isPianoAssignment (int assignment)
{
    return assignment != 3; // 0 right, 1 left, 2 split; 3 other track
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

void MainComponent::TrackList::resized()
{
    auto area = getLocalBounds();
    bool pianoDone = false, otherDone = false;
    for (auto& row : rows)
    {
        const bool piano = dynamic_cast<juce::Label*> (row.head.get()) != nullptr;
        if (piano && ! pianoDone)
        {
            pianoHeader.setBounds (area.removeFromTop (22));
            pianoDone = true;
        }
        if (! piano && ! otherDone)
        {
            area.removeFromTop (6);
            otherHeader.setBounds (area.removeFromTop (22));
            otherDone = true;
        }
        row.head->setBounds (area.removeFromTop (24));
        auto boxes = area.removeFromTop (26).withTrimmedLeft (piano ? 0 : 26);
        if (row.sound != nullptr)
        {
            row.role->setBounds (boxes.removeFromLeft (boxes.getWidth() / 2 - 2));
            boxes.removeFromLeft (4);
            row.sound->setBounds (boxes);
        }
        else
            row.role->setBounds (boxes);
        auto line = area.removeFromTop (20).withTrimmedLeft (piano ? 0 : 26);
        if (row.volume != nullptr)
            row.volume->setBounds (line.removeFromRight (std::min (110, line.getWidth() / 2)));
        row.detail->setBounds (line);
        area.removeFromTop (6);
    }
    pianoHeader.setVisible (pianoDone);
    otherHeader.setVisible (otherDone);
    juce::ignoreUnused (owner_);
}

int MainComponent::TrackList::preferredHeight() const
{
    int height = 0;
    bool piano = false, other = false;
    for (const auto& row : rows)
    {
        const bool isPiano = dynamic_cast<juce::Label*> (row.head.get()) != nullptr;
        if (isPiano && ! piano) { height += 22; piano = true; }
        if (! isPiano && ! other) { height += 28; other = true; }
        height += 24 + 26 + 20 + 6;
    }
    return height;
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
    tracksButton_.onClick = [this] {
        tracksOpen_ = ! tracksOpen_;
        resized();
    };
    tracksView_.setViewedComponent (&trackList_, false);
    tracksView_.setScrollBarsShown (true, false);
    trackList_.pianoHeader.setText (TRANS ("Piano parts: who plays what"), juce::dontSendNotification);
    trackList_.otherHeader.setText (TRANS ("Other tracks: on/off and sound"), juce::dontSendNotification);
    for (auto* label : { &trackList_.pianoHeader, &trackList_.otherHeader })
    {
        label->setFont (juce::FontOptions (12.5f, juce::Font::bold));
        trackList_.addChildComponent (label);
    }
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
    for (auto* c : std::initializer_list<juce::Component*> {
             &modeCaption_, &modeBoth_, &modeRight_, &modeLeft_, &waitToggle_, &timingCaption_, &toleranceBox_,
             &accompanimentCaption_, &otherPartToggle_, &backingToggle_, &tracksButton_, &tracksView_, &volumeCaption_,
             &volume_, &soundCaption_, &soundBox_, &instrumentWindowButton_, &ringNote_ })
        addAndMakeVisible (c);

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

    roll_.setPlayhead (view.positionSeconds, running && tab_ == 0);
    tracksRoll_.setPlayhead (view.positionSeconds, running && tab_ == 1);
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

void MainComponent::setTrackMix (std::vector<bool> muted, std::vector<double> volumes)
{
    muted_ = std::move (muted);
    volumes_ = std::move (volumes);
    for (auto& row : trackList_.rows)
    {
        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (row.head.get()))
            toggle->setToggleState (row.part >= muted_.size() || ! muted_[row.part], juce::dontSendNotification);
        if (row.volume != nullptr)
            row.volume->setValue (row.part < volumes_.size() ? volumes_[row.part] : 1.0, juce::dontSendNotification);
    }
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
    for (auto& row : trackList_.rows)
        if (row.sound != nullptr)
        {
            fillSoundBox (*row.sound, true, false);
            const auto chosen = row.part < partSounds_.size() ? partSounds_[row.part] : 0;
            row.sound->setSelectedId (chosen == 0 ? 1000 : chosen, juce::dontSendNotification);
        }
}

void MainComponent::setPartSounds (std::vector<int> soundIds)
{
    partSounds_ = std::move (soundIds);
    refreshSoundBoxes();
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
        notices = TRANS ("Hands were guessed from pitch: check the parts marked ? in the side panel.");
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
    if (! song_ || muted_.size() != song_->parts.size())
        muted_.assign (song_ ? song_->parts.size() : 0, false);
    if (! song_ || volumes_.size() != song_->parts.size())
        volumes_.assign (song_ ? song_->parts.size() : 0, 1.0);
    if (changed)
        setLoop (false, 0.0, 0.0);
    if (changed && ! song_)
        partSounds_.clear();
    rebuildPartList();
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
    for (auto* label : { &appName_, &title_, &trackList_.pianoHeader, &trackList_.otherHeader })
        label->setColour (juce::Label::textColourId, theme::text);
    notices_.setColour (juce::Label::textColourId, theme::warning);
    if (! transport_.mayRingOut)
        ringNote_.setColour (juce::Label::textColourId, theme::mutedText);
    const bool ok = input_.status == InputMonitorView::Status::connected;
    const bool bad = input_.status == InputMonitorView::Status::lost || input_.status == InputMonitorView::Status::openFailed
                     || input_.status == InputMonitorView::Status::noDevices;
    connection_.setColour (juce::TextButton::textColourOffId, ok ? theme::good : bad ? theme::danger : theme::warning);
    connection_.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    for (auto& row : trackList_.rows)
        if (row.detail != nullptr)
            row.detail->setColour (juce::Label::textColourId, theme::mutedText);
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

// --- tracks list ------------------------------------------------------------

juce::ComboBox* MainComponent::makeRoleBox (std::size_t part, int assignment)
{
    auto* role = new juce::ComboBox();
    role->addItem (TRANS ("Right hand"), 1);
    role->addItem (TRANS ("Left hand"), 2);
    role->addItem (TRANS ("Both hands (split at C4)"), 3);
    role->addItem (TRANS ("Other track (app plays it)"), 4);
    role->setSelectedId (assignment + 1, juce::dontSendNotification);
    role->setTitle (TRANS ("Who plays this part"));
    role->setTooltip (TRANS ("Who plays this part. Change it if the guess is wrong."));
    role->onChange = [this, part, role] {
        // Deferred: the change rebuilds this list, which deletes this box.
        const int chosen = role->getSelectedId() - 1;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), part, chosen] {
            if (safe != nullptr && safe->intents_.partAssignmentChanged)
                safe->intents_.partAssignmentChanged (part, chosen);
        });
    };
    return role;
}

juce::ComboBox* MainComponent::makeSoundBox (std::size_t part)
{
    auto* box = new juce::ComboBox();
    box->setTitle (TRANS ("Sound of this track"));
    box->setTooltip (TRANS ("The instrument for this track. \"Main sound\" uses the sound chosen below."));
    box->onChange = [this, part, box] {
        const int id = box->getSelectedId();
        const int chosen = id == 1000 ? 0 : id;
        if (part >= partSounds_.size())
            partSounds_.resize (part + 1, 0);
        if (partSounds_[part] == chosen)
            return;
        partSounds_[part] = chosen;
        if (intents_.partSoundChanged)
            intents_.partSoundChanged (part, chosen);
    };
    return box;
}

void MainComponent::rebuildPartList()
{
    for (auto& row : trackList_.rows)
        for (auto* c : std::initializer_list<juce::Component*> { row.head.get(), row.role.get(), row.sound.get(), row.volume.get(), row.detail.get() })
            if (c != nullptr)
                trackList_.removeChildComponent (c);
    trackList_.rows.clear();
    int backingCount = 0;
    if (song_)
    {
        std::vector<PartRow> piano, other;
        for (std::size_t i = 0; i < song_->parts.size(); ++i)
        {
            const auto& part = song_->parts[i];
            PartRow row;
            row.part = i;
            row.role.reset (makeRoleBox (i, part.assignment));
            row.detail = std::make_unique<juce::Label>();
            row.detail->setFont (juce::FontOptions (12.0f));
            row.detail->setColour (juce::Label::textColourId, theme::mutedText);
            row.detail->setText (toJuce (part.detail), juce::dontSendNotification);
            row.volume = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            row.volume->setRange (0.0, 1.0);
            row.volume->setValue (i < volumes_.size() ? volumes_[i] : 1.0, juce::dontSendNotification);
            row.volume->setTitle (TRANS ("Track volume"));
            row.volume->setTooltip (TRANS ("Volume of this track"));
            {
                auto* slider = row.volume.get();
                slider->onDragEnd = [this, i, slider] {
                    if (i >= volumes_.size())
                        volumes_.resize (i + 1, 1.0);
                    volumes_[i] = slider->getValue();
                    if (intents_.partVolumeChanged)
                        intents_.partVolumeChanged (i, slider->getValue());
                };
            }
            if (isPianoAssignment (part.assignment))
            {
                auto label = std::make_unique<juce::Label>();
                label->setText (toJuce ((part.guessedFromPitch ? "? " : "") + part.label), juce::dontSendNotification);
                label->setColour (juce::Label::textColourId, roleColour (part.role));
                label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
                if (part.guessedFromPitch)
                    label->setTooltip (TRANS ("Guessed from pitch: please check"));
                row.head = std::move (label);
                piano.push_back (std::move (row));
            }
            else
            {
                ++backingCount;
                auto play = std::make_unique<juce::ToggleButton> (toJuce (part.instrument + " · " + part.label));
                play->setToggleState (i >= muted_.size() || ! muted_[i], juce::dontSendNotification);
                play->setTooltip (TRANS ("Play this track. Untick to mute it."));
                auto* raw = play.get();
                play->onClick = [this, i, raw] {
                    if (i < muted_.size())
                        muted_[i] = ! raw->getToggleState();
                    if (intents_.partMuteChanged)
                        intents_.partMuteChanged (i, ! raw->getToggleState());
                };
                row.head = std::move (play);
                row.sound.reset (makeSoundBox (i));
                other.push_back (std::move (row));
            }
        }
        for (auto* rows : { &piano, &other })
            for (auto& row : *rows)
                trackList_.rows.push_back (std::move (row));
    }
    for (auto& row : trackList_.rows)
        for (auto* c : std::initializer_list<juce::Component*> { row.head.get(), row.role.get(), row.sound.get(), row.volume.get(), row.detail.get() })
            if (c != nullptr)
                trackList_.addAndMakeVisible (c);
    tabs_.setTabName (1, backingCount > 0 ? TRANS ("Other tracks") + " (" + juce::String (backingCount) + ")" : TRANS ("Other tracks"));
    tracksButton_.setButtonText (TRANS ("Tracks and instruments") + "  " + juce::String::fromUTF8 (tracksOpen_ ? "\xe2\x96\xb4" : "\xe2\x96\xbe"));
    refreshSoundBoxes();
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
    for (const auto& row : trackList_.rows)
        anyOther = anyOther || row.sound != nullptr;
    tracksEmpty_.setVisible (hasSong && ! mine && ! anyOther);
    emptyOpenButton_.setVisible (! hasSong);
    emptyHint_.setVisible (! hasSong);
    for (auto* button : recentButtons_)
        button->setVisible (! hasSong);
    resized();
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
        menuBar_.setBounds (header.removeFromLeft (std::min (380, header.getWidth() / 2)).withSizeKeepingCentre (std::min (380, header.getWidth() / 2), 36));
        settingsButton_.setBounds (header.removeFromRight (120));
        header.removeFromRight (10);
        connection_.setBounds (header.removeFromRight (std::min (300, header.getWidth())));
    }

    const bool narrow = area.getWidth() < 980;
    sidebarArea_ = narrow ? juce::Rectangle<int>() : area.removeFromRight (sidebarWidth);
    layoutSidebar (sidebarArea_);

    auto main = area.reduced (20, 14);
    crumb_.setBounds (main.removeFromTop (18));
    title_.setBounds (main.removeFromTop (34));
    notices_.setVisible (notices_.getText().isNotEmpty());
    if (notices_.isVisible())
        notices_.setBounds (main.removeFromTop (20));
    main.removeFromTop (6);

    auto bar = main.removeFromTop (38);
    playButton_.setBounds (bar.removeFromLeft (118));
    bar.removeFromLeft (8);
    stopButton_.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (8);
    silenceButton_.setBounds (bar.removeFromLeft (104));
    bar.removeFromLeft (8);
    loopButton_.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (14);
    tempoBox_.setBounds (bar.removeFromRight (96));
    tempoLabel_.setBounds (bar.removeFromRight (64));
    positionLabel_.setBounds (bar);
    main.removeFromTop (gap);

    keyboard_.setBounds (main.removeFromBottom (92));
    main.removeFromBottom (gap);
    board_.setBounds (main.removeFromBottom (80));
    main.removeFromBottom (gap);

    if (narrow)
    {
        // No room for the side panel: keep the essentials in a row above the roll.
        auto row = main.removeFromTop (30);
        for (auto* b : { &modeBoth_, &modeRight_, &modeLeft_ })
        {
            b->setBounds (row.removeFromLeft (70));
            row.removeFromLeft (4);
        }
        row.removeFromLeft (8);
        waitToggle_.setBounds (row.removeFromLeft (190));
        otherPartToggle_.setBounds (row.removeFromLeft (120));
        backingToggle_.setBounds (row.removeFromLeft (130));
        main.removeFromTop (gap);
    }

    rollPanel_ = main;
    auto inner = rollPanel_.reduced (10);
    if (song_)
    {
        tabs_.setBounds (inner.removeFromTop (30).withWidth (std::min (inner.getWidth(), 340)));
        inner.removeFromTop (6);
    }
    roll_.setBounds (inner);
    tracksRoll_.setBounds (inner);
    tracksEmpty_.setBounds (inner.withSizeKeepingCentre (inner.getWidth(), 40));

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
    for (auto* c : std::initializer_list<juce::Component*> { &modeCaption_, &timingCaption_, &toleranceBox_, &accompanimentCaption_,
                                                              &tracksButton_, &tracksView_, &volumeCaption_, &volume_,
                                                              &soundCaption_, &soundBox_, &instrumentWindowButton_, &ringNote_ })
        c->setVisible (visible);
    if (! visible)
        return;

    auto side = area.reduced (18, 16);
    modeCaption_.setBounds (side.removeFromTop (20));
    side.removeFromTop (4);
    auto modes = side.removeFromTop (38);
    const int third = (modes.getWidth() - 8) / 3;
    modeBoth_.setBounds (modes.removeFromLeft (third));
    modes.removeFromLeft (4);
    modeRight_.setBounds (modes.removeFromLeft (third));
    modes.removeFromLeft (4);
    modeLeft_.setBounds (modes);
    side.removeFromTop (6);
    waitToggle_.setBounds (side.removeFromTop (28));
    side.removeFromTop (10);
    timingCaption_.setBounds (side.removeFromTop (20));
    toleranceBox_.setBounds (side.removeFromTop (30));
    side.removeFromTop (14);

    // Bottom block first so the tracks list takes what is left.
    auto bottom = side.removeFromBottom (190);
    volumeCaption_.setBounds (bottom.removeFromTop (20));
    volume_.setBounds (bottom.removeFromTop (28));
    bottom.removeFromTop (8);
    soundCaption_.setBounds (bottom.removeFromTop (20));
    soundBox_.setBounds (bottom.removeFromTop (30));
    bottom.removeFromTop (6);
    instrumentWindowButton_.setBounds (bottom.removeFromTop (28).removeFromLeft (130));
    bottom.removeFromTop (8);
    ringNote_.setBounds (bottom);

    accompanimentCaption_.setBounds (side.removeFromTop (20));
    otherPartToggle_.setBounds (side.removeFromTop (28));
    backingToggle_.setBounds (side.removeFromTop (28));
    side.removeFromTop (8);
    tracksButton_.setVisible (song_ != nullptr);
    tracksButton_.setBounds (side.removeFromTop (34));
    side.removeFromTop (6);
    tracksView_.setVisible (song_ != nullptr && tracksOpen_);
    tracksView_.setBounds (side);
    trackList_.setSize (side.getWidth() - (trackList_.preferredHeight() > side.getHeight() ? 10 : 0),
                        trackList_.preferredHeight());
    trackList_.resized(); // rows may be new while the size stayed the same
    tracksButton_.setButtonText (TRANS ("Tracks and instruments") + "  " + juce::String::fromUTF8 (tracksOpen_ ? "\xe2\x96\xb4" : "\xe2\x96\xbe"));
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
            menu.addItem (TRANS ("Side panel: tracks and instruments"), true, tracksOpen_, [this] { tracksOpen_ = ! tracksOpen_; resized(); });
            menu.addSeparator();
            menu.addItem (TRANS ("Zoom in"), hasSong, false, [this] {
                roll_.setPixelsPerSecond (roll_.pixelsPerSecond() * 1.25);
                tracksRoll_.setPixelsPerSecond (roll_.pixelsPerSecond());
                if (intents_.zoomChanged) intents_.zoomChanged (roll_.pixelsPerSecond()); });
            menu.addItem (TRANS ("Zoom out"), hasSong, false, [this] {
                roll_.setPixelsPerSecond (roll_.pixelsPerSecond() / 1.25);
                tracksRoll_.setPixelsPerSecond (roll_.pixelsPerSecond());
                if (intents_.zoomChanged) intents_.zoomChanged (roll_.pixelsPerSecond()); });
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
