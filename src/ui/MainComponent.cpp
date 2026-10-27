#include "ui/MainComponent.h"

#include "ui/AppLookAndFeel.h"
#include "ui/RollColours.h"

namespace pianoassist::ui
{
namespace
{
const juce::Colour bannerColour { 0xffb3541e };
constexpr int partListWidth = 260;
constexpr int gap = 8;

bool isPianoAssignment (int assignment)
{
    return assignment != 3; // 0 right, 1 left, 2 split; 3 is backing
}

void setupCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, theme::mutedText);
}

void setupValue (juce::Label& label)
{
    label.setFont (juce::FontOptions (18.0f, juce::Font::bold));
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
    logButton_.setTooltip ("Copy or save a diagnostic report, or open the log folder. Nothing is sent anywhere.");
    logButton_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem ("Copy diagnostics to clipboard", [this] { if (intents_.copyDiagnostics) intents_.copyDiagnostics(); });
        menu.addItem ("Save diagnostics to a file...", [this] { if (intents_.saveDiagnostics) intents_.saveDiagnostics(); });
        menu.addItem ("Open log folder", [this] { if (intents_.openLogFolder) intents_.openLogFolder(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&logButton_));
    };
    addAndMakeVisible (logButton_);

    statusLabel_.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (statusLabel_);

    bannerLabel_.setColour (juce::Label::backgroundColourId, bannerColour);
    bannerLabel_.setColour (juce::Label::textColourId, juce::Colours::white);
    bannerLabel_.setFont (juce::FontOptions (15.0f, juce::Font::bold));
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

    openButton_.onClick = [this] { if (intents_.chooseMidiFile) intents_.chooseMidiFile(); };
    addAndMakeVisible (openButton_);
    songTitle_.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    songTitle_.setText ("No song loaded", juce::dontSendNotification);
    addAndMakeVisible (songTitle_);
    songSummary_.setColour (juce::Label::textColourId, theme::mutedText);
    addAndMakeVisible (songSummary_);
    songNotices_.setColour (juce::Label::textColourId, juce::Colours::orange);
    songNotices_.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (songNotices_);

    emptyHint_.setText ("Open a MIDI file (.mid) or drop it here to see its notes.", juce::dontSendNotification);
    emptyHint_.setJustificationType (juce::Justification::centred);
    emptyHint_.setColour (juce::Label::textColourId, theme::mutedText);
    addAndMakeVisible (emptyHint_);

    partsHeader_.setText ("Piano parts: who plays what", juce::dontSendNotification);
    partsHeader_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    partsHeader_.setTooltip ("Hands are inferred from the file and may be wrong; change them here.");
    addChildComponent (partsHeader_);
    tracksHeader_.setText ("Other tracks (played by the app)", juce::dontSendNotification);
    tracksHeader_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addChildComponent (tracksHeader_);
    tracksEmpty_.setText ("This song has no other tracks: every part is piano.", juce::dontSendNotification);
    tracksEmpty_.setColour (juce::Label::textColourId, theme::mutedText);
    tracksEmpty_.setJustificationType (juce::Justification::centred);
    addChildComponent (tracksEmpty_);

    tabs_.addTab ("Practice", theme::panel, 0);
    tabs_.addTab ("Other tracks", theme::panel, 1);
    tabs_.setColour (juce::TabbedButtonBar::tabTextColourId, theme::mutedText);
    tabs_.setColour (juce::TabbedButtonBar::frontTextColourId, theme::text);
    tabs_.setColour (juce::TabbedButtonBar::tabOutlineColourId, theme::panelEdge);
    tabs_.setColour (juce::TabbedButtonBar::frontOutlineColourId, theme::accent);
    tabs_.setTitle ("Views");
    tabs_.setCurrentTabIndex (0, false);
    tabs_.addChangeListener (this);
    addAndMakeVisible (tabs_);

    playButton_.onClick = [this] { if (intents_.playPause) intents_.playPause(); };
    stopButton_.setTooltip ("Stop and return to the start. Notes are released and may ring out briefly.");
    stopButton_.onClick = [this] { if (intents_.stop) intents_.stop(); };
    silenceButton_.setTooltip ("Emergency silence: cut all sound immediately (Esc)");
    silenceButton_.onClick = [this] { if (intents_.silence) intents_.silence(); };
    volume_.setRange (0.0, 1.0);
    volume_.setValue (0.8, juce::dontSendNotification);
    volume_.setTitle ("Volume");
    volume_.onValueChange = [this] { if (intents_.volumeChanged) intents_.volumeChanged (static_cast<float> (volume_.getValue())); };
    volumeLabel_.setText ("Volume", juce::dontSendNotification);
    timeLabel_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    transportNote_.setColour (juce::Label::textColourId, theme::mutedText);
    transportNote_.setFont (juce::FontOptions (13.0f));
    soundLabel_.setText ("Sound", juce::dontSendNotification);
    soundBox_.setTitle ("Sound");
    soundBox_.setTooltip ("What the song sounds like: built-in synth, a SoundFont (.sf2) or a VST3 instrument");
    soundBox_.onChange = [this] {
        const int id = soundBox_.getSelectedId();
        constexpr int loadFont = 900, addFile = 901, scan = 902;
        // Action items are not selections: restore the shown choice first.
        if (id == loadFont || id == addFile || id == scan)
        {
            soundBox_.setSelectedId (sound_.selected, juce::dontSendNotification);
            const auto action = id == loadFont ? intents_.loadSoundFont : id == addFile ? intents_.addPluginFile : intents_.scanPlugins;
            juce::MessageManager::callAsync ([action] { if (action) action(); });
            return;
        }
        if (id > 0 && id != sound_.selected && intents_.soundChosen)
            intents_.soundChosen (id);
    };
    instrumentWindowButton_.setTooltip ("Open the VST3 instrument's own window (sounds, presets)");
    instrumentWindowButton_.onClick = [this] { if (intents_.showInstrumentWindow) intents_.showInstrumentWindow(); };
    for (auto* c : std::initializer_list<juce::Component*> { &soundLabel_, &soundBox_, &instrumentWindowButton_ })
        addAndMakeVisible (c);
    setSound ({});

    for (auto* c : std::initializer_list<juce::Component*> { &playButton_, &stopButton_, &silenceButton_, &volume_,
                                                              &volumeLabel_, &timeLabel_, &transportNote_ })
        addAndMakeVisible (c);

    practiceToggle_.onClick = [this] { if (intents_.practiceChanged) intents_.practiceChanged (practiceToggle_.getToggleState()); };
    modeBox_.addItem ("Both hands", 1);
    modeBox_.addItem ("Right hand", 2);
    modeBox_.addItem ("Left hand", 3);
    modeBox_.setSelectedId (1, juce::dontSendNotification);
    modeBox_.setTooltip ("Which hand you play. The app plays everything else.");
    modeBox_.onChange = [this] { if (intents_.modeChanged) intents_.modeChanged (modeBox_.getSelectedId() - 1); };
    toleranceBox_.addItem ("Strict timing", 1);
    toleranceBox_.addItem ("Normal timing", 2);
    toleranceBox_.addItem ("Easy timing", 3);
    toleranceBox_.setSelectedId (2, juce::dontSendNotification);
    toleranceBox_.setTooltip ("How early or late a note may be played and still count. Strict: 120 ms early / 180 ms late. "
                              "Normal: 250 / 300 ms. Easy: 400 / 600 ms. Within the window the song keeps going.");
    otherPartToggle_.setTooltip ("In one-hand practice, the app plays the other hand for you");
    otherPartToggle_.onClick = [this] { if (intents_.playOtherPartChanged) intents_.playOtherPartChanged (otherPartToggle_.getToggleState()); };
    backingToggle_.setTooltip ("Play the band/backing parts (drums, bass, strings...)");
    backingToggle_.onClick = [this] { if (intents_.playBackingChanged) intents_.playBackingChanged (backingToggle_.getToggleState()); };
    toleranceBox_.onChange = [this] { if (intents_.toleranceChanged) intents_.toleranceChanged (toleranceBox_.getSelectedId() - 1); };
    gateLabel_.setColour (juce::Label::textColourId, theme::mutedText);
    gateLabel_.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &practiceToggle_, &modeBox_, &toleranceBox_, &otherPartToggle_,
                                                              &backingToggle_, &gateLabel_, &board_ })
        addAndMakeVisible (c);
    setWantsKeyboardFocus (true);
    // Tab moves through every control in reading order.
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    inputBox_.setTitle ("MIDI input device");
    modeBox_.setTitle ("Hand to practise");
    toleranceBox_.setTitle ("Timing tolerance");
    volume_.setTitle ("Volume");
    board_.setTitle ("Notes to play next");
    keyboard_.setTitle ("On-screen piano keyboard; click a key to hear it");
    playButton_.setTooltip ("Play / Pause (Space)");
    openButton_.setTooltip ("Open a MIDI file (Ctrl+O), or drag one onto the window");
    practiceToggle_.setTooltip ("Playback stops at each note or chord of your hand until you play it (Ctrl+P)");

    for (auto* roll : { &roll_, &tracksRoll_ })
    {
        roll->onZoomChanged = [this, roll] (double pps) {
            (roll == &roll_ ? tracksRoll_ : roll_).setPixelsPerSecond (pps);
            if (intents_.zoomChanged)
                intents_.zoomChanged (pps);
        };
        roll->onSeekRequested = [this] (double seconds) { if (intents_.seek) intents_.seek (seconds); };
        addChildComponent (roll);
    }
    roll_.setVisible (true);
    roll_.setTitle ("Piano roll of your parts");
    tracksRoll_.setTitle ("Piano roll of the other tracks");
    keyboard_.onKey = [this] (std::uint8_t pitch, bool down) { if (intents_.keyClicked) intents_.keyClicked (pitch, down); };
    addAndMakeVisible (keyboard_);
    emptyHint_.toFront (false);

    setInputMonitor ({});
    setTransport ({});
    setSize (1200, 800);
}

