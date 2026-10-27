#include "ui/SettingsComponent.h"

#include "ui/AppLookAndFeel.h"

namespace pianoassist::ui
{
namespace
{
juce::String toJuce (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }

void heading (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (17.0f, juce::Font::bold));
}

void note (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f));
    label.setJustificationType (juce::Justification::topLeft);
    label.setMinimumHorizontalScale (1.0f);
}

/** Lays out rows top to bottom with a fixed gap. */
struct Rows
{
    juce::Rectangle<int> area;
    juce::Rectangle<int> take (int height, int gapAfter = 8)
    {
        auto row = area.removeFromTop (height);
        area.removeFromTop (gapAfter);
        return row;
    }
};
} // namespace

/** A page: a scrollable column of controls with muted notes. */
class SettingsComponent::Page : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

protected:
    void addNote (juce::Label& label, const juce::String& text)
    {
        note (label, text);
        notes_.push_back (&label);
        addAndMakeVisible (label);
    }
    void addHeading (juce::Label& label, const juce::String& text)
    {
        heading (label, text);
        addAndMakeVisible (label);
    }

public:
    void recolour()
    {
        for (auto* label : notes_)
            label->setColour (juce::Label::textColourId, theme::mutedText);
    }

private:
    std::vector<juce::Label*> notes_;
};

// --- MIDI and audio ---------------------------------------------------------------------

class SettingsComponent::MidiAudioPage final : public Page
{
public:
    MidiAudioPage (Intents& intents, juce::AudioDeviceManager& devices)
        : intents_ (intents),
          audio_ (devices, 0, 0, 1, 2, false, false, true, false)
    {
        addHeading (midiHeading_, TRANS ("Piano (MIDI input)"));
        input_.setTitle (TRANS ("MIDI input device"));
        input_.setTextWhenNothingSelected (TRANS ("Choose your piano"));
        input_.setTextWhenNoChoicesAvailable (TRANS ("No MIDI inputs found"));
        input_.onChange = [this] {
            const auto index = input_.getSelectedItemIndex();
            if (index >= 0 && static_cast<std::size_t> (index) < ids_.size() && intents_.selectInput)
                intents_.selectInput (ids_[static_cast<std::size_t> (index)]);
        };
        addAndMakeVisible (input_);
        rescan_.setButtonText (TRANS ("Rescan"));
        rescan_.onClick = [this] { if (intents_.rescanInputs) intents_.rescanInputs(); };
        addAndMakeVisible (rescan_);
        status_.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (status_);
        addNote (midiNote_, TRANS ("Connect the Casio with a USB cable to its USB TO HOST port. The piano is found again "
                                   "automatically after a reconnect. If another program (a DAW, FL Studio, Casio app) uses "
                                   "the piano, close it and press Rescan."));

        addHeading (latencyHeading_, TRANS ("Input delay"));
        latency_.setRange (-50.0, 150.0, 1.0);
        latency_.setTextValueSuffix (" ms");
        latency_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        latency_.setTitle (TRANS ("Input delay"));
        latency_.onDragEnd = [this] { if (intents_.inputOffsetChanged) intents_.inputOffsetChanged (latency_.getValue()); };
        addAndMakeVisible (latency_);
        addNote (latencyNote_, TRANS ("How much earlier your presses are counted, to make up for the delay of the piano, "
                                      "USB and sound output. Leave 0 unless presses on the beat are judged late; then try 20-60 ms."));

        addHeading (audioHeading_, TRANS ("Sound output"));
        audio_.setItemHeight (26);
        addAndMakeVisible (audio_);
        devices.addChangeListener (&listener_);
        listener_.onChange = [this] { if (intents_.audioDeviceChanged) intents_.audioDeviceChanged(); };
        devices_ = &devices;
    }

    ~MidiAudioPage() override { devices_->removeChangeListener (&listener_); }

