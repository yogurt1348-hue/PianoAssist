#include "AdaptiveSongPanel.h"

#include "CadenzaLookAndFeel.h"

#include <algorithm>

namespace cadenza::ui
{
namespace
{
using namespace cadenza::adaptive;

juce::Colour stateColour(TrackingState s)
{
    switch (s) {
        case TrackingState::Tracking:    return juce::Colour(0xff4caf50);
        case TrackingState::Uncertain:   return juce::Colour(0xffffc107);
        case TrackingState::Waiting:     return juce::Colour(0xff42a5f5);
        case TrackingState::Lost:        return juce::Colour(0xffef5350);
        case TrackingState::Reacquiring: return juce::Colour(0xffff9800);
        case TrackingState::Idle:        break;
    }
    return juce::Colours::grey;
}

void caption(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, CadenzaLookAndFeel::textDim());
    l.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
}

void value(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colours::white);
    l.setFont(juce::Font(juce::FontOptions(15.0f)));
}
}

AdaptiveSongPanel::AdaptiveSongPanel(Callbacks callbacks)
    : m_cb(std::move(callbacks))
{
    for (auto* b : { &m_loadMidi, &m_loadBacking, &m_clearBacking, &m_play }) addAndMakeVisible(*b);
    m_loadMidi.onClick = [this] { chooseMidi(false); };
    m_loadBacking.onClick = [this] { chooseMidi(true); };
    m_clearBacking.setTooltip("Use the backing tracks of the song file itself");
    m_clearBacking.onClick = [this] {
        if (auto* e = m_cb.engine ? m_cb.engine() : nullptr) e->clearBackingMidi();
        refreshTracks();
    };
    m_play.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d32));
    m_play.onClick = [this] { togglePlay(); };

    for (auto* l : { &m_fileLabel, &m_backingLabel }) {
        l->setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(*l);
    }
    m_fileLabel.setText("No MIDI file loaded", juce::dontSendNotification);
    m_backingLabel.setText("Backing: tracks of the same file", juce::dontSendNotification);

    caption(m_refCaption, "Reference track (you play)");
    caption(m_refChannelCaption, "Channel");
    caption(m_backingCaption, "Backing tracks (Cadenza plays)");
    caption(m_inputCaption, "MIDI input");
    caption(m_startCaption, "Start");
    caption(m_smoothingCaption, "Tempo smoothing");
    caption(m_sensitivityCaption, "Tracking sensitivity");
    caption(m_waitCaption, "Wait timeout (s)");
    for (auto* l : { &m_refCaption, &m_refChannelCaption, &m_backingCaption, &m_inputCaption, &m_startCaption,
                     &m_smoothingCaption, &m_sensitivityCaption, &m_waitCaption })
        addAndMakeVisible(*l);

    m_refChannel.addItem("All", 1);
    for (int c = 1; c <= 16; ++c) m_refChannel.addItem(juce::String(c), c + 1);
    m_refChannel.setSelectedId(1, juce::dontSendNotification);
    m_refTrack.onChange = [this] {
        // The part you play is never also played by the band (same-file layout).
        auto* e = m_cb.engine ? m_cb.engine() : nullptr;
        if (e != nullptr && !e->hasSeparateBacking() && m_refChannel.getSelectedId() <= 1)
            for (std::size_t i = 0; i < m_backingToggles.size(); ++i)
                if (m_backingTrackIndex[i] == m_refTrack.getSelectedId() - 1)
                    m_backingToggles[i]->setToggleState(false, juce::dontSendNotification);
        rebuild();
    };
    m_refChannel.onChange = [this] { rebuild(); };

    m_startMode.addItem("Wait for me (start with my first note)", 1);
    m_startMode.addItem("Play the intro, wait at my entry", 2);
    m_startMode.setSelectedId(1, juce::dontSendNotification);
    m_alignment.addItem("Align files by beats", 1);
    m_alignment.addItem("Align files by seconds", 2);
    m_alignment.setSelectedId(1, juce::dontSendNotification);
    m_alignment.onChange = [this] { rebuild(); };

    m_input.addItem("Auto (main keyboard port)", 1);
    if (m_cb.midiInputs) {
        const auto inputs = m_cb.midiInputs();
        for (int i = 0; i < inputs.size(); ++i) m_input.addItem(inputs[i], i + 2);
        const auto selected = m_cb.selectedMidiInput ? m_cb.selectedMidiInput() : juce::String();
        const int idx = inputs.indexOf(selected);
        m_input.setSelectedId(idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    }
    m_input.onChange = [this] {
        if (!m_cb.selectMidiInput) return;
        m_cb.selectMidiInput(m_input.getSelectedId() <= 1 ? juce::String() : m_input.getText());
    };
    for (auto* c : { &m_refTrack, &m_refChannel, &m_input, &m_startMode, &m_alignment }) addAndMakeVisible(*c);

    m_backingView.setViewedComponent(&m_backingList, false);
    m_backingView.setScrollBarsShown(true, false);
    addAndMakeVisible(m_backingView);

    auto slider = [this](juce::Slider& s, double lo, double hi, double step, double def) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
        s.setRange(lo, hi, step);
        s.setValue(def, juce::dontSendNotification);
        addAndMakeVisible(s);
    };
    slider(m_smoothing, 0.1, 1.0, 0.05, 0.5);
    slider(m_sensitivity, 1.0, 10.0, 1.0, 6.0);
    slider(m_wait, 0.1, 2.0, 0.05, 0.35);
    m_smoothing.setTooltip("Higher = the band follows tempo changes faster; lower = steadier");
    m_sensitivity.setTooltip("Higher = stronger phase correction and gives up sooner on wrong notes");
    m_wait.setTooltip("How late you may be before the band waits for you");

    if (auto* e = m_cb.engine ? m_cb.engine() : nullptr)
        m_diagnostics.setToggleState(e->diagnosticsLogging(), juce::dontSendNotification);
    m_diagnostics.onClick = [this] {
        if (auto* e = m_cb.engine ? m_cb.engine() : nullptr) e->setDiagnosticsLogging(m_diagnostics.getToggleState());
    };
    m_diagnostics.setColour(juce::ToggleButton::textColourId, CadenzaLookAndFeel::textDim());
    addAndMakeVisible(m_diagnostics);

    for (auto* l : { &m_state, &m_position, &m_bpm, &m_confidence, &m_phase, &m_notes }) {
        value(*l);
        addAndMakeVisible(*l);
    }
    m_state.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    m_warnings.setColour(juce::Label::textColourId, juce::Colour(0xffffcc80));
    m_warnings.setJustificationType(juce::Justification::topLeft);
    m_warnings.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(m_warnings);

    refreshTracks();
    setSize(640, 780);
    startTimerHz(15);
}

