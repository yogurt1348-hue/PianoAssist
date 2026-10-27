#include "ui/MainComponent.h"

#include "ui/RollColours.h"

namespace pianoassist::ui
{
namespace
{
const juce::Colour bannerColour { 0xffb3541e };
const juce::Colour panelColour { 0xff2a3238 };
constexpr int partListWidth = 250;

void setupCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
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
    logButton_.onClick = [this] { if (intents_.openLogFolder) intents_.openLogFolder(); };
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
    songSummary_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (songSummary_);
    songNotices_.setColour (juce::Label::textColourId, juce::Colours::orange);
    songNotices_.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (songNotices_);

    emptyHint_.setText ("Open a MIDI file (.mid) or drop it here to see its notes.", juce::dontSendNotification);
    emptyHint_.setJustificationType (juce::Justification::centred);
    emptyHint_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (emptyHint_);

    partsHeader_.setText ("Parts (inferred, may be wrong)", juce::dontSendNotification);
    partsHeader_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addChildComponent (partsHeader_);

    playButton_.setTooltip ("Play / Pause (Space)");
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
    transportNote_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    transportNote_.setFont (juce::FontOptions (13.0f));
    for (auto* c : std::initializer_list<juce::Component*> { &playButton_, &stopButton_, &silenceButton_, &volume_,
                                                              &volumeLabel_, &timeLabel_, &transportNote_ })
        addAndMakeVisible (c);

    practiceToggle_.setTooltip ("Playback stops at each note or chord of your hand until you play it on the piano");
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
    toleranceBox_.onChange = [this] { if (intents_.toleranceChanged) intents_.toleranceChanged (toleranceBox_.getSelectedId() - 1); };
    gateLabel_.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    gateLabel_.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &practiceToggle_, &modeBox_, &toleranceBox_, &gateLabel_, &board_ })
        addAndMakeVisible (c);
    setWantsKeyboardFocus (true);

    roll_.onZoomChanged = [this] (double pps) { if (intents_.zoomChanged) intents_.zoomChanged (pps); };
    roll_.onSeekRequested = [this] (double seconds) { if (intents_.seek) intents_.seek (seconds); };
    addAndMakeVisible (roll_);
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
    transportNote_.setColour (juce::Label::textColourId, view.audioAvailable ? juce::Colours::lightgrey : juce::Colours::orange);

    roll_.setPlayhead (view.positionSeconds, running);
}

void MainComponent::setPractice (const PracticeView& view)
{
    practiceToggle_.setToggleState (view.practising, juce::dontSendNotification);
    modeBox_.setSelectedId (view.mode + 1, juce::dontSendNotification);
    keyboard_.setPracticePitches (view.practising ? view.required : std::vector<std::uint8_t> {}, view.wrong);

    toleranceBox_.setSelectedId (view.tolerance + 1, juce::dontSendNotification);
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
    songNotices_.setText (notices, juce::dontSendNotification);
    songNotices_.setColour (juce::Label::textColourId, juce::Colours::orange);
    emptyHint_.setVisible (song_ == nullptr);
    rebuildPartList();
    resized();
}

void MainComponent::setSongError (const std::string& message)
{
    songNotices_.setText (toJuce (message), juce::dontSendNotification);
    songNotices_.setColour (juce::Label::textColourId, juce::Colours::orangered);
}