void MainComponent::setTransport (const TransportView& view)
{
    using State = TransportView::State;
    transport_ = view;
    const bool hasSong = view.state != State::noSong;
    playButton_.setEnabled (hasSong && view.audioAvailable);
    stopButton_.setEnabled (hasSong && view.audioAvailable);
    silenceButton_.setEnabled (view.audioAvailable);
    const bool running = view.state == State::playing || view.state == State::waiting;
    playButton_.setButtonText (running ? "Pause" : "Play");
    timeLabel_.setText (hasSong ? formatTime (view.positionSeconds) + " / " + formatTime (view.durationSeconds) : juce::String(),
                        juce::dontSendNotification);

    juce::String note;
    if (! view.audioAvailable)
        note = "No sound: " + toJuce (view.audioText);
    else if (view.mayRingOut && ! running)
        note = view.state == State::paused ? "Playback held; sound may ring out. Silence cuts it." : "Stopped; sound may ring out. Silence cuts it.";
    else if (view.state == State::completed)
        note = "End of song.";
    else
        note = toJuce (view.audioText);
    transportNote_.setText (note, juce::dontSendNotification);
    transportNote_.setColour (juce::Label::textColourId, view.audioAvailable ? theme::mutedText : juce::Colours::orange);

    roll_.setPlayhead (view.positionSeconds, running && tab_ == 0);
    tracksRoll_.setPlayhead (view.positionSeconds, running && tab_ == 1);
}