    void setInput (const InputMonitorView& view)
    {
        std::vector<std::string> ids;
        for (const auto& d : view.devices)
            ids.push_back (d.identifier);
        if (ids != ids_)
        {
            ids_ = ids;
            input_.clear (juce::dontSendNotification);
            int id = 1;
            for (const auto& d : view.devices)
                input_.addItem (toJuce (d.name), id++);
        }
        int selected = -1;
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == view.selectedIdentifier)
                selected = static_cast<int> (i);
        input_.setSelectedItemIndex (selected, juce::dontSendNotification);
        status_.setText (toJuce (view.statusText), juce::dontSendNotification);
        status_.setColour (juce::Label::textColourId,
                           view.status == InputMonitorView::Status::connected ? theme::good : theme::warning);
    }

    void setValues (const Values& v) { latency_.setValue (v.inputOffsetMs, juce::dontSendNotification); }

    void resized() override
    {
        Rows r { getLocalBounds().reduced (20, 16) };
        midiHeading_.setBounds (r.take (26, 4));
        auto row = r.take (30);
        rescan_.setBounds (row.removeFromRight (100));
        row.removeFromRight (8);
        input_.setBounds (row);
        status_.setBounds (r.take (22, 2));
        midiNote_.setBounds (r.take (52, 14));
        latencyHeading_.setBounds (r.take (26, 4));
        latency_.setBounds (r.take (28, 2).withWidth (std::min (r.area.getWidth(), 420)));
        latencyNote_.setBounds (r.take (40, 14));
        audioHeading_.setBounds (r.take (26, 4));
        audio_.setBounds (r.area.withHeight (std::max (200, r.area.getHeight())));
    }

private:
    struct Listener final : juce::ChangeListener
    {
        std::function<void()> onChange;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onChange) onChange(); }
    };

    Intents& intents_;
    juce::Label midiHeading_, midiNote_, status_, latencyHeading_, latencyNote_, audioHeading_;
    juce::ComboBox input_;
    juce::TextButton rescan_;
    juce::Slider latency_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::AudioDeviceSelectorComponent audio_;
    Listener listener_;
    juce::AudioDeviceManager* devices_ = nullptr;
    std::vector<std::string> ids_;
};

// --- Sounds ------------------------------------------------------------------------------

class SettingsComponent::SoundsPage final : public Page
{
public:
    explicit SoundsPage (Intents& intents) : intents_ (intents)
    {
        addHeading (mainHeading_, TRANS ("Main sound"));
        sound_.setTitle (TRANS ("Main sound"));
        sound_.onChange = [this] {
            const int id = sound_.getSelectedId();
            if (id > 0 && id != view_.selected && intents_.soundChosen)
                intents_.soundChosen (id);
        };
        addAndMakeVisible (sound_);
        window_.setButtonText (TRANS ("Instrument window..."));
        window_.onClick = [this] { if (intents_.showInstrumentWindow) intents_.showInstrumentWindow(); };
        addAndMakeVisible (window_);
        addNote (mainNote_, TRANS ("Plays every track that has no sound of its own. Choose a sound per track in the side "
                                   "panel (Tracks and instruments). Tracks left on the main sound reach it on their own MIDI "
                                   "channel, so a multitimbral instrument can be set up per channel in its window."));

        addHeading (vstHeading_, TRANS ("VST3 instruments"));
        for (auto* b : { &scan_, &addFile_, &addFolder_, &rescanAll_, &soundFont_ })
            addAndMakeVisible (b);
        scan_.setButtonText (TRANS ("Scan for new"));
        scan_.onClick = [this] { if (intents_.scanPlugins) intents_.scanPlugins(); };
        addFile_.setButtonText (TRANS ("Add .vst3 file..."));
        addFile_.onClick = [this] { if (intents_.addPluginFile) intents_.addPluginFile(); };
        addFolder_.setButtonText (TRANS ("Add folder..."));
        addFolder_.onClick = [this] { if (intents_.addPluginFolder) intents_.addPluginFolder(); };
        rescanAll_.setButtonText (TRANS ("Scan everything again"));
        rescanAll_.setTooltip (TRANS ("Also retries plug-ins that were skipped before"));
        rescanAll_.onClick = [this] { if (intents_.rescanAllPlugins) intents_.rescanAllPlugins(); };
        soundFont_.setButtonText (TRANS ("Load SoundFont (.sf2)..."));
        soundFont_.onClick = [this] { if (intents_.loadSoundFont) intents_.loadSoundFont(); };
        progress_.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (progress_);
        addAndMakeVisible (list_);
        list_.setMultiLine (true);
        list_.setReadOnly (true);
        list_.setScrollbarsShown (true);
        list_.setTitle (TRANS ("Instruments, folders and problems"));
        addNote (vstNote_, TRANS ("Each plug-in is checked in a separate helper process, so a faulty one cannot close "
                                  "PianoAssist. 64-bit VST3 only. If an instrument stays silent, open its window: many "
                                  "(Kontakt, LABS...) need their sound library downloaded first. FL Studio's FLEX works only "
                                  "inside FL Studio; sending MIDI to FL Studio is planned as a later step."));
    }

