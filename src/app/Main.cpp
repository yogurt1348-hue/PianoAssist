#include "app/AppPaths.h"
#include "app/AudioService.h"
#include "app/InstrumentService.h"
#include "app/LessonController.h"
#include "app/MidiInputService.h"
#include "app/SongLoader.h"
#include "core/Utf8Path.h"
#include "core/BinaryFile.h"
#include "diagnostics/DiagnosticReport.h"
#include "diagnostics/RotatingLog.h"
#include "diagnostics/SessionReport.h"
#include "settings/Settings.h"
#include "ui/AppLookAndFeel.h"
#include "ui/MainComponent.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <exception>
#include <memory>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;

lesson::PracticeMode toPracticeMode (int index)
{
    switch (index)
    {
        case 1: return lesson::PracticeMode::rightHand;
        case 2: return lesson::PracticeMode::leftHand;
        default: return lesson::PracticeMode::bothHands;
    }
}

// Used only by the crash/terminate handlers, which cannot capture state.
diagnostics::RotatingLog* crashLog = nullptr;

void logCrash (const char* what)
{
    if (crashLog == nullptr)
        return;
    crashLog->write (LogLevel::error, "crash", what,
                     { { "stack", juce::SystemStats::getStackBacktrace().toStdString() } });
}

void installCrashLogging (diagnostics::RotatingLog& log)
{
    crashLog = &log;
    juce::SystemStats::setApplicationCrashHandler ([] (void*) { logCrash ("Application crashed"); });
    std::set_terminate ([] {
        std::string what = "std::terminate called";
        if (const auto current = std::current_exception())
        {
            try { std::rethrow_exception (current); }
            catch (const std::exception& e) { what += std::string (": ") + e.what(); }
            catch (...) { what += ": unknown exception"; }
        }
        logCrash (what.c_str());
        std::abort();
    });
}

void logEnvironment (diagnostics::RotatingLog& log)
{
#if JUCE_DEBUG
    const char* buildType = "Debug";
#else
    const char* buildType = "Release";
#endif
    std::string displays;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        displays += (displays.empty() ? "" : "; ") + std::to_string (display.totalArea.getWidth()) + "x"
                    + std::to_string (display.totalArea.getHeight()) + " @"
                    + juce::String (display.scale * 100.0, 0).toStdString() + "%";

    log.write (LogLevel::info, "startup", "Environment",
               { { "build", buildType },
                 { "juce", juce::SystemStats::getJUCEVersion().toStdString() },
                 { "os", juce::SystemStats::getOperatingSystemName().toStdString() },
                 { "os64bit", juce::SystemStats::isOperatingSystem64Bit() ? "yes" : "no" },
                 { "cpu", juce::SystemStats::getCpuModel().toStdString() },
                 { "cpuCores", std::to_string (juce::SystemStats::getNumCpus()) },
                 { "memoryMB", std::to_string (juce::SystemStats::getMemorySizeInMegabytes()) },
                 { "language", juce::SystemStats::getUserLanguage().toStdString() },
                 { "displays", displays } });

    std::string outputs;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        outputs += (outputs.empty() ? "" : "; ") + device.name.toStdString();
    log.write (LogLevel::info, "startup", "MIDI outputs (for diagnosis only)",
               { { "devices", outputs.empty() ? "(none)" : outputs } });
}

settings::Settings loadSettings (diagnostics::RotatingLog& log)
{
    const auto path = settingsFile();
    auto result = settings::loadFromFile (path);
    for (const auto& warning : result.warnings)
        log.write (LogLevel::warning, "settings", warning);

    if (result.status == settings::LoadStatus::recovered)
    {
        // Keep the unusable file for diagnosis instead of silently losing it.
        std::error_code ec;
        auto backup = path;
        backup += ".bad";
        std::filesystem::copy_file (path, backup, std::filesystem::copy_options::overwrite_existing, ec);
        log.write (ec ? LogLevel::error : LogLevel::info, "settings",
                   ec ? "Could not back up unusable settings" : "Unusable settings backed up as settings.json.bad");
    }
    if (result.status != settings::LoadStatus::loaded)
        if (const auto error = settings::saveToFile (path, result.settings); ! error.empty())
            log.write (LogLevel::error, "settings", error);

    const char* status = result.status == settings::LoadStatus::loaded      ? "loaded"
                         : result.status == settings::LoadStatus::defaulted ? "first run, defaults written"
                                                                              : "recovered with defaults";
    log.write (LogLevel::info, "settings", "Settings ready",
               { { "status", status },
                 { "storedSchemaVersion", std::to_string (result.storedSchemaVersion) },
                 { "practiceProfile", result.settings.practiceProfile.name },
                 { "midiInput", result.settings.midiInput.lastSeenName.empty() ? "(none)"
                                                                               : result.settings.midiInput.lastSeenName } });
    return result.settings;
}