void MainComponent::setSound (const SoundView& view)
{
    sound_ = view;
    soundBox_.clear (juce::dontSendNotification);
    soundBox_.addItem ("Built-in synth", SoundView::builtIn);
    if (! view.soundFontName.empty())
        soundBox_.addItem ("SoundFont: " + toJuce (view.soundFontName), SoundView::soundFont);
    if (! view.plugins.empty())
        soundBox_.addSectionHeading ("VST3 instruments");
    for (std::size_t i = 0; i < view.plugins.size(); ++i)
        soundBox_.addItem (toJuce (view.plugins[i]), SoundView::firstPlugin + static_cast<int> (i));
    soundBox_.addSeparator();
    soundBox_.addItem ("Load SoundFont (.sf2)...", 900);
    soundBox_.addItem ("Add VST3 instrument file...", 901);
    soundBox_.addItem (view.scanning ? "Scanning..." : "Find VST3 instruments", 902);
    soundBox_.setItemEnabled (902, ! view.scanning);
    soundBox_.setSelectedId (view.selected, juce::dontSendNotification);
    soundBox_.setTooltip (view.status.empty() ? juce::String ("What the song sounds like") : toJuce (view.status));
    instrumentWindowButton_.setEnabled (view.canEdit);
    if (! view.status.empty())
        transportNote_.setTooltip (toJuce (view.status));
}