    void setSound (const SoundView& v)
    {
        view_ = v;
        sound_.clear (juce::dontSendNotification);
        sound_.addItem (TRANS ("Built-in sound"), SoundView::builtIn);
        if (! v.soundFontName.empty())
            sound_.addItem ("SoundFont: " + toJuce (v.soundFontName), SoundView::soundFont);
        for (std::size_t i = 0; i < v.plugins.size(); ++i)
            sound_.addItem (toJuce (v.plugins[i]), SoundView::firstPlugin + static_cast<int> (i));
        sound_.setSelectedId (v.selected, juce::dontSendNotification);
        window_.setEnabled (v.canEdit);
        scan_.setEnabled (! v.scanning);
        rescanAll_.setEnabled (! v.scanning);
        progress_.setText (toJuce (! v.scanProgress.empty() ? v.scanProgress : v.status), juce::dontSendNotification);

        juce::String text;
        text << TRANS ("Instruments found") << " (" << static_cast<int> (v.plugins.size()) << "):\n";
        for (const auto& p : v.plugins)
            text << "  • " << toJuce (p) << "\n";
        if (v.plugins.empty())
            text << "  " << TRANS ("none yet: press \"Scan for new\" or add a .vst3 file") << "\n";
        text << "\n" << TRANS ("Folders scanned") << ":\n";
        for (std::size_t i = 0; i < v.folders.size(); ++i)
            text << "  " << (i < v.standardFolderCount ? TRANS ("(standard)") + " " : "[" + juce::String (static_cast<int> (i - v.standardFolderCount) + 1) + "] ")
                 << toJuce (v.folders[i]) << "\n";
        if (! v.problems.empty())
        {
            text << "\n" << TRANS ("Problems") << ":\n";
            for (const auto& p : v.problems)
                text << "  ! " << toJuce (p) << "\n";
        }
        list_.setText (text, false);
        removeFolder_.clear();
        for (std::size_t i = v.standardFolderCount; i < v.folders.size(); ++i)
        {
            auto* b = removeFolder_.add (new juce::TextButton (TRANS ("Remove folder") + " " + juce::String (static_cast<int> (i - v.standardFolderCount) + 1)));
            b->setTooltip (toJuce (v.folders[i]));
            b->onClick = [this, i] { if (intents_.removePluginFolder) intents_.removePluginFolder (i); };
            addAndMakeVisible (b);
        }
        resized();
    }

    void resized() override
    {
        Rows r { getLocalBounds().reduced (20, 16) };
        mainHeading_.setBounds (r.take (26, 4));
        auto row = r.take (30);
        window_.setBounds (row.removeFromRight (170));
        row.removeFromRight (8);
        sound_.setBounds (row);
        mainNote_.setBounds (r.take (54, 12));
        vstHeading_.setBounds (r.take (26, 4));
        row = r.take (30);
        const int w = (row.getWidth() - 16) / 3;
        scan_.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
        addFile_.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
        addFolder_.setBounds (row);
        row = r.take (30);
        rescanAll_.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
        soundFont_.setBounds (row.removeFromLeft (w * 2 + 8));
        progress_.setBounds (r.take (22, 4));
        if (! removeFolder_.isEmpty())
        {
            row = r.take (28);
            for (auto* b : removeFolder_)
            {
                b->setBounds (row.removeFromLeft (150));
                row.removeFromLeft (6);
            }
        }
        vstNote_.setBounds (r.area.removeFromBottom (70));
        r.area.removeFromBottom (8);
        list_.setBounds (r.area);
    }

private:
    Intents& intents_;
    SoundView view_;
    juce::Label mainHeading_, mainNote_, vstHeading_, vstNote_, progress_;
    juce::ComboBox sound_;
    juce::TextButton window_, scan_, addFile_, addFolder_, rescanAll_, soundFont_;
    juce::OwnedArray<juce::TextButton> removeFolder_;
    juce::TextEditor list_;
};