diagnostics::SessionInfo describeSession (const settings::Settings& s)
{
    diagnostics::SessionInfo info;
    info.appVersion = PIANOASSIST_VERSION;
    info.osName = juce::SystemStats::getOperatingSystemName().toStdString();
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        info.midiInputNames.push_back (device.name.toStdString());
    info.selectedMidiInput = s.midiInput.lastSeenName;
    return info;
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::Component* content, diagnostics::RotatingLog& log)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (
                              juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons),
          log_ (log)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (720, 560, 10000, 10000);
        // Fit the screen at any Windows scaling (200% on a 1080p laptop
        // leaves about 960 x 520 logical pixels).
        auto size = juce::Rectangle<int> (getWidth(), getHeight());
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            size = size.getIntersection (display->userArea.reduced (20).withZeroOrigin());
        centreWithSize (std::max (size.getWidth(), 720), std::max (size.getHeight(), 560));
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        log_.write (LogLevel::info, "ui", "Window close requested");
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    diagnostics::RotatingLog& log_;
};
} // namespace

class PianoAssistApplication final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        diagnostics::RotatingLog::Options options;
        options.directory = logDirectory();
        options.maxFileBytes = 5 * 1024 * 1024;
        options.maxArchivedFiles = 5;
        log_ = std::make_unique<diagnostics::RotatingLog> (options);
        installCrashLogging (*log_);

        log_->write (LogLevel::info, "startup", "Startup begin", { { "appVersion", PIANOASSIST_VERSION } });
        logEnvironment (*log_);
        settings_ = loadSettings (*log_);
        diagnostics::logSessionStart (*log_, describeSession (settings_));

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);
        midi_ = std::make_unique<MidiInputService> (
            *log_, MidiInputService::Remembered { settings_.midiInput.deviceId, settings_.midiInput.lastSeenName });

        auto* content = new ui::MainComponent ({
            [this] (const std::string& id) { midi_->selectByUser (id); },
            [this] {
                log_->write (LogLevel::info, "ui", "User pressed Rescan");
                midi_->rescan ("user rescan");
            },
            [this] {
                log_->write (LogLevel::info, "ui", "User opened the log folder");
                juce::File (juce::String::fromUTF8 (pathToUtf8 (logDirectory()).c_str())).startAsProcess();
            },
            [this] { copyDiagnostics(); },
            [this] { saveDiagnostics(); },
            [this] { chooseMidiFile(); },
            [this] (const juce::File& file) {
                log_->write (LogLevel::info, "ui", "User dropped a file on the window");
                openSong (file);
            },
            [this] (std::size_t part, bool muted) {
                if (song_ == nullptr || part >= song_->parts.size())
                    return;
                mutedParts_.resize (song_->parts.size(), false);
                mutedParts_[part] = muted;
                log_->write (LogLevel::info, "ui", muted ? "Track muted" : "Track unmuted", { { "part", std::to_string (part + 1) } });
                lesson_->setMutedParts (mutedParts_);
            },
            [this] (double pixelsPerSecond) { settings_.visual.pixelsPerSecond = pixelsPerSecond; },
            [this] { lesson_->playPause(); },
            [this] { lesson_->stop(); },
            [this] { lesson_->silence(); },
            [this] (double seconds) { lesson_->seek (seconds); },
            [this] (float gain) { audio_->setGain (gain); },
            [this] (bool practising) { lesson_->setPractising (practising); },
            [this] (int mode) {
                settings_.practice.mode = toPracticeMode (mode);
                lesson_->setMode (settings_.practice.mode);
                saveSettings();
            },
            [this] (int tolerance) {
                settings_.practiceProfile = lesson::profileFor (static_cast<lesson::Tolerance> (std::clamp (tolerance, 0, 2)));
                lesson_->setProfile (settings_.practiceProfile);
                saveSettings();
            },
            [this] (std::uint8_t pitch, bool down) { lesson_->previewKey (pitch, down); },
            [this] (bool play) {
                settings_.practice.accompaniment.playOtherPianoPart = play;
                lesson_->setAccompaniment (settings_.practice.accompaniment);
                saveSettings();
            },
            [this] (bool play) {
                settings_.practice.accompaniment.playBacking = play;
                lesson_->setAccompaniment (settings_.practice.accompaniment);
                saveSettings();
            },
            [this] (std::size_t part, int assignment) { reassignPart (part, assignment); },
            [this] (int id) {
                if (id == ui::SoundView::builtIn)
                    instruments_->chooseBuiltIn();
                else if (id >= ui::SoundView::firstPlugin)
                    instruments_->choosePlugin (static_cast<std::size_t> (id - ui::SoundView::firstPlugin));
                else if (id == ui::SoundView::soundFont && ! settings_.sound.soundFontPath.empty())
                    instruments_->chooseSoundFont (pathFromUtf8 (settings_.sound.soundFontPath));
            },
            [this] { chooseSoundFont(); },
            [this] { choosePluginFile(); },
            [this] { instruments_->startScan(); },
            [this] { instruments_->showEditor(); },
        });
        content_ = content;
        content->setPixelsPerSecond (settings_.visual.pixelsPerSecond);
        midi_->setViewListener ([this, content] (const ui::InputMonitorView& view) {
            lastInputView_ = view;
            content->setInputMonitor (view);
        });
        midi_->setChoiceListener ([this] (const midi_io::MidiDeviceInfo& device) { rememberInput (device); });

        window_ = std::make_unique<MainWindow> (getApplicationName() + " " + getApplicationVersion(), content, *log_);
        log_->write (LogLevel::info, "startup", "Main window shown");

        midi_->start();

        audio_ = std::make_unique<AudioService> (*log_);
        audioError_ = audio_->start();
        // Profiles are chosen as presets; an older or edited file maps to one.
        settings_.practiceProfile = lesson::profileFor (lesson::toleranceOf (settings_.practiceProfile));
        lesson_ = std::make_unique<LessonController> (*log_, *audio_, midi_->lessonQueue(), settings_.practiceProfile,
                                                      settings_.calibration.inputOffsetMs);
        instruments_ = std::make_unique<InstrumentService> (*log_, *audio_, settings_.sound, [this] { saveSettings(); });
        instruments_->onChange = [this] { content_->setSound (instruments_->view()); };
        instruments_->restore();
        content_->setSound (instruments_->view());
        lesson_->setMode (settings_.practice.mode);
        lesson_->setAccompaniment (settings_.practice.accompaniment);
        lesson_->start();
        content->setVolume (0.8f);
        startTimerHz (30);
        log_->write (LogLevel::info, "startup", "Startup complete");

        const auto argument = commandLine.unquoted().trim();
        if (argument.isNotEmpty() && juce::File::isAbsolutePath (argument) && juce::File (argument).existsAsFile())
        {
            log_->write (LogLevel::info, "startup", "Opening file from the command line");
            openSong (juce::File (argument));
        }
    }

    void shutdown() override
    {
        if (log_ != nullptr)
            log_->write (LogLevel::info, "session", "Shutdown begin");
        if (log_ != nullptr)
            if (const auto error = settings::saveToFile (settingsFile(), settings_); ! error.empty())
                log_->write (LogLevel::error, "settings", error);
        stopTimer();
        chooser_.reset();
        lesson_.reset();
        if (instruments_ != nullptr)
            instruments_->saveState();
        instruments_.reset();
        audio_.reset();
        midi_.reset();
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        if (log_ != nullptr)
            log_->write (LogLevel::info, "session", "Session ended");
        crashLog = nullptr;
        log_.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void unhandledException (const std::exception* e, const juce::String& sourceFile, int line) override
    {
        if (log_ != nullptr)
            log_->write (LogLevel::error, "crash", "Unhandled exception",
                         { { "what", e != nullptr ? e->what() : "unknown" },
                           { "where", juce::File (sourceFile).getFileName().toStdString() + ":" + std::to_string (line) } });
    }

private:
    void chooseMidiFile()
    {
        log_->write (LogLevel::info, "ui", "User pressed Open MIDI file");
        chooser_ = std::make_unique<juce::FileChooser> ("Open a MIDI file", lastFolder_, "*.mid;*.midi;*.kar");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file == juce::File())
                                   {
                                       log_->write (LogLevel::info, "ui", "Open MIDI file cancelled");
                                       return;
                                   }
                                   openSong (file);
                               });
    }

    void openSong (const juce::File& file)
    {
        lastFolder_ = file.getParentDirectory();
        const auto path = pathFromUtf8 (file.getFullPathName().toStdString());
        auto song = loadSong (path, *log_);
        if (! song)
        {
            content_->setSongError ("Could not open " + file.getFileName().toStdString() + ": " + song.error());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not open the MIDI file",
                                                    file.getFileName() + "\n\n" + juce::String::fromUTF8 (song.error().c_str()));
            return;
        }
        song_ = std::move (song).value();
        mutedParts_.clear(); // the controller clears its own copy on a new song
        songTitle_ = file.getFileNameWithoutExtension().toStdString();
        lesson_->loadSong (song_);
        content_->setSong (std::make_shared<const ui::PianoRollView> (makePianoRollView (*song_, songTitle_)));
        log_->write (LogLevel::info, "song", "Song shown in piano roll");
    }

    std::string diagnosticReport (std::size_t logLines)
    {
        diagnostics::DiagnosticSummary s;
        s.appVersion = PIANOASSIST_VERSION;
#if JUCE_DEBUG
        s.build = "Debug";
#else
        s.build = "Release";
#endif
        s.os = juce::SystemStats::getOperatingSystemName().toStdString();
        s.audio = audioError_.empty() ? audio_->deviceDescription() : audioError_;
        for (const auto& device : lastInputView_.devices)
        {
            s.midiInputs.push_back (device.name);
            if (device.identifier == lastInputView_.selectedIdentifier)
                s.selectedMidiInput = device.name;
        }
        s.song = song_ != nullptr ? songTitle_ : std::string();
        const auto practice = lesson_->snapshot();
        s.practice = std::string (practice.practising ? "on, " : "off, ") + std::string (lesson::toString (practice.mode))
                     + ", " + settings_.practiceProfile.name + " timing"
                     + (practice.accompaniment.playOtherPianoPart ? "" : ", other hand muted")
                     + (practice.accompaniment.playBacking ? "" : ", backing muted");

        auto lines = diagnostics::tailLines (log_->currentFile(), logLines);
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
        const auto user = juce::SystemStats::getLogonName().toStdString();
        auto text = diagnostics::buildReport (s, lines);
        return diagnostics::redact (std::move (text), { { home, "%USERPROFILE%" }, { user, "<user>" } });
    }

    void copyDiagnostics()
    {
        juce::SystemClipboard::copyTextToClipboard (juce::String::fromUTF8 (diagnosticReport (300).c_str()));
        log_->write (LogLevel::info, "ui", "Diagnostics copied to the clipboard");
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Diagnostics copied",
                                                "A report with the last 300 log lines is on the clipboard. "
                                                "Your user folder name is replaced. Paste it where you need it.");
    }

    void saveDiagnostics()
    {
        log_->write (LogLevel::info, "ui", "User pressed Save diagnostics");
        const auto name = "PianoAssist-diagnostics-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".txt";
        chooser_ = std::make_unique<juce::FileChooser> (
            "Save diagnostics", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.txt");
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file == juce::File())
                                       return;
                                   const auto report = diagnosticReport (20'000);
                                   const auto path = pathFromUtf8 (file.getFullPathName().toStdString());
                                   const auto written = writeFileAtomically (
                                       path, std::vector<std::uint8_t> (report.begin(), report.end()));
                                   if (! written)
                                       log_->write (LogLevel::error, "ui", "Saving diagnostics failed", { { "error", written.error() } });
                                   else
                                       log_->write (LogLevel::info, "ui", "Diagnostics saved", { { "file", fileNameForLog (path) } });
                               });
    }

    void chooseSoundFont()
    {
        log_->write (LogLevel::info, "ui", "User pressed Load SoundFont");
        chooser_ = std::make_unique<juce::FileChooser> ("Load a SoundFont", lastFolder_, "*.sf2");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file != juce::File())
                                       instruments_->chooseSoundFont (pathFromUtf8 (file.getFullPathName().toStdString()));
                               });
    }

    void choosePluginFile()
    {
        log_->write (LogLevel::info, "ui", "User pressed Add VST3 instrument file");
        const auto folder = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                                .getChildFile ("Common Files").getChildFile ("VST3");
        chooser_ = std::make_unique<juce::FileChooser> ("Add a VST3 instrument", folder, "*.vst3");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file != juce::File())
                                       instruments_->addPluginFile (file);
                               });
    }

    void reassignPart (std::size_t part, int assignment)
    {
        if (song_ == nullptr || assignment < 0 || assignment > 3)
            return;
        const auto to = static_cast<timeline::Part::Assignment> (assignment);
        auto mapped = timeline::reassignPart (*song_, part, to);
        if (! mapped)
        {
            log_->write (LogLevel::error, "song", "Part change failed", { { "error", mapped.error() } });
            return;
        }
        log_->write (LogLevel::info, "song", "User changed a part",
                     { { "part", std::to_string (part + 1) },
                       { "from", timeline::toString (song_->parts[part].assignment) },
                       { "to", timeline::toString (to) } });
        song_ = std::make_shared<const timeline::Song> (std::move (mapped).value());
        lesson_->loadSong (song_, true);
        content_->setSong (std::make_shared<const ui::PianoRollView> (makePianoRollView (*song_, songTitle_)));
    }

    void timerCallback() override
    {
        audio_->collectGarbage();
        const auto snap = audio_->snapshot();

        ui::TransportView view;
        using S = ui::TransportView::State;
        if (song_ == nullptr)
            view.state = S::noSong;
        else
            switch (snap.state)
            {
                case audio::PlaybackState::stopped:   view.state = S::stopped; break;
                case audio::PlaybackState::playing:   view.state = S::playing; break;
                case audio::PlaybackState::paused:    view.state = S::paused; break;
                case audio::PlaybackState::completed: view.state = S::completed; break;
                case audio::PlaybackState::waitingForGate: view.state = S::waiting; break;
            }
        view.positionSeconds = snap.positionSeconds;
        view.durationSeconds = song_ != nullptr ? song_->durationSeconds : 0.0;
        view.audioAvailable = audioError_.empty();
        view.audioText = audioError_.empty() ? "Built-in synth" : audioError_;
        view.mayRingOut = snap.activeVoices > 0;
        content_->setTransport (view);

        const auto practice = lesson_->snapshot();
        ui::PracticeView pv;
        pv.practising = practice.practising;
        pv.mode = static_cast<int> (practice.mode);
        pv.waiting = practice.waiting;
        pv.tolerance = static_cast<int> (practice.tolerance);
        pv.playOtherPart = practice.accompaniment.playOtherPianoPart;
        pv.playBacking = practice.accompaniment.playBacking;
        pv.accompanimentOnly = practice.accompanimentOnly;
        pv.current = { practice.current.right, practice.current.left };
        for (const auto& step : practice.upcoming)
            pv.upcoming.push_back ({ step.right, step.left });
        pv.required = practice.missing;
        pv.matched = practice.matched;
        pv.wrong = practice.recentWrong;
        pv.gateNumber = practice.gateNumber;
        pv.gateCount = practice.gateCount;
        content_->setPractice (pv);
    }

    void rememberInput (const midi_io::MidiDeviceInfo& device)
    {
        if (settings_.midiInput.deviceId == device.identifier && settings_.midiInput.lastSeenName == device.name)
            return;
        settings_.midiInput = { device.identifier, device.name };
        if (saveSettings())
            log_->write (LogLevel::info, "settings", "Remembered MIDI input", { { "device", device.name } });
    }

    bool saveSettings()
    {
        if (const auto error = settings::saveToFile (settingsFile(), settings_); ! error.empty())
        {
            log_->write (LogLevel::error, "settings", error);
            return false;
        }
        return true;
    }

    ui::AppLookAndFeel lookAndFeel_; // outlives every component
    std::unique_ptr<diagnostics::RotatingLog> log_;
    settings::Settings settings_;
    std::unique_ptr<MidiInputService> midi_;
    std::unique_ptr<MainWindow> window_;
    ui::MainComponent* content_ = nullptr; // owned by window_
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File lastFolder_ = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    std::shared_ptr<const timeline::Song> song_;
    std::string songTitle_;
    std::vector<bool> mutedParts_; ///< "Other tracks" the user switched off
    ui::InputMonitorView lastInputView_;
    std::unique_ptr<AudioService> audio_;
    std::unique_ptr<LessonController> lesson_;
    std::unique_ptr<InstrumentService> instruments_;
    std::string audioError_;
};

} // namespace pianoassist::app

START_JUCE_APPLICATION (pianoassist::app::PianoAssistApplication)