AdaptiveSongPanel::~AdaptiveSongPanel()
{
    stopTimer();
    if (auto* e = m_cb.engine ? m_cb.engine() : nullptr) e->stop();
    if (m_cb.setModeActive) m_cb.setModeActive(false);
}

void AdaptiveSongPanel::chooseMidi(bool backing)
{
    m_chooser = std::make_unique<juce::FileChooser>(backing ? "Choose the backing MIDI file" : "Choose a MIDI song",
                                                    juce::File(), "*.mid;*.midi;*.kar");
    juce::Component::SafePointer<AdaptiveSongPanel> safe(this);
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe, backing](const juce::FileChooser& fc) {
        auto* self = safe.getComponent();
        if (self == nullptr) return;
        const auto file = fc.getResult();
        auto* e = self->m_cb.engine ? self->m_cb.engine() : nullptr;
        if (file == juce::File() || e == nullptr) return;
        juce::String error;
        const bool ok = backing ? e->loadBackingMidi(file, error) : e->loadMidi(file, error);
        if (!ok) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Adaptive Song", error);
            return;
        }
        self->m_play.setButtonText("Play");
        self->refreshTracks();
    });
}

void AdaptiveSongPanel::refreshTracks()
{
    auto* e = m_cb.engine ? m_cb.engine() : nullptr;
    const auto* midi = e ? e->midi() : nullptr;
    const auto* backingFile = e && e->hasSeparateBacking() ? e->backingMidi() : midi;

    m_fileLabel.setText(midi ? e->midiName() : juce::String("No MIDI file loaded"), juce::dontSendNotification);
    m_backingLabel.setText(e && e->hasSeparateBacking() ? "Backing file: " + e->backingName()
                                                        : juce::String("Backing: tracks of the same file"),
                           juce::dontSendNotification);
    m_clearBacking.setEnabled(e && e->hasSeparateBacking());
    m_alignment.setVisible(e && e->hasSeparateBacking());

    m_refTrack.clear(juce::dontSendNotification);
    m_backingToggles.clear();
    m_backingTrackIndex.clear();
    m_backingList.removeAllChildren();
    if (midi == nullptr) { rebuild(); resized(); return; }

    const auto infos = describeTracks(*midi);
    for (const auto& t : infos)
        if (t.noteCount > 0) m_refTrack.addItem(juce::String(describeTrack(t)), t.index + 1);
    const int suggested = suggestReferenceTrack(*midi);
    if (suggested >= 0) m_refTrack.setSelectedId(suggested + 1, juce::dontSendNotification);

    const auto backingInfos = describeTracks(*backingFile);
    const std::vector<int> refTracks = suggested >= 0 && backingFile == midi ? std::vector<int> { suggested } : std::vector<int> {};
    const auto suggestedBacking = suggestBackingTracks(*backingFile, refTracks);
    for (const auto& t : backingInfos) {
        if (t.noteCount == 0) continue;
        auto toggle = std::make_unique<juce::ToggleButton>(juce::String(describeTrack(t)));
        toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        toggle->setToggleState(std::find(suggestedBacking.begin(), suggestedBacking.end(), t.index) != suggestedBacking.end(),
                               juce::dontSendNotification);
        toggle->onClick = [this] { rebuild(); };
        m_backingList.addAndMakeVisible(*toggle);
        m_backingToggles.push_back(std::move(toggle));
        m_backingTrackIndex.push_back(t.index);
    }
    resized();
    rebuild();
}