// --- Practice and tolerances --------------------------------------------------------------

class SettingsComponent::PracticePage final : public Page
{
public:
    explicit PracticePage (Intents& intents) : intents_ (intents)
    {
        addHeading (toleranceHeading_, TRANS ("Timing tolerance"));
        const juce::String labels[] = { TRANS ("Strict: 120 ms early, 180 ms late"),
                                        TRANS ("Normal: 250 ms early, 300 ms late (recommended)"),
                                        TRANS ("Easy: 400 ms early, 600 ms late") };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = tolerance_[static_cast<std::size_t> (i)];
            b.setButtonText (labels[i]);
            b.setRadioGroupId (2001);
            b.onClick = [this, i] {
                if (tolerance_[static_cast<std::size_t> (i)].getToggleState() && intents_.toleranceChanged)
                    intents_.toleranceChanged (i);
            };
            addAndMakeVisible (b);
        }
        addNote (toleranceNote_, TRANS ("A note played this much before or after its time still counts, and the song keeps "
                                        "going. Each song remembers its own choice."));
        addHeading (waitHeading_, TRANS ("Waiting"));
        wait_.setButtonText (TRANS ("Wait for my notes when a new song is opened"));
        wait_.onClick = [this] { if (intents_.waitForNotesChanged) intents_.waitForNotesChanged (wait_.getToggleState()); };
        addAndMakeVisible (wait_);
        addHeading (handsHeading_, TRANS ("Hands"));
        addNote (handsNote_, TRANS ("Which part is the right or left hand is guessed from the file. Parts marked ? were guessed "
                                    "from pitch. Change them in the side panel under Tracks and instruments; each song "
                                    "remembers its own choices."));
        addHeading (libraryHeading_, TRANS ("Song library"));
        library_.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (library_);
        changeLibrary_.setButtonText (TRANS ("Change..."));
        changeLibrary_.onClick = [this] { if (intents_.chooseLibraryFolder) intents_.chooseLibraryFolder(); };
        openLibrary_.setButtonText (TRANS ("Open"));
        openLibrary_.onClick = [this] { if (intents_.openLibraryFolder) intents_.openLibraryFolder(); };
        addAndMakeVisible (changeLibrary_);
        addAndMakeVisible (openLibrary_);
        addNote (libraryNote_, TRANS ("Opening a MIDI file adds a copy to the library: a folder per song with the MIDI, its "
                                      "settings and its instruments' states. Your original file is never changed."));
    }

    void setValues (const Values& v)
    {
        for (int i = 0; i < 3; ++i)
            tolerance_[static_cast<std::size_t> (i)].setToggleState (v.tolerance == i, juce::dontSendNotification);
        wait_.setToggleState (v.waitForNotes, juce::dontSendNotification);
        library_.setText (toJuce (v.libraryFolder), juce::dontSendNotification);
        library_.setTooltip (toJuce (v.libraryFolder));
    }

    void resized() override
    {
        Rows r { getLocalBounds().reduced (20, 16) };
        toleranceHeading_.setBounds (r.take (26, 4));
        for (auto& b : tolerance_)
            b.setBounds (r.take (26, 2));
        toleranceNote_.setBounds (r.take (40, 12));
        waitHeading_.setBounds (r.take (26, 4));
        wait_.setBounds (r.take (26, 12));
        handsHeading_.setBounds (r.take (26, 4));
        handsNote_.setBounds (r.take (54, 12));
        libraryHeading_.setBounds (r.take (26, 4));
        auto row = r.take (30);
        openLibrary_.setBounds (row.removeFromRight (80));
        row.removeFromRight (8);
        changeLibrary_.setBounds (row.removeFromRight (100));
        row.removeFromRight (8);
        library_.setBounds (row);
        libraryNote_.setBounds (r.take (40));
    }