void MainComponent::rebuildPartList()
{
    partToggles_.clear();
    partDetails_.clear();
    partsHeader_.setVisible (song_ != nullptr);
    if (! song_)
        return;

    for (std::size_t i = 0; i < song_->parts.size(); ++i)
    {
        const auto& part = song_->parts[i];
        auto* toggle = partToggles_.add (new juce::ToggleButton (toJuce (part.roleText + ": " + part.label)));
        toggle->setToggleState (true, juce::dontSendNotification);
        toggle->setColour (juce::ToggleButton::textColourId, roleColour (part.role).brighter (0.3f));
        toggle->setColour (juce::ToggleButton::tickColourId, roleColour (part.role));
        toggle->setTooltip ("Show or hide this part in the piano roll");
        toggle->onClick = [this, i, toggle] {
            roll_.setPartVisible (i, toggle->getToggleState());
            if (intents_.partVisibilityChanged)
                intents_.partVisibilityChanged (i, toggle->getToggleState());
        };
        addAndMakeVisible (toggle);

        auto* detail = partDetails_.add (new juce::Label());
        detail->setText (toJuce (part.detail), juce::dontSendNotification);
        detail->setFont (juce::FontOptions (12.0f));
        detail->setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (detail);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    g.setColour (panelColour);
    g.fillRoundedRectangle (lastNoteCaption_.getBounds().getUnion (rateValue_.getBounds()).expanded (10, 4).toFloat(), 6.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto bar = area.removeFromTop (30);
    inputLabel_.setBounds (bar.removeFromLeft (84));
    logButton_.setBounds (bar.removeFromRight (130));
    bar.removeFromRight (6);
    rescanButton_.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    inputBox_.setBounds (bar.removeFromLeft (std::min (bar.getWidth(), 420)));
    bar.removeFromLeft (10);
    statusLabel_.setBounds (bar);

    if (bannerLabel_.isVisible())
    {
        area.removeFromTop (6);
        bannerLabel_.setBounds (area.removeFromTop (32));
    }

    area.removeFromTop (10);
    auto monitor = area.removeFromTop (44).reduced (10, 0);
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

    area.removeFromTop (12);
    auto songBar = area.removeFromTop (30);
    openButton_.setBounds (songBar.removeFromLeft (150));
    songBar.removeFromLeft (10);
    songTitle_.setBounds (songBar.removeFromLeft (std::min (songBar.getWidth() / 2, 380)));
    songSummary_.setBounds (songBar);
    songNotices_.setBounds (area.removeFromTop (songNotices_.getText().isEmpty() ? 0 : 22));

    area.removeFromTop (6);
    auto transportBar = area.removeFromTop (30);
    playButton_.setBounds (transportBar.removeFromLeft (80));
    transportBar.removeFromLeft (6);
    stopButton_.setBounds (transportBar.removeFromLeft (70));
    transportBar.removeFromLeft (6);
    silenceButton_.setBounds (transportBar.removeFromLeft (80));
    transportBar.removeFromLeft (12);
    timeLabel_.setBounds (transportBar.removeFromLeft (120));
    volume_.setBounds (transportBar.removeFromRight (160));
    volumeLabel_.setBounds (transportBar.removeFromRight (60));
    transportNote_.setBounds (transportBar);

    area.removeFromTop (6);
    auto practiceBar = area.removeFromTop (30);
    practiceToggle_.setBounds (practiceBar.removeFromLeft (230));
    practiceBar.removeFromLeft (6);
    modeBox_.setBounds (practiceBar.removeFromLeft (130));
    practiceBar.removeFromLeft (12);
    toleranceBox_.setBounds (practiceBar.removeFromLeft (140));
    gateLabel_.setBounds (practiceBar.removeFromRight (200));

    area.removeFromTop (6);
    keyboard_.setBounds (area.removeFromBottom (84));
    area.removeFromBottom (6);
    board_.setBounds (area.removeFromBottom (92));
    area.removeFromBottom (6);

    if (song_)
    {
        auto list = area.removeFromLeft (partListWidth);
        area.removeFromLeft (8);
        partsHeader_.setBounds (list.removeFromTop (22));
        for (int i = 0; i < partToggles_.size(); ++i)
        {
            partToggles_[i]->setBounds (list.removeFromTop (24));
            partDetails_[i]->setBounds (list.removeFromTop (18).withTrimmedLeft (26));
            list.removeFromTop (4);
        }
    }
    roll_.setBounds (area);
    emptyHint_.setBounds (area.withSizeKeepingCentre (area.getWidth(), 40));
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