void MainComponent::setPractice (const PracticeView& view)
{
    practiceToggle_.setToggleState (view.practising, juce::dontSendNotification);
    modeBox_.setSelectedId (view.mode + 1, juce::dontSendNotification);
    keyboard_.setPracticePitches (view.practising ? view.required : std::vector<std::uint8_t> {}, view.wrong);

    toleranceBox_.setSelectedId (view.tolerance + 1, juce::dontSendNotification);
    otherPartToggle_.setToggleState (view.playOtherPart, juce::dontSendNotification);
    otherPartToggle_.setEnabled (view.mode != 0);
    backingToggle_.setToggleState (view.playBacking, juce::dontSendNotification);
    board_.setView (view);
    gateLabel_.setText (view.practising && view.gateCount > 0
                            ? "Step " + juce::String (static_cast<int> (view.gateNumber == 0 ? view.gateCount : view.gateNumber))
                                  + " of " + juce::String (static_cast<int> (view.gateCount))
                            : juce::String(),
                        juce::dontSendNotification);
}

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
    if (ctrl && key.getKeyCode() == 'O' && intents_.chooseMidiFile)
    {
        intents_.chooseMidiFile();
        return true;
    }
    if (ctrl && key.getKeyCode() == 'P')
    {
        practiceToggle_.triggerClick();
        return true;
    }
    if (ctrl && key.getKeyCode() >= '1' && key.getKeyCode() <= '3')
    {
        modeBox_.setSelectedId (key.getKeyCode() - '0', juce::sendNotificationSync);
        return true;
    }
    if (ctrl && key.getKeyCode() == juce::KeyPress::tabKey)
    {
        showTab (1 - tab_); // Ctrl+Tab: Practice <-> Other tracks
        return true;
    }
    if (ctrl && key.getKeyCode() == 'T')
    {
        toleranceBox_.setSelectedId (toleranceBox_.getSelectedId() % 3 + 1, juce::sendNotificationSync);
        return true;
    }
    return false;
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
    const bool bannerChanged = view.bannerText.empty() != view_.bannerText.empty();
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

    roll_.setHeldInputPitches (view.heldPitches);
    keyboard_.setHeldPitches (view.heldPitches);
    if (bannerChanged)
        resized();
}

void MainComponent::setSong (std::shared_ptr<const PianoRollView> song)
{
    song_ = std::move (song);
    roll_.setSong (song_.get());

    songTitle_.setText (song_ ? toJuce (song_->title) : juce::String ("No song loaded"), juce::dontSendNotification);
    songSummary_.setText (song_ ? toJuce (song_->summary) : juce::String(), juce::dontSendNotification);
    juce::String notices;
    if (song_)
        for (const auto& notice : song_->notices)
            notices << (notices.isEmpty() ? "" : "  ") << toJuce (notice);
    if (song_ && song_->needsPartCheck)
        notices = "Hands were guessed from pitch: check the parts on the left (marked ?) before practising."
                  + (notices.isEmpty() ? juce::String() : "  " + notices);
    songNotices_.setText (notices, juce::dontSendNotification);
    songNotices_.setColour (juce::Label::textColourId, theme::warning);
    emptyHint_.setVisible (song_ == nullptr);
    tracksRoll_.setSong (song_.get());
    if (song_)
        for (std::size_t i = 0; i < song_->parts.size(); ++i)
        {
            const bool piano = isPianoAssignment (song_->parts[i].assignment);
            roll_.setPartVisible (i, piano);
            tracksRoll_.setPartVisible (i, ! piano);
        }
    if (! song_ || muted_.size() != song_->parts.size())
        muted_.assign (song_ ? song_->parts.size() : 0, false);
    rebuildPartList();
    showTab (tab_);
}

void MainComponent::setSongError (const std::string& message)
{
    songNotices_.setText (toJuce (message), juce::dontSendNotification);
    songNotices_.setColour (juce::Label::textColourId, juce::Colours::orangered);
}