private:
    Intents& intents_;
    juce::Label toleranceHeading_, toleranceNote_, waitHeading_, handsHeading_, handsNote_, libraryHeading_, library_, libraryNote_;
    std::array<juce::ToggleButton, 3> tolerance_;
    juce::ToggleButton wait_;
    juce::TextButton changeLibrary_, openLibrary_;
};

// --- Appearance ----------------------------------------------------------------------------

class SettingsComponent::AppearancePage final : public Page
{
public:
    explicit AppearancePage (Intents& intents) : intents_ (intents)
    {
        addHeading (themeHeading_, TRANS ("Theme"));
        theme_.addItem (TRANS ("Dark"), 1);
        theme_.addItem (TRANS ("Light"), 2);
        theme_.onChange = [this] { if (intents_.themeChanged) intents_.themeChanged (theme_.getSelectedId() == 2); };
        addAndMakeVisible (theme_);
        addHeading (scaleHeading_, TRANS ("Interface size"));
        for (const int percent : { 85, 100, 115, 125, 150, 175, 200 })
            scale_.addItem (juce::String (percent) + "%", percent);
        scale_.onChange = [this] { if (intents_.uiScaleChanged) intents_.uiScaleChanged (scale_.getSelectedId() / 100.0); };
        addAndMakeVisible (scale_);
        addHeading (languageHeading_, TRANS ("Language"));
        language_.addItem (juce::String::fromUTF8 ("Русский"), 1);
        language_.addItem ("English", 2);
        language_.onChange = [this] {
            if (intents_.languageChanged)
                intents_.languageChanged (language_.getSelectedId() == 2 ? "en" : "ru");
        };
        addAndMakeVisible (language_);
        addNote (languageNote_, TRANS ("The language changes after PianoAssist is restarted."));
        addHeading (zoomHeading_, TRANS ("Piano roll zoom"));
        zoom_.setRange (40.0, 600.0, 1.0);
        zoom_.setSkewFactorFromMidPoint (150.0);
        zoom_.setTextValueSuffix (" px/s");
        zoom_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
        zoom_.onDragEnd = [this] { if (intents_.zoomChanged) intents_.zoomChanged (zoom_.getValue()); };
        addAndMakeVisible (zoom_);
        addNote (zoomNote_, TRANS ("Ctrl + mouse wheel over the roll zooms too."));
    }

    void setValues (const Values& v)
    {
        theme_.setSelectedId (v.lightTheme ? 2 : 1, juce::dontSendNotification);
        const int percent = juce::roundToInt (v.uiScale * 100.0);
        if (scale_.indexOfItemId (percent) < 0)
            scale_.addItem (juce::String (percent) + "%", percent);
        scale_.setSelectedId (percent, juce::dontSendNotification);
        language_.setSelectedId (v.language == "en" ? 2 : 1, juce::dontSendNotification);
        zoom_.setValue (v.pixelsPerSecond, juce::dontSendNotification);
    }

    void resized() override
    {
        Rows r { getLocalBounds().reduced (20, 16) };
        const int w = std::min (r.area.getWidth(), 320);
        themeHeading_.setBounds (r.take (26, 4));
        theme_.setBounds (r.take (30, 14).withWidth (w));
        scaleHeading_.setBounds (r.take (26, 4));
        scale_.setBounds (r.take (30, 14).withWidth (w));
        languageHeading_.setBounds (r.take (26, 4));
        language_.setBounds (r.take (30, 2).withWidth (w));
        languageNote_.setBounds (r.take (22, 14));
        zoomHeading_.setBounds (r.take (26, 4));
        zoom_.setBounds (r.take (28, 2).withWidth (std::min (r.area.getWidth(), 460)));
        zoomNote_.setBounds (r.take (22));
    }

private:
    Intents& intents_;
    juce::Label themeHeading_, scaleHeading_, languageHeading_, languageNote_, zoomHeading_, zoomNote_;
    juce::ComboBox theme_, scale_, language_;
    juce::Slider zoom_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
};

// --- Diagnostics ---------------------------------------------------------------------------