AdaptiveSessionConfig AdaptiveSongPanel::configFromControls() const
{
    AdaptiveSessionConfig cfg;
    const double s = m_sensitivity.getValue();
    cfg.follower.tempo.smoothing = m_smoothing.getValue();
    cfg.follower.lostAfterMisses = std::max(3, static_cast<int>(11.0 - s));
    cfg.follower.waitGraceSec = m_wait.getValue();
    cfg.transport.phaseCorrectionTimeSec = std::max(0.4, 2.2 - 0.2 * s);
    cfg.transport.startMode = m_startMode.getSelectedId() == 2 ? AdaptiveStartMode::FreeRunIntro
                                                               : AdaptiveStartMode::WaitForPerformer;
    return cfg;
}

void AdaptiveSongPanel::rebuild()
{
    auto* e = m_cb.engine ? m_cb.engine() : nullptr;
    if (e == nullptr || !e->hasMidi() || m_refTrack.getSelectedId() <= 0) {
        m_warnings.setText(e && e->hasMidi() ? "Choose the track you will play." : "Load a MIDI file to start.",
                           juce::dontSendNotification);
        return;
    }
    AdaptiveSongOptions opt;
    opt.referenceTracks = { m_refTrack.getSelectedId() - 1 };
    opt.referenceChannel = m_refChannel.getSelectedId() <= 1 ? -1 : m_refChannel.getSelectedId() - 2;
    for (std::size_t i = 0; i < m_backingToggles.size(); ++i)
        if (m_backingToggles[i]->getToggleState()) opt.backingTracks.push_back({ m_backingTrackIndex[i], 0xFFFF });
    opt.alignment = m_alignment.getSelectedId() == 2 ? TimelineAlignment::BySeconds : TimelineAlignment::ByBeats;
    opt.reservedOutputChannels = m_cb.reservedChannels ? m_cb.reservedChannels() : 0;

    e->setConfig(configFromControls());
    const auto result = e->apply(opt);
    m_play.setButtonText("Play");
    if (!result.ok) {
        m_warnings.setText("Cannot use this selection: " + juce::String(result.error), juce::dontSendNotification);
        return;
    }
    juce::String text;
    for (const auto& w : result.song.warnings) text << "- " << juce::String(w) << "\n";
    if (text.isEmpty())
        text = juce::String(result.song.reference->groupCount()) + " reference events, "
               + juce::String(static_cast<int>(result.song.backing->events().size())) + " backing events. Ready.";
    m_warnings.setText(text, juce::dontSendNotification);
}

void AdaptiveSongPanel::togglePlay()
{
    auto* e = m_cb.engine ? m_cb.engine() : nullptr;
    if (e == nullptr) return;
    if (e->playRequested()) {
        e->stop();
        m_play.setButtonText("Play");
        return;
    }
    if (!e->songReady()) rebuild();
    if (!e->songReady()) return;
    e->setConfig(configFromControls());
    if (m_cb.setModeActive) m_cb.setModeActive(true);
    e->play();
    m_play.setButtonText("Stop");
}