juce::ComboBox* MainComponent::makeRoleBox (std::size_t part, int assignment, juce::Colour colour)
{
    auto* role = new juce::ComboBox();
    role->addItem ("Right hand", 1);
    role->addItem ("Left hand", 2);
    role->addItem ("Both hands (split at C4)", 3);
    role->addItem ("Other track (app plays it)", 4);
    role->setSelectedId (assignment + 1, juce::dontSendNotification);
    role->setTitle ("Who plays this part");
    role->setTooltip ("Who plays this part. Change it if the guess is wrong.");
    role->setColour (juce::ComboBox::outlineColourId, colour);
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

void MainComponent::rebuildPartList()
{
    pianoRows_.clear();
    trackRows_.clear();
    if (! song_)
        return;

    int backingCount = 0;
    for (std::size_t i = 0; i < song_->parts.size(); ++i)
    {
        const auto& part = song_->parts[i];
        const auto colour = roleColour (part.role);
        PartRow row;
        row.part = i;
        row.role.reset (makeRoleBox (i, part.assignment, colour));
        row.detail = std::make_unique<juce::Label>();
        row.detail->setFont (juce::FontOptions (12.0f));
        row.detail->setColour (juce::Label::textColourId, theme::mutedText);

        if (isPianoAssignment (part.assignment))
        {
            auto label = std::make_unique<juce::Label>();
            label->setText (toJuce ((part.guessedFromPitch ? "? " : "") + part.label), juce::dontSendNotification);
            label->setColour (juce::Label::textColourId, colour.brighter (0.3f));
            label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
            if (part.guessedFromPitch)
                label->setTooltip ("Guessed from pitch: please check");
            row.head = std::move (label);
            row.detail->setText (toJuce (part.detail), juce::dontSendNotification);
        }
        else
        {
            ++backingCount;
            auto play = std::make_unique<juce::ToggleButton> (toJuce (part.instrument + " · " + part.label));
            play->setToggleState (i >= muted_.size() || ! muted_[i], juce::dontSendNotification);
            play->setColour (juce::ToggleButton::textColourId, theme::text);
            play->setTooltip ("Play this track. Untick to mute it.");
            auto* raw = play.get();
            play->onClick = [this, i, raw] {
                if (i < muted_.size())
                    muted_[i] = ! raw->getToggleState();
                if (intents_.partMuteChanged)
                    intents_.partMuteChanged (i, ! raw->getToggleState());
            };
            row.head = std::move (play);
            row.detail->setText (toJuce (part.detail), juce::dontSendNotification);
        }
        for (auto* c : std::initializer_list<juce::Component*> { row.head.get(), row.role.get(), row.detail.get() })
            addChildComponent (c);
        (isPianoAssignment (part.assignment) ? pianoRows_ : trackRows_).push_back (std::move (row));
    }
    tabs_.setTabName (1, backingCount > 0 ? "Other tracks (" + juce::String (backingCount) + ")" : juce::String ("Other tracks"));
}

void MainComponent::showTab (int index)
{
    tab_ = index;
    if (tabs_.getCurrentTabIndex() != index)
        tabs_.setCurrentTabIndex (index, false);
    const bool practice = index == 0;
    const bool hasSong = song_ != nullptr;
    roll_.setVisible (practice);
    tracksRoll_.setVisible (! practice && hasSong);
    partsHeader_.setVisible (practice && hasSong);
    tracksHeader_.setVisible (! practice && hasSong);
    tracksEmpty_.setVisible (! practice && hasSong && trackRows_.empty());
    for (auto& row : pianoRows_)
        for (auto* c : std::initializer_list<juce::Component*> { row.head.get(), row.role.get(), row.detail.get() })
            c->setVisible (practice);
    for (auto& row : trackRows_)
        for (auto* c : std::initializer_list<juce::Component*> { row.head.get(), row.role.get(), row.detail.get() })
            c->setVisible (! practice);
    resized();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
    for (const auto& panel : panels_)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (panel.toFloat(), 8.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
}

void MainComponent::resized()
{
    panels_.clear();
    auto area = getLocalBounds().reduced (12);
    const auto panel = [this] (juce::Rectangle<int> r) {
        panels_.push_back (r);
        return r.reduced (10, 8);
    };

    // 1. Input: device row and live monitor.
    {
        auto box = panel (area.removeFromTop (bannerLabel_.isVisible() ? 140 : 102));
        auto bar = box.removeFromTop (30);
        inputLabel_.setBounds (bar.removeFromLeft (84));
        logButton_.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (6);
        rescanButton_.setBounds (bar.removeFromRight (80));
        bar.removeFromRight (6);
        inputBox_.setBounds (bar.removeFromLeft (std::min (bar.getWidth() / 2, 420)));
        bar.removeFromLeft (10);
        statusLabel_.setBounds (bar);
        if (bannerLabel_.isVisible())
        {
            box.removeFromTop (6);
            bannerLabel_.setBounds (box.removeFromTop (32));
        }
        box.removeFromTop (8);
        auto monitor = box.removeFromTop (44);
        const auto columnWidth = monitor.getWidth() / 4;
        const std::pair<juce::Label*, juce::Label*> columns[] = { { &lastNoteCaption_, &lastNoteValue_ },
                                                                  { &heldCaption_, &heldValue_ },
                                                                  { &sustainCaption_, &sustainValue_ },
                                                                  { &rateCaption_, &rateValue_ } };
        for (const auto& [caption, value] : columns)
        {
            auto column = monitor.removeFromLeft (columnWidth);
            caption->setBounds (column.removeFromTop (18));
            value->setBounds (column);
        }
    }
    area.removeFromTop (gap);

    // 2. Song, transport, sound and practice controls.
    {
        const bool narrow = area.getWidth() < 1180;
        const int rows = 3 + (narrow ? 1 : 0);
        const int notices = songNotices_.getText().isEmpty() ? 0 : 22;
        auto box = panel (area.removeFromTop (16 + rows * 34 + notices));

        auto songBar = box.removeFromTop (30);
        openButton_.setBounds (songBar.removeFromLeft (150));
        songBar.removeFromLeft (10);
        songTitle_.setBounds (songBar.removeFromLeft (std::min (songBar.getWidth() / 2, 380)));
        songSummary_.setBounds (songBar);
        songNotices_.setBounds (box.removeFromTop (notices));
        box.removeFromTop (4);

        auto transportBar = box.removeFromTop (30);
        playButton_.setBounds (transportBar.removeFromLeft (80));
        transportBar.removeFromLeft (6);
        stopButton_.setBounds (transportBar.removeFromLeft (70));
        transportBar.removeFromLeft (6);
        silenceButton_.setBounds (transportBar.removeFromLeft (80));
        transportBar.removeFromLeft (12);
        timeLabel_.setBounds (transportBar.removeFromLeft (110));
        volume_.setBounds (transportBar.removeFromRight (140));
        volumeLabel_.setBounds (transportBar.removeFromRight (56));
        auto soundRow = transportBar;
        if (narrow)
        {
            transportNote_.setBounds (transportBar);
            box.removeFromTop (4);
            soundRow = box.removeFromTop (30);
        }
        soundLabel_.setBounds (soundRow.removeFromLeft (50));
        soundBox_.setBounds (soundRow.removeFromLeft (240));
        soundRow.removeFromLeft (6);
        instrumentWindowButton_.setBounds (soundRow.removeFromLeft (100));
        soundRow.removeFromLeft (10);
        if (! narrow)
            transportNote_.setBounds (soundRow);
        box.removeFromTop (4);

        auto practiceBar = box.removeFromTop (30);
        practiceToggle_.setBounds (practiceBar.removeFromLeft (220));
        practiceBar.removeFromLeft (6);
        modeBox_.setBounds (practiceBar.removeFromLeft (120));
        practiceBar.removeFromLeft (8);
        toleranceBox_.setBounds (practiceBar.removeFromLeft (136));
        practiceBar.removeFromLeft (12);
        otherPartToggle_.setBounds (practiceBar.removeFromLeft (134));
        backingToggle_.setBounds (practiceBar.removeFromLeft (120));
        gateLabel_.setBounds (practiceBar.removeFromRight (std::max (0, std::min (160, practiceBar.getWidth()))));
    }
    area.removeFromTop (gap);

    // 3. Keyboard and the "play next" board at the bottom, always visible.
    keyboard_.setBounds (area.removeFromBottom (84));
    area.removeFromBottom (gap);
    board_.setBounds (area.removeFromBottom (92));
    area.removeFromBottom (gap);

    // 4. Tabs: your piano parts, or the other tracks.
    tabs_.setBounds (area.removeFromTop (30).withWidth (std::min (area.getWidth(), 360)));
    auto content = panel (area);
    if (song_)
    {
        auto list = content.removeFromLeft (partListWidth);
        content.removeFromLeft (gap);
        const bool practice = tab_ == 0;
        (practice ? partsHeader_ : tracksHeader_).setBounds (list.removeFromTop (22));
        for (auto& row : practice ? pianoRows_ : trackRows_)
        {
            row.head->setBounds (list.removeFromTop (24));
            row.role->setBounds (list.removeFromTop (24).withTrimmedLeft (practice ? 0 : 26));
            row.detail->setBounds (list.removeFromTop (18).withTrimmedLeft (practice ? 0 : 26));
            list.removeFromTop (6);
        }
        tracksEmpty_.setBounds (content.withSizeKeepingCentre (content.getWidth(), 40));
    }
    roll_.setBounds (content);
    tracksRoll_.setBounds (content);
    emptyHint_.setBounds (content.withSizeKeepingCentre (content.getWidth(), 40));
    repaint();
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isMidiFile (files[0]);
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && intents_.openMidiFile)
        intents_.openMidiFile (juce::File (files[0]));
}

} // namespace pianoassist::ui