class SettingsComponent::DiagnosticsPage final : public Page
{
public:
    explicit DiagnosticsPage (Intents& intents) : intents_ (intents)
    {
        addHeading (monitorHeading_, TRANS ("Live MIDI input"));
        const juce::String captions[] = { TRANS ("Last note"), TRANS ("Held keys"), TRANS ("Sustain pedal"), TRANS ("Events per second"),
                                          TRANS ("Notes since start") };
        for (std::size_t i = 0; i < captions_.size(); ++i)
        {
            addNote (captions_[i], captions[i]);
            values_[i].setFont (juce::FontOptions (18.0f, juce::Font::bold));
            addAndMakeVisible (values_[i]);
        }
        addHeading (systemHeading_, TRANS ("This installation"));
        info_.setMultiLine (true);
        info_.setReadOnly (true);
        addAndMakeVisible (info_);
        for (auto* b : { &copy_, &save_, &logs_, &data_, &updates_ })
            addAndMakeVisible (b);
        copy_.setButtonText (TRANS ("Copy report"));
        copy_.onClick = [this] { if (intents_.copyDiagnostics) intents_.copyDiagnostics(); };
        save_.setButtonText (TRANS ("Save report..."));
        save_.onClick = [this] { if (intents_.saveDiagnostics) intents_.saveDiagnostics(); };
        logs_.setButtonText (TRANS ("Open log folder"));
        logs_.onClick = [this] { if (intents_.openLogFolder) intents_.openLogFolder(); };
        data_.setButtonText (TRANS ("Open data folder"));
        data_.onClick = [this] { if (intents_.openDataFolder) intents_.openDataFolder(); };
        updates_.setButtonText (TRANS ("Check for updates..."));
        updates_.onClick = [this] { if (intents_.checkForUpdates) intents_.checkForUpdates(); };
        testBuilds_.setButtonText (TRANS ("Also offer test builds when checking for updates"));
        testBuilds_.onClick = [this] { if (intents_.includeTestBuildsChanged) intents_.includeTestBuildsChanged (testBuilds_.getToggleState()); };
        addAndMakeVisible (testBuilds_);
        addNote (privacy_, TRANS ("Reports stay on this computer until you share them; your user folder name is replaced. "
                                  "PianoAssist sends nothing by itself. Checking for updates reads the public release list on GitHub."));
    }

    void setInput (const InputMonitorView& v)
    {
        values_[0].setText (v.lastNote.empty() ? "-" : toJuce (v.lastNote), juce::dontSendNotification);
        values_[1].setText (v.heldNotes.empty() ? "-" : toJuce (v.heldNotes), juce::dontSendNotification);
        values_[2].setText (v.sustainDown ? TRANS ("Down") : TRANS ("Up"), juce::dontSendNotification);
        values_[3].setText (juce::String (v.eventsPerSecond, 0), juce::dontSendNotification);
        values_[4].setText (juce::String (static_cast<juce::int64> (v.totalNoteOns)), juce::dontSendNotification);
    }

    void setValues (const Values& v)
    {
        juce::String text;
        text << "PianoAssist " << toJuce (v.version) << (v.portable ? " (portable)" : "") << "\n"
             << TRANS ("Sound output") << ": " << toJuce (v.audioDescription) << "\n"
             << TRANS ("Data folder") << ": " << toJuce (v.dataFolder) << "\n"
             << TRANS ("Song library") << ": " << toJuce (v.libraryFolder);
        info_.setText (text, false);
        testBuilds_.setToggleState (v.includeTestBuilds, juce::dontSendNotification);
    }