void AdaptiveSongPanel::timerCallback()
{
    auto* e = m_cb.engine ? m_cb.engine() : nullptr;
    if (e == nullptr) return;
    const auto st = e->status();
    const bool playing = e->playRequested();
    if (m_wasPlaying && !playing) m_play.setButtonText("Play");
    m_wasPlaying = playing;

    juce::String state = !st.loaded ? "NO SONG" : (!playing ? "STOPPED" : juce::String(toString(st.state)));
    if (playing && st.state == TrackingState::Idle) state = "READY - start playing";
    if (st.finished) state = "FINISHED";
    m_state.setText(state, juce::dontSendNotification);
    m_state.setColour(juce::Label::textColourId, playing ? stateColour(st.state) : juce::Colours::grey);
    m_position.setText("Bar " + juce::String(st.playheadBarBeat.bar + 1) + "  beat " + juce::String(st.playheadBarBeat.beat + 1)
                           + "   (tick " + juce::String(static_cast<int>(st.playheadTick)) + ", event "
                           + juce::String(std::max(0, st.groupIndex + 1)) + "/" + juce::String(st.groupCount) + ")",
                       juce::dontSendNotification);
    m_bpm.setText("You: " + juce::String(st.estimatedBpm, 1) + " BPM    Band: " + juce::String(st.playbackBpm, 1) + " BPM",
                  juce::dontSendNotification);
    m_confidenceValue = st.confidence;
    m_confidence.setText("Confidence " + juce::String(juce::roundToInt(st.confidence * 100.0)) + "%", juce::dontSendNotification);
    m_phase.setText("Band " + juce::String(std::abs(st.phaseErrorMs), 0) + " ms "
                        + (st.phaseErrorMs >= 0 ? "behind you" : "ahead of you"),
                    juce::dontSendNotification);
    m_notes.setText("Matched " + juce::String(st.matchedNotes) + "  /  extra " + juce::String(st.extraNotes),
                    juce::dontSendNotification);
    repaint(m_confidence.getBounds().withHeight(m_confidence.getHeight() + 8));
}

void AdaptiveSongPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20242e));
    auto bar = m_confidence.getBounds().removeFromBottom(4).translated(0, 6).toFloat();
    g.setColour(juce::Colour(0xff3a3f4b));
    g.fillRoundedRectangle(bar, 2.0f);
    g.setColour(m_confidenceValue >= 0.6 ? juce::Colour(0xff4caf50)
                                         : (m_confidenceValue >= 0.45 ? juce::Colour(0xffffc107) : juce::Colour(0xffef5350)));
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, m_confidenceValue))), 2.0f);
}

void AdaptiveSongPanel::resized()
{
    auto a = getLocalBounds().reduced(12);
    auto row = [&a](int h) { auto r = a.removeFromTop(h); a.removeFromTop(6); return r; };

    auto r = row(28);
    m_loadMidi.setBounds(r.removeFromLeft(120));
    r.removeFromLeft(8);
    m_fileLabel.setBounds(r);
    r = row(28);
    m_loadBacking.setBounds(r.removeFromLeft(170));
    r.removeFromLeft(4);
    m_clearBacking.setBounds(r.removeFromLeft(28));
    r.removeFromLeft(8);
    if (m_alignment.isVisible()) m_alignment.setBounds(r.removeFromRight(190));
    m_backingLabel.setBounds(r);

    m_refCaption.setBounds(row(16));
    r = row(26);
    m_refChannel.setBounds(r.removeFromRight(70));
    m_refChannelCaption.setBounds(r.removeFromRight(60));
    m_refTrack.setBounds(r.withTrimmedRight(8));

    m_backingCaption.setBounds(row(16));
    m_backingView.setBounds(row(120));
    const int listH = static_cast<int>(m_backingToggles.size()) * 24;
    m_backingList.setSize(m_backingView.getWidth() - m_backingView.getScrollBarThickness(), std::max(listH, 1));
    for (std::size_t i = 0; i < m_backingToggles.size(); ++i)
        m_backingToggles[i]->setBounds(4, static_cast<int>(i) * 24, m_backingList.getWidth() - 8, 22);

    r = row(26);
    m_inputCaption.setBounds(r.removeFromLeft(80));
    m_input.setBounds(r.removeFromLeft(250));
    r.removeFromLeft(12);
    m_startCaption.setBounds(r.removeFromLeft(40));
    m_startMode.setBounds(r);

    auto sliderRow = [&](juce::Label& cap, juce::Slider& s) {
        auto rr = row(24);
        cap.setBounds(rr.removeFromLeft(150));
        s.setBounds(rr);
    };
    sliderRow(m_smoothingCaption, m_smoothing);
    sliderRow(m_sensitivityCaption, m_sensitivity);
    sliderRow(m_waitCaption, m_wait);
    m_diagnostics.setBounds(row(22).removeFromLeft(200));

    r = row(40);
    m_play.setBounds(r.removeFromLeft(140));
    r.removeFromLeft(16);
    m_state.setBounds(r);
    m_position.setBounds(row(22));
    m_bpm.setBounds(row(22));
    m_confidence.setBounds(row(22).removeFromLeft(240));
    a.removeFromTop(6);
    m_phase.setBounds(row(22));
    m_notes.setBounds(row(22));
    m_warnings.setBounds(a);
}
}