    void resized() override
    {
        Rows r { getLocalBounds().reduced (20, 16) };
        monitorHeading_.setBounds (r.take (26, 4));
        auto captions = r.take (18, 0);
        auto values = r.take (28, 14);
        const int w = captions.getWidth() / static_cast<int> (captions_.size());
        for (std::size_t i = 0; i < captions_.size(); ++i)
        {
            captions_[i].setBounds (captions.removeFromLeft (w));
            values_[i].setBounds (values.removeFromLeft (w));
        }
        systemHeading_.setBounds (r.take (26, 4));
        info_.setBounds (r.take (86, 10));
        auto row = r.take (30);
        const int bw = (row.getWidth() - 24) / 4;
        for (auto* b : { &copy_, &save_, &logs_, &data_ })
        {
            b->setBounds (row.removeFromLeft (bw));
            row.removeFromLeft (8);
        }
        row = r.take (30);
        updates_.setBounds (row.removeFromLeft (bw * 2));
        testBuilds_.setBounds (r.take (26, 8));
        privacy_.setBounds (r.take (54));
    }

private:
    Intents& intents_;
    juce::Label monitorHeading_, systemHeading_, privacy_;
    std::array<juce::Label, 5> captions_, values_;
    juce::TextEditor info_;
    juce::TextButton copy_, save_, logs_, data_, updates_;
    juce::ToggleButton testBuilds_;
};

// --- the window content -----------------------------------------------------------------------

SettingsComponent::SettingsComponent (Intents intents, juce::AudioDeviceManager& devices)
    : intents_ (std::move (intents))
{
    midiAudio_ = std::make_unique<MidiAudioPage> (intents_, devices);
    sounds_ = std::make_unique<SoundsPage> (intents_);
    practice_ = std::make_unique<PracticePage> (intents_);
    appearance_ = std::make_unique<AppearancePage> (intents_);
    diagnostics_ = std::make_unique<DiagnosticsPage> (intents_);
    pages_ = { midiAudio_.get(), sounds_.get(), practice_.get(), appearance_.get(), diagnostics_.get() };
    const juce::String names[] = { TRANS ("MIDI and audio"), TRANS ("Sounds"), TRANS ("Practice and timing"), TRANS ("Appearance"),
                                   TRANS ("Diagnostics") };
    for (int i = 0; i < 5; ++i)
    {
        auto& button = pageButtons_[static_cast<std::size_t> (i)];
        button.setButtonText (names[i]);
        button.setClickingTogglesState (false);
        button.setRadioGroupId (3001);
        button.onClick = [this, i] { showPage (static_cast<SettingsPage> (i)); };
        addAndMakeVisible (button);
        addChildComponent (pages_[static_cast<std::size_t> (i)]);
    }
    showPage (SettingsPage::midiAndAudio);
    lookAndFeelChanged();
    setSize (920, 660);
}

SettingsComponent::~SettingsComponent() = default;

void SettingsComponent::showPage (SettingsPage page)
{
    current_ = std::clamp (static_cast<int> (page), 0, 4);
    for (int i = 0; i < 5; ++i)
    {
        pages_[static_cast<std::size_t> (i)]->setVisible (i == current_);
        pageButtons_[static_cast<std::size_t> (i)].setToggleState (i == current_, juce::dontSendNotification);
    }
}

void SettingsComponent::setValues (const Values& values)
{
    midiAudio_->setValues (values);
    practice_->setValues (values);
    appearance_->setValues (values);
    diagnostics_->setValues (values);
}

void SettingsComponent::setInputMonitor (const InputMonitorView& view)
{
    midiAudio_->setInput (view);
    diagnostics_->setInput (view);
}

void SettingsComponent::setSound (const SoundView& view)
{
    sounds_->setSound (view);
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::header);
    g.setColour (theme::panelEdge);
    g.fillRect (sideWidth, 0, 1, getHeight());
}

void SettingsComponent::resized()
{
    auto area = getLocalBounds();
    auto side = area.removeFromLeft (sideWidth).reduced (10, 14);
    for (auto& button : pageButtons_)
    {
        button.setBounds (side.removeFromTop (36));
        side.removeFromTop (4);
    }
    area.removeFromLeft (1);
    for (auto* page : pages_)
        page->setBounds (area);
}

void SettingsComponent::lookAndFeelChanged()
{
    for (auto* page : pages_)
        if (page != nullptr)
        {
            page->recolour();
            page->repaint();
        }
    for (auto& button : pageButtons_)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::header);
        button.setColour (juce::TextButton::buttonOnColourId, theme::control);
        button.setColour (juce::TextButton::textColourOffId, theme::mutedText);
        button.setColour (juce::TextButton::textColourOnId, theme::text);
    }
    repaint();
}

} // namespace pianoassist::ui
