#include "app/AppPaths.h"
#include "app/AudioService.h"
#include "app/InstrumentService.h"
#include "app/LessonController.h"
#include "app/MidiInputService.h"
#include "app/PluginScanner.h"
#include "app/SongLoader.h"
#include "app/UpdateChecker.h"
#include "core/BinaryFile.h"
#include "core/Utf8Path.h"
#include "diagnostics/DiagnosticReport.h"
#include "diagnostics/RotatingLog.h"
#include "diagnostics/SessionReport.h"
#include "settings/Settings.h"
#include "settings/SongLibrary.h"
#include "ui/AppLookAndFeel.h"
#include "ui/MainComponent.h"
#include "ui/LibraryComponent.h"
#include "ui/SettingsComponent.h"
#include "ui/Translations.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
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

juce::String toJuce (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }
juce::File toFile (const std::filesystem::path& path) { return juce::File (toJuce (pathToUtf8 (path))); }
std::filesystem::path toPath (const juce::File& file) { return pathFromUtf8 (file.getFullPathName().toStdString()); }

bool isLibrarySong (const std::filesystem::path& midi)
{
    std::error_code ec;
    return std::filesystem::is_regular_file (midi.parent_path() / "manifest.json", ec);
}

std::string versionText()
{
    auto text = toString (installedVersion());
    if (std::string_view (PIANOASSIST_COMMIT) != "local")
        text += " (" + std::string (PIANOASSIST_COMMIT).substr (0, 7) + ")";
    return text;
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
                 { "version", versionText() },
                 { "portable", isPortable() ? "yes" : "no" },
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
    info.appVersion = versionText();
    info.osName = juce::SystemStats::getOperatingSystemName().toStdString();
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        info.midiInputNames.push_back (device.name.toStdString());
    info.selectedMidiInput = s.midiInput.lastSeenName;
    return info;
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::Component* content, std::function<void()> onClose)
        : DocumentWindow (name, ui::theme::window, DocumentWindow::allButtons),
          onClose_ (std::move (onClose))
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

    void closeButtonPressed() override { onClose_(); }

private:
    std::function<void()> onClose_;
};

/** A tool window that hides on close (settings, help). */
class ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& name, juce::Component* content)
        : DocumentWindow (name, ui::theme::window, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        setResizeLimits (640, 460, 4000, 4000);
    }
    void closeButtonPressed() override { setVisible (false); }
};

/** Shown in a tool window: a read-only text. */
class TextPage final : public juce::Component
{
public:
    explicit TextPage (const juce::String& text)
    {
        editor_.setMultiLine (true);
        editor_.setReadOnly (true);
        editor_.setScrollbarsShown (true);
        editor_.setFont (juce::FontOptions (15.0f));
        editor_.setText (text, false);
        addAndMakeVisible (editor_);
        setSize (680, 560);
    }
    void resized() override { editor_.setBounds (getLocalBounds().reduced (12)); }
    void paint (juce::Graphics& g) override { g.fillAll (ui::theme::panel); }

private:
    juce::TextEditor editor_;
};
} // namespace

class PianoAssistApplication final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // The plug-in scan helper is this exe again; it must not be redirected
    // to the running instance.
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains (scanPluginSwitch); }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains (scanPluginSwitch))
        {
            setApplicationReturnValue (runPluginScanHelper (juce::StringArray::fromTokens (commandLine, true)));
            helperMode_ = true;
            quit();
            return;
        }
        commandLine_ = commandLine;
        chooseDataFolder();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (window_ == nullptr)
            return;
        window_->toFront (true);
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        if (helperMode_)
            return;
        if (log_ != nullptr)
            log_->write (LogLevel::info, "session", "Shutdown begin");
        stopTimer();
        if (lesson_ != nullptr)
            saveSongNow();
        if (log_ != nullptr)
            if (const auto error = settings::saveToFile (settingsFile(), settings_); ! error.empty())
                log_->write (LogLevel::error, "settings", error);
        chooser_.reset();
        updates_.reset();
        settingsWindow_.reset();
        helpWindow_.reset();
        libraryWindow_.reset();
        library_ui_ = nullptr;
        lesson_.reset();
        if (instruments_ != nullptr)
            instruments_->saveState();
        instruments_.reset();
        audio_.reset();
        midi_.reset();
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::LocalisedStrings::setCurrentMappings (nullptr);
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
    // --- startup --------------------------------------------------------------------------------

    /** Portable copies keep everything in Data\ beside the exe. When that
        folder cannot be written (e.g. under Program Files) the user picks
        another one; the per-user folder is never used silently instead. */
    void chooseDataFolder()
    {
        if (! portableMarkerPresent())
        {
            setDataDirectory (userDataDirectory(), false);
            start();
            return;
        }
        if (isWritableDirectory (portableDataDirectory()))
        {
            setDataDirectory (portableDataDirectory(), true);
            start();
            return;
        }
        askForPortableFolder (toJuce (pathToUtf8 (portableDataDirectory())));
    }

    void askForPortableFolder (const juce::String& failedFolder)
    {
        // The language is not known yet: the question is shown in both.
        const auto message = juce::String::fromUTF8 (
                                 "Портативная версия не может записывать в папку:\n")
                             + failedFolder
                             + juce::String::fromUTF8 ("\n\nПереместите папку PianoAssist туда, где можно писать (например, в Документы), "
                                                      "или выберите другую папку для данных на этот запуск.\n\n")
                             + "This portable copy cannot write to the folder above. Move the PianoAssist folder somewhere "
                               "writable, or choose another data folder for this run.";
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "PianoAssist", message,
                                            juce::String::fromUTF8 ("Выбрать папку / Choose folder..."),
                                            juce::String::fromUTF8 ("Выйти / Quit"), nullptr,
                                            juce::ModalCallbackFunction::create ([this] (int result) {
                                                if (result == 0)
                                                {
                                                    quit();
                                                    return;
                                                }
                                                chooser_ = std::make_unique<juce::FileChooser> (
                                                    "PianoAssist data", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
                                                chooser_->launchAsync (
                                                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                    [this] (const juce::FileChooser& chooser) {
                                                        const auto folder = chooser.getResult();
                                                        if (folder == juce::File())
                                                        {
                                                            quit();
                                                            return;
                                                        }
                                                        if (! isWritableDirectory (toPath (folder)))
                                                        {
                                                            askForPortableFolder (folder.getFullPathName());
                                                            return;
                                                        }
                                                        setDataDirectory (toPath (folder), true);
                                                        start();
                                                    });
                                            }));
    }

    void start()
    {
        diagnostics::RotatingLog::Options options;
        options.directory = logDirectory();
        options.maxFileBytes = 5 * 1024 * 1024;
        options.maxArchivedFiles = 5;
        log_ = std::make_unique<diagnostics::RotatingLog> (options);
        installCrashLogging (*log_);

        log_->write (LogLevel::info, "startup", "Startup begin", { { "appVersion", versionText() } });
        logEnvironment (*log_);
        settings_ = loadSettings (*log_);
        diagnostics::logSessionStart (*log_, describeSession (settings_));

        if (settings_.appearance.language == "ru")
            juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (ui::russianTranslations(), false));
        ui::theme::applyTheme (settings_.appearance.theme == "light");
        lookAndFeel_.refreshColours();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);
        juce::Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (settings_.appearance.uiScale));

        midi_ = std::make_unique<MidiInputService> (
            *log_, MidiInputService::Remembered { settings_.midiInput.deviceId, settings_.midiInput.lastSeenName });

        auto* content = new ui::MainComponent (mainIntents());
        content_ = content;
        content->setPixelsPerSecond (settings_.visual.pixelsPerSecond);
        content->setAppearance (settings_.appearance.theme == "light", settings_.appearance.uiScale);
        content->setTempo (static_cast<int> (std::lround (settings_.practice.tempoPercent)));
        content->setRecentFiles (settings_.recentFiles);
        midi_->setViewListener ([this] (const ui::InputMonitorView& view) {
            lastInputView_ = view;
            content_->setInputMonitor (view);
            if (settings_ui_ != nullptr)
                settings_ui_->setInputMonitor (view);
        });
        midi_->setChoiceListener ([this] (const midi_io::MidiDeviceInfo& device) { rememberInput (device); });

        window_ = std::make_unique<MainWindow> (getApplicationName(), content, [this] {
            log_->write (LogLevel::info, "ui", "Window close requested");
            systemRequestedQuit();
        });
        log_->write (LogLevel::info, "startup", "Main window shown");

        midi_->start();

        audio_ = std::make_unique<AudioService> (*log_);
        audioError_ = audio_->start (settings_.audio.deviceStateXml);
        // Profiles are chosen as presets; an older or edited file maps to one.
        settings_.practiceProfile = lesson::profileFor (lesson::toleranceOf (settings_.practiceProfile));
        lesson_ = std::make_unique<LessonController> (*log_, *audio_, midi_->lessonQueue(), settings_.practiceProfile,
                                                      settings_.calibration.inputOffsetMs);
        instruments_ = std::make_unique<InstrumentService> (*log_, *audio_, settings_.sound, [this] { saveSettings(); });
        instruments_->onChange = [this] { showSound(); };
        instruments_->onTrackSoundsChanged = [this] {
            content_->setPartSounds (instruments_->partSoundIds());
            scheduleSongSave();
        };
        instruments_->restore();
        showSound();
        lesson_->setMode (settings_.practice.mode);
        lesson_->setAccompaniment (settings_.practice.accompaniment);
        lesson_->setPractising (settings_.practice.waitForNotes);
        lesson_->setTempo (settings_.practice.tempoPercent / 100.0);
        lesson_->setCountIn (settings_.practice.countIn);
        lesson_->start();
        content->setVolume (0.8f);
        startTimerHz (30);
        log_->write (LogLevel::info, "startup", "Startup complete", { { "dataFolder", isPortable() ? "portable" : "per-user" } });

        openFromCommandLine (commandLine_);
    }

    void openFromCommandLine (const juce::String& commandLine)
    {
        const auto argument = commandLine.unquoted().trim();
        if (argument.isNotEmpty() && juce::File::isAbsolutePath (argument) && juce::File (argument).existsAsFile())
        {
            log_->write (LogLevel::info, "startup", "Opening file from the command line");
            openSong (juce::File (argument));
        }
    }

    ui::MainComponent::Intents mainIntents()
    {
        ui::MainComponent::Intents i;
        i.chooseMidiFile = [this] { chooseMidiFile(); };
        i.openMidiFile = [this] (const juce::File& file) { openSong (file); };
        i.closeSong = [this] { closeSong(); };
        i.quit = [this] { systemRequestedQuit(); };
        i.openSettings = [this] (ui::SettingsPage page) { openSettings (page); };
        i.showHelp = [this] { showHelp(); };
        i.showAbout = [this] { showAbout(); };
        i.copyDiagnostics = [this] { copyDiagnostics(); };
        i.saveDiagnostics = [this] { saveDiagnostics(); };
        i.openLogFolder = [this] { openFolder (logDirectory(), "log"); };

        i.playPause = [this] { lesson_->playPause(); scheduleSongSave(); };
        i.stop = [this] { lesson_->stop(); scheduleSongSave(); };
        i.silence = [this] { lesson_->silence(); };
        i.seek = [this] (double seconds) { lesson_->seek (seconds); scheduleSongSave(); };
        i.tempoChanged = [this] (int percent) {
            settings_.practice.tempoPercent = std::clamp (percent, 25, 200);
            lesson_->setTempo (settings_.practice.tempoPercent / 100.0);
            manifest_.practice.tempoPercent = settings_.practice.tempoPercent;
            log_->write (LogLevel::info, "ui", "Tempo changed", { { "percent", std::to_string (percent) } });
            saveSettings();
            scheduleSongSave();
        };
        i.loopChanged = [this] (bool enabled, double start, double end) {
            manifest_.loop = { enabled, start, end };
            lesson_->setLoop (enabled, start, end);
            content_->setLoop (enabled, start, end);
            log_->write (LogLevel::info, "ui", enabled ? "Loop set" : "Loop off",
                         { { "start", juce::String (start, 2).toStdString() }, { "end", juce::String (end, 2).toStdString() } });
            scheduleSongSave();
        };
        i.volumeChanged = [this] (float gain) { audio_->setGain (gain); };
        i.zoomChanged = [this] (double pixelsPerSecond) {
            settings_.visual.pixelsPerSecond = pixelsPerSecond;
            manifest_.pixelsPerSecond = pixelsPerSecond;
            scheduleSongSave();
        };

        i.practiceChanged = [this] (bool practising) {
            lesson_->setPractising (practising);
            manifest_.practice.waitForNotes = practising;
            scheduleSongSave();
        };
        i.modeChanged = [this] (int mode) {
            settings_.practice.mode = toPracticeMode (mode);
            manifest_.practice.mode = settings_.practice.mode;
            lesson_->setMode (settings_.practice.mode);
            saveSettings();
            scheduleSongSave();
        };
        i.toleranceChanged = [this] (int tolerance) { setTolerance (tolerance); };
        i.playOtherPartChanged = [this] (bool play) {
            settings_.practice.accompaniment.playOtherPianoPart = play;
            manifest_.practice.accompaniment = settings_.practice.accompaniment;
            lesson_->setAccompaniment (settings_.practice.accompaniment);
            saveSettings();
            scheduleSongSave();
        };
        i.playBackingChanged = [this] (bool play) {
            settings_.practice.accompaniment.playBacking = play;
            manifest_.practice.accompaniment = settings_.practice.accompaniment;
            lesson_->setAccompaniment (settings_.practice.accompaniment);
            saveSettings();
            scheduleSongSave();
        };
        i.countInChanged = [this] (bool on) {
            settings_.practice.countIn = on;
            lesson_->setCountIn (on);
            saveSettings();
        };
        i.partAssignmentChanged = [this] (std::size_t part, int assignment) {
            reassignPart (part, assignment);
            scheduleSongSave();
        };
        i.partMuteChanged = [this] (std::size_t part, bool muted) {
            if (song_ == nullptr || part >= song_->parts.size())
                return;
            mutedParts_.resize (song_->parts.size(), false);
            mutedParts_[part] = muted;
            log_->write (LogLevel::info, "ui", muted ? "Track muted" : "Track unmuted", { { "part", std::to_string (part + 1) } });
            lesson_->setMutedParts (mutedParts_);
            scheduleSongSave();
        };
        i.partVolumeChanged = [this] (std::size_t part, double volume) {
            if (song_ == nullptr || part >= song_->parts.size())
                return;
            partVolumes_.resize (song_->parts.size(), 1.0);
            partVolumes_[part] = std::clamp (volume, 0.0, 1.0);
            lesson_->setPartGains (partVolumes_);
            scheduleSongSave();
        };
        i.openSongFolder = [this] {
            if (! songFolder_.empty())
                openFolder (songFolder_, "song");
        };
        i.openLibraryFolder = [this] { openFolder (libraryRoot (settings_.library.root), "library"); };
        i.showLibrary = [this] { showLibrary(); };
        i.checkForUpdates = [this] { checkForUpdates(); };
        i.partSoundChanged = [this] (std::size_t part, int soundId) { instruments_->choosePart (part, soundId); };
        i.keyClicked = [this] (std::uint8_t pitch, bool down) { lesson_->previewKey (pitch, down); };
        i.soundChosen = [this] (int id) { instruments_->chooseMain (id); };
        i.showInstrumentWindow = [this] { instruments_->showEditor(); };
        i.themeChanged = [this] (bool light) { setTheme (light); };
        i.uiScaleChanged = [this] (double scale) { setScale (scale); };
        return i;
    }

    ui::SettingsComponent::Intents settingsIntents()
    {
        ui::SettingsComponent::Intents i;
        i.selectInput = [this] (const std::string& id) { midi_->selectByUser (id); };
        i.rescanInputs = [this] {
            log_->write (LogLevel::info, "ui", "User pressed Rescan");
            midi_->rescan ("user rescan");
        };
        i.inputOffsetChanged = [this] (double ms) {
            settings_.calibration.inputOffsetMs = ms;
            saveSettings();
            showRestartNote (TRANS ("The new input delay is used after PianoAssist is restarted."));
        };
        i.audioDeviceChanged = [this] {
            const auto xml = audio_->deviceStateXml();
            if (xml.size() <= 16 * 1024 && xml != settings_.audio.deviceStateXml)
            {
                settings_.audio.deviceStateXml = xml;
                audioError_.clear();
                saveSettings();
                refreshSettingsValues();
            }
        };
        i.soundChosen = [this] (int id) { instruments_->chooseMain (id); };
        i.loadSoundFont = [this] { chooseSoundFont(); };
        i.addPluginFile = [this] { choosePluginFile(); };
        i.addPluginFolder = [this] { choosePluginFolder(); };
        i.removePluginFolder = [this] (std::size_t index) { instruments_->removeScanFolder (index); };
        i.scanPlugins = [this] { instruments_->startScan(); };
        i.rescanAllPlugins = [this] { instruments_->rescanAll(); };
        i.showInstrumentWindow = [this] { instruments_->showEditor(); };
        i.toleranceChanged = [this] (int tolerance) { setTolerance (tolerance); };
        i.waitForNotesChanged = [this] (bool wait) {
            settings_.practice.waitForNotes = wait;
            saveSettings();
        };
        i.chooseLibraryFolder = [this] { chooseLibraryFolder(); };
        i.openLibraryFolder = [this] { openFolder (libraryRoot (settings_.library.root), "library"); };
        i.themeChanged = [this] (bool light) { setTheme (light); };
        i.uiScaleChanged = [this] (double scale) { setScale (scale); };
        i.languageChanged = [this] (const std::string& language) {
            if (language == settings_.appearance.language)
                return;
            settings_.appearance.language = language;
            saveSettings();
            showRestartNote (language == "ru" ? juce::String::fromUTF8 ("Язык сменится после перезапуска PianoAssist.")
                                              : juce::String ("The language changes after PianoAssist is restarted."));
        };
        i.zoomChanged = [this] (double pixelsPerSecond) {
            settings_.visual.pixelsPerSecond = pixelsPerSecond;
            manifest_.pixelsPerSecond = pixelsPerSecond;
            content_->setPixelsPerSecond (pixelsPerSecond);
            saveSettings();
            scheduleSongSave();
        };
        i.includeTestBuildsChanged = [this] (bool include) {
            settings_.updates.includeTestBuilds = include;
            saveSettings();
        };
        i.checkForUpdates = [this] { checkForUpdates(); };
        i.copyDiagnostics = [this] { copyDiagnostics(); };
        i.saveDiagnostics = [this] { saveDiagnostics(); };
        i.openLogFolder = [this] { openFolder (logDirectory(), "log"); };
        i.openDataFolder = [this] { openFolder (dataDirectory(), "data"); };
        return i;
    }

    // --- settings window and appearance ---------------------------------------------------------

    void openSettings (ui::SettingsPage page)
    {
        if (settingsWindow_ == nullptr)
        {
            auto* component = new ui::SettingsComponent (settingsIntents(), audio_->deviceManager());
            settings_ui_ = component;
            settingsWindow_ = std::make_unique<ToolWindow> (TRANS ("PianoAssist settings"), component);
            settingsWindow_->centreAroundComponent (window_.get(), component->getWidth(), component->getHeight());
            component->setInputMonitor (lastInputView_);
            component->setSound (instruments_->view());
        }
        refreshSettingsValues();
        settings_ui_->showPage (page);
        settingsWindow_->setVisible (true);
        settingsWindow_->toFront (true);
        log_->write (LogLevel::info, "ui", "Settings opened", { { "page", std::to_string (static_cast<int> (page)) } });
    }

    void refreshSettingsValues()
    {
        if (settings_ui_ == nullptr)
            return;
        ui::SettingsComponent::Values v;
        v.tolerance = static_cast<int> (lesson::toleranceOf (settings_.practiceProfile));
        v.waitForNotes = settings_.practice.waitForNotes;
        v.inputOffsetMs = settings_.calibration.inputOffsetMs;
        v.lightTheme = settings_.appearance.theme == "light";
        v.uiScale = settings_.appearance.uiScale;
        v.language = settings_.appearance.language;
        v.pixelsPerSecond = settings_.visual.pixelsPerSecond;
        v.libraryFolder = pathToUtf8 (libraryRoot (settings_.library.root));
        v.dataFolder = pathToUtf8 (dataDirectory());
        v.portable = isPortable();
        v.includeTestBuilds = settings_.updates.includeTestBuilds;
        v.version = versionText();
        v.audioDescription = audioError_.empty() ? audio_->deviceDescription() : audioError_;
        settings_ui_->setValues (v);
    }

    void showSound()
    {
        const auto view = instruments_->view();
        content_->setSound (view);
        if (settings_ui_ != nullptr)
            settings_ui_->setSound (view);
    }

    void setTheme (bool light)
    {
        settings_.appearance.theme = light ? "light" : "dark";
        ui::theme::applyTheme (light);
        lookAndFeel_.refreshColours();
        for (auto* w : std::initializer_list<juce::DocumentWindow*> { window_.get(), settingsWindow_.get(), helpWindow_.get() })
            if (w != nullptr)
            {
                w->setBackgroundColour (ui::theme::window);
                w->sendLookAndFeelChange();
            }
        content_->setAppearance (light, settings_.appearance.uiScale);
        refreshSettingsValues();
        saveSettings();
    }

    void setScale (double scale)
    {
        settings_.appearance.uiScale = std::clamp (scale, 0.75, 2.0);
        juce::Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (settings_.appearance.uiScale));
        content_->setAppearance (settings_.appearance.theme == "light", settings_.appearance.uiScale);
        refreshSettingsValues();
        saveSettings();
    }

    void setTolerance (int tolerance)
    {
        settings_.practiceProfile = lesson::profileFor (static_cast<lesson::Tolerance> (std::clamp (tolerance, 0, 2)));
        manifest_.tolerance = std::clamp (tolerance, 0, 2);
        lesson_->setProfile (settings_.practiceProfile);
        refreshSettingsValues();
        saveSettings();
        scheduleSongSave();
    }

    void showRestartNote (const juce::String& text)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "PianoAssist", text);
    }

    void showHelp()
    {
        const auto text = TRANS (
            "How to practise\n\n"
            "1. Connect the piano by USB and switch it on. The button at the top right shows whether it is found.\n"
            "2. Open a MIDI file (File > Open, or drag it onto the window). PianoAssist keeps a copy in its song library; "
            "your file is not changed.\n"
            "3. Choose the hand: Both, Right or Left. The notes of that hand are yours; PianoAssist plays the rest, "
            "unless you switch off \"Other hand\" or \"Other tracks\".\n"
            "4. With \"Wait for my notes\" on, the song stops at each of your notes and goes on when you press the right keys. "
            "The keys to press are shown under the roll and on the keyboard. Stretches without your notes play by themselves.\n"
            "5. Press Space to play or pause, Esc to stop. Silence stops every sound at once.\n"
            "6. Slow down with Tempo. Drag across the bar numbers above the roll to repeat a passage (loop).\n\n"
            "Each song remembers its hands, tracks, sounds, tempo, loop and position. Settings (Ctrl+,) has the MIDI input, "
            "sound output, input delay, instruments (VST3, SoundFont), timing tolerance, theme, size and language.\n\n"
            "If something goes wrong: Help > Diagnostics shows what the piano sends, and \"Copy report\" gives a text to send.");
        helpWindow_ = std::make_unique<ToolWindow> (TRANS ("How to practise"), new TextPage (text));
        helpWindow_->centreAroundComponent (window_.get(), 680, 560);
        helpWindow_->setVisible (true);
    }

    void showAbout()
    {
        const auto text = "PianoAssist " + toJuce (versionText()) + "\n\n"
                          + TRANS ("Practise MIDI songs on a digital piano: the song waits for your notes.") + "\n\n"
                          + TRANS ("Data folder") + ": " + toJuce (pathToUtf8 (dataDirectory())) + (isPortable() ? " (portable)" : "")
                          + "\n\n" + TRANS ("Built with JUCE, TinySoundFont, nlohmann/json. Licences are in the program folder.");
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, TRANS ("About PianoAssist"), text);
    }

    void openFolder (const std::filesystem::path& folder, const char* what)
    {
        std::error_code ec;
        std::filesystem::create_directories (folder, ec);
        log_->write (LogLevel::info, "ui", "User opened a folder", { { "folder", what } });
        if (! toFile (folder).startAsProcess())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TRANS ("Could not open the folder"),
                                                    toJuce (pathToUtf8 (folder)));
    }

    // --- updates ------------------------------------------------------------------------------

    void checkForUpdates()
    {
        if (updates_ == nullptr)
            updates_ = std::make_unique<UpdateChecker>();
        if (updates_->isChecking())
            return;
        log_->write (LogLevel::info, "updates", "Checking for updates",
                     { { "installed", versionText() }, { "testBuilds", settings_.updates.includeTestBuilds ? "yes" : "no" } });
        updates_->check (settings_.updates.includeTestBuilds, [this] (const settings::UpdateCheck& result) {
            const auto installed = toJuce (versionText());
            if (! result.error.empty())
            {
                log_->write (LogLevel::warning, "updates", "Update check failed", { { "error", result.error } });
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TRANS ("Could not check for updates"),
                                                        TRANS (toJuce (result.error)) + "\n\n" + TRANS ("Installed") + ": " + installed);
                return;
            }
            if (! result.newest)
            {
                log_->write (LogLevel::info, "updates", "No update");
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon, TRANS ("No updates"),
                    TRANS ("You have the newest version.") + "\n\n" + TRANS ("Installed") + ": " + installed
                        + (settings_.updates.includeTestBuilds ? "" : "\n" + TRANS ("Test builds are not offered (see Settings > Diagnostics).")));
                return;
            }
            const auto offer = *result.newest;
            log_->write (LogLevel::info, "updates", "Update available", { { "tag", offer.tag } });
            auto text = TRANS ("Installed") + ": " + installed + "\n" + TRANS ("Available") + ": "
                        + toJuce (settings::toString (offer.version)) + (offer.testBuild ? " (" + TRANS ("test build") + ")" : "")
                        + "\n\n" + toJuce (offer.notes) + "\n\n"
                        + (isPortable() ? TRANS ("Portable copy: download the portable ZIP and replace the program files; keep the Data folder.")
                                        : TRANS ("Download the installer and run it; your settings and songs are kept."))
                        + "\n" + TRANS ("Nothing is downloaded or installed until you do it yourself.");
            const auto page = juce::URL (toJuce (offer.pageUrl));
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::InfoIcon, TRANS ("Update available"), text,
                                                TRANS ("Open the release page"), TRANS ("Later"), nullptr,
                                                juce::ModalCallbackFunction::create ([page] (int choice) {
                                                    if (choice != 0)
                                                        page.launchInDefaultBrowser();
                                                }));
        });
    }

    // --- song library window ---------------------------------------------------------------------

    void showLibrary()
    {
        if (libraryWindow_ == nullptr)
        {
            ui::LibraryComponent::Intents i;
            i.open = [this] (std::size_t row) {
                if (row >= librarySongs_.size())
                    return;
                const auto& song = librarySongs_[row];
                auto loaded = settings::loadManifest (song.folder);
                libraryWindow_->setVisible (false);
                openSong (toFile (song.folder / pathFromUtf8 (loaded.manifest.midiFile)));
            };
            i.openFolder = [this] (std::size_t row) {
                if (row < librarySongs_.size())
                    openFolder (librarySongs_[row].folder, "song");
            };
            i.remove = [this] (std::size_t row) { if (row < librarySongs_.size()) removeFromLibrary (librarySongs_[row]); };
            i.refresh = [this] { refreshLibrary(); };
            i.openLibraryFolder = [this] { openFolder (libraryRoot (settings_.library.root), "library"); };
            library_ui_ = new ui::LibraryComponent (std::move (i));
            libraryWindow_ = std::make_unique<ToolWindow> (TRANS ("Song library"), library_ui_);
            libraryWindow_->centreAroundComponent (window_.get(), library_ui_->getWidth(), library_ui_->getHeight());
        }
        refreshLibrary();
        libraryWindow_->setVisible (true);
        libraryWindow_->toFront (true);
        log_->write (LogLevel::info, "ui", "Song library opened", { { "songs", std::to_string (librarySongs_.size()) } });
    }

    void refreshLibrary()
    {
        if (library_ui_ == nullptr)
            return;
        const auto root = libraryRoot (settings_.library.root);
        librarySongs_ = settings::listSongs (root);
        std::vector<ui::LibraryRow> rows;
        for (const auto& s : librarySongs_)
        {
            ui::LibraryRow row { s.name, pathToUtf8 (s.folder.filename()), {}, s.sourcePath, s.midiMissing };
            if (! s.lastOpenedAt.empty())
                row.lastOpened = juce::Time::fromISO8601 (toJuce (s.lastOpenedAt)).formatted ("%d.%m.%Y %H:%M").toStdString();
            rows.push_back (std::move (row));
        }
        library_ui_->setRows (std::move (rows), pathToUtf8 (root));
    }

    void removeFromLibrary (const settings::SongEntry& song)
    {
        const auto text = TRANS ("Remove \"%1\" from the library?").replace ("%1", toJuce (song.name)) + "\n\n"
                          + TRANS ("This deletes the library's copy, its saved settings and plug-in states. The original MIDI file is not deleted:")
                          + "\n" + toJuce (song.sourcePath.empty() ? std::string ("-") : song.sourcePath);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, TRANS ("Remove from library"),
                                                                     text, TRANS ("Remove"), TRANS ("Cancel"), library_ui_);
        juce::AlertWindow::showAsync (options, [this, folder = song.folder] (int result) {
            if (result != 1)
                return;
            if (! songFolder_.empty() && std::filesystem::equivalent (songFolder_, folder))
                closeSong();
            const auto midiPrefix = pathToUtf8 (folder);
            auto removed = settings::removeSong (libraryRoot (settings_.library.root), folder);
            if (! removed)
            {
                log_->write (LogLevel::error, "library", "Remove failed", { { "error", removed.error() } });
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TRANS ("Could not remove the song"),
                                                        toJuce (removed.error()));
            }
            else
            {
                log_->write (LogLevel::info, "library", "Song removed from library", { { "folder", fileNameForLog (folder) } });
                auto& recent = settings_.recentFiles;
                recent.erase (std::remove_if (recent.begin(), recent.end(),
                                              [&] (const std::string& p) { return p.rfind (midiPrefix, 0) == 0; }),
                              recent.end());
                content_->setRecentFiles (recent);
                saveSettings();
            }
            refreshLibrary();
        });
    }

    // --- songs ----------------------------------------------------------------------------------

    void chooseMidiFile()
    {
        log_->write (LogLevel::info, "ui", "User pressed Open MIDI file");
        chooser_ = std::make_unique<juce::FileChooser> (TRANS ("Open a MIDI file"), lastFolder_, "*.mid;*.midi;*.kar");
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

    /** Imports the file into the library (a copy; the original is only read),
        then loads it with the song's saved choices. */
    void openSong (const juce::File& file)
    {
        saveSongNow();
        const auto source = toPath (file);
        if (! isLibrarySong (source))
            lastFolder_ = file.getParentDirectory();
        content_->setLoading (file.getFileName().toStdString());
        // Let the window show "Loading..." before the work starts.
        juce::Timer::callAfterDelay (30, [this, source, name = file.getFileNameWithoutExtension().toStdString()] {
            if (lesson_ != nullptr)
                loadSongFrom (source, name);
        });
    }

    void loadSongFrom (const std::filesystem::path& source, const std::string& name)
    {
        std::filesystem::path folder;
        std::vector<std::string> notices;
        if (isLibrarySong (source))
            folder = source.parent_path();
        else
        {
            const auto root = libraryRoot (settings_.library.root);
            auto imported = settings::importMidi (root, source, name);
            if (! imported)
            {
                // The library is unavailable: practise anyway, without saving.
                log_->write (LogLevel::error, "library", "Import failed", { { "error", imported.error() } });
                notices.push_back (TRANS ("The song library is not available, so this song's settings are not saved:").toStdString()
                                   + " " + imported.error());
            }
            else
            {
                folder = imported.value().entry.folder;
                log_->write (LogLevel::info, "library", imported.value().reused ? "Song already in the library" : "Song imported",
                             { { "folder", fileNameForLog (folder) } });
                if (! imported.value().reused)
                    notices.push_back (TRANS ("Added to your song library as a copy; the original file is not changed.").toStdString());
            }
        }

        settings::SongManifest manifest;
        bool fresh = true;
        if (! folder.empty())
        {
            auto loaded = settings::loadManifest (folder);
            for (const auto& warning : loaded.warnings)
                log_->write (LogLevel::warning, "library", warning);
            if (loaded.recovered)
                notices.push_back (TRANS ("This song's saved settings were damaged; defaults are used (the old file is kept as manifest.json.bad).").toStdString());
            manifest = std::move (loaded.manifest);
            fresh = manifest.tracks.empty() && manifest.positionSeconds == 0.0 && ! manifest.loop.enabled;
            if (manifest.id.empty())
                manifest.id = settings::makeSongId();
        }
        if (fresh)
        {
            // A new song starts with the current choices.
            manifest.practice = settings_.practice;
            manifest.tolerance = static_cast<int> (lesson::toleranceOf (settings_.practiceProfile));
            manifest.pixelsPerSecond = settings_.visual.pixelsPerSecond;
        }
        if (manifest.name.empty())
            manifest.name = name;

        const auto midi = folder.empty() ? source : folder / pathFromUtf8 (manifest.midiFile);
        auto song = loadSong (midi, *log_);
        if (! song)
        {
            content_->setSongError (TRANS ("Could not open").toStdString() + " " + name + ": " + song.error());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TRANS ("Could not open the MIDI file"),
                                                    toJuce (name) + "\n\n" + toJuce (song.error()));
            return;
        }

        lesson_->stop();
        song_ = std::move (song).value();
        songFolder_ = folder;
        manifest_ = std::move (manifest);
        manifest_.lastOpenedAt = settings::currentTimeIso();
        if (! songFolder_.empty())
        {
            // Only the time stamp changes here: the rest is still what was on disk.
            if (auto saved = settings::saveManifest (songFolder_, manifest_); ! saved)
                log_->write (LogLevel::warning, "library", "Could not record when the song was opened", { { "error", saved.error() } });
        }
        songTitle_ = manifest_.name;
        songNotices_ = notices;
        lastStateHashes_.clear();
        applyManifest();
        rememberRecent (pathToUtf8 (midi));
        log_->write (LogLevel::info, "song", "Song shown in piano roll");
        refreshLibrary();
    }

    /** Applies the song's saved choices; nothing starts playing. */
    void applyManifest()
    {
        const auto& m = manifest_;
        const auto& parts = song_->parts;

        // Hands: only parts the user changed were saved; others keep the guess.
        auto song = *song_;
        for (std::size_t i = 0; i < parts.size() && i < m.tracks.size(); ++i)
        {
            const auto& saved = m.tracks[i];
            if (saved.track != parts[i].track || saved.channel != parts[i].channel || saved.assignment < 0 || saved.assignment > 3)
                continue;
            const auto to = static_cast<timeline::Part::Assignment> (saved.assignment);
            if (song.parts[i].assignment == to && song.parts[i].setByUser)
                continue;
            if (auto mapped = timeline::reassignPart (song, i, to))
                song = std::move (mapped).value();
        }
        song_ = std::make_shared<const timeline::Song> (std::move (song));

        mutedParts_.assign (parts.size(), false);
        partVolumes_.assign (parts.size(), 1.0);
        std::vector<int> channels;
        std::vector<InstrumentService::TrackSound> sounds (parts.size());
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            channels.push_back (parts[i].channel);
            if (i >= m.tracks.size() || m.tracks[i].track != parts[i].track || m.tracks[i].channel != parts[i].channel)
                continue;
            const auto& saved = m.tracks[i];
            mutedParts_[i] = saved.muted;
            partVolumes_[i] = std::clamp (saved.volume, 0.0, 1.0);
            sounds[i].choice = saved.sound;
            if (! saved.stateFile.empty() && ! songFolder_.empty())
            {
                const auto bytes = settings::readTrackState (songFolder_, saved.stateFile);
                sounds[i].state.append (bytes.data(), bytes.size());
            }
        }

        // The song's choices become the current ones; "wait when a song is
        // opened" stays the user's global default.
        const bool waitDefault = settings_.practice.waitForNotes;
        const bool countIn = settings_.practice.countIn;
        settings_.practice = m.practice;
        settings_.practice.waitForNotes = waitDefault;
        settings_.practice.countIn = countIn;
        settings_.practiceProfile = lesson::profileFor (static_cast<lesson::Tolerance> (std::clamp (m.tolerance, 0, 2)));
        settings_.visual.pixelsPerSecond = m.pixelsPerSecond;

        // Order matters: loading clears mutes, volumes and the loop.
        lesson_->loadSong (song_);
        lesson_->setMode (m.practice.mode);
        lesson_->setAccompaniment (m.practice.accompaniment);
        lesson_->setPractising (m.practice.waitForNotes);
        lesson_->setTempo (m.practice.tempoPercent / 100.0);
        lesson_->setProfile (settings_.practiceProfile);
        lesson_->setMutedParts (mutedParts_);
        lesson_->setPartGains (partVolumes_);
        lesson_->setLoop (m.loop.enabled, m.loop.startSeconds, m.loop.endSeconds);
        if (m.positionSeconds > 0.0 && m.positionSeconds < song_->durationSeconds)
            lesson_->seek (m.positionSeconds);

        instruments_->setSong (channels, std::move (sounds));
        showSongView();
        content_->setPixelsPerSecond (m.pixelsPerSecond);
        content_->setTempo (static_cast<int> (std::lround (m.practice.tempoPercent)));
        content_->setTrackMix (mutedParts_, partVolumes_);
        content_->setPartSounds (instruments_->partSoundIds());
        content_->setLoop (m.loop.enabled, m.loop.startSeconds, m.loop.endSeconds);
        refreshSettingsValues();
        saveSettings();
    }

    void showSongView()
    {
        auto view = makePianoRollView (*song_, songTitle_);
        for (const auto& notice : songNotices_)
            view.notices.push_back (notice);
        content_->setSong (std::make_shared<const ui::PianoRollView> (std::move (view)));
    }

    void closeSong()
    {
        if (song_ == nullptr)
            return;
        saveSongNow();
        log_->write (LogLevel::info, "song", "Song closed");
        lesson_->stop();
        lesson_->loadSong (nullptr);
        instruments_->setSong ({}, {});
        song_.reset();
        songFolder_.clear();
        manifest_ = {};
        mutedParts_.clear();
        partVolumes_.clear();
        content_->setSong (nullptr);
        content_->setRecentFiles (settings_.recentFiles);
    }

    void rememberRecent (const std::string& path)
    {
        auto& recent = settings_.recentFiles;
        recent.erase (std::remove (recent.begin(), recent.end(), path), recent.end());
        recent.insert (recent.begin(), path);
        if (recent.size() > settings::maxRecentFiles)
            recent.resize (settings::maxRecentFiles);
        content_->setRecentFiles (recent);
    }

    void scheduleSongSave()
    {
        if (! songFolder_.empty())
            songSaveDueMs_ = juce::Time::getMillisecondCounterHiRes() + 1500.0;
    }

    /** Writes the song's manifest (atomically, with a backup) and the plug-in
        states that changed. Runs on the message thread, never the audio thread. */
    void saveSongNow()
    {
        songSaveDueMs_ = 0.0;
        if (song_ == nullptr || songFolder_.empty())
            return;
        auto& m = manifest_;
        const auto& parts = song_->parts;
        const auto sounds = instruments_->trackSounds();
        m.tracks.clear();
        lastStateHashes_.resize (parts.size(), 0);
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            settings::TrackState t;
            t.track = parts[i].track;
            t.channel = parts[i].channel;
            t.assignment = parts[i].setByUser ? static_cast<int> (parts[i].assignment) : -1;
            t.muted = i < mutedParts_.size() && mutedParts_[i];
            t.volume = i < partVolumes_.size() ? partVolumes_[i] : 1.0;
            if (i < sounds.size())
            {
                t.sound = sounds[i].choice;
                t.sound.pluginFolders.clear();
                if (sounds[i].state.getSize() > 0)
                {
                    const auto hash = std::hash<std::string_view> {}(
                        std::string_view (static_cast<const char*> (sounds[i].state.getData()), sounds[i].state.getSize()));
                    t.stateFile = "plugin-state/track-" + std::to_string (i + 1) + ".bin";
                    if (hash != lastStateHashes_[i])
                    {
                        const auto* data = static_cast<const std::uint8_t*> (sounds[i].state.getData());
                        if (auto written = settings::writeTrackState (songFolder_, i, { data, data + sounds[i].state.getSize() }))
                        {
                            t.stateFile = written.value();
                            lastStateHashes_[i] = hash;
                        }
                        else
                            log_->write (LogLevel::error, "library", "Plug-in state not saved", { { "error", written.error() } });
                    }
                }
            }
            m.tracks.push_back (std::move (t));
        }
        const auto position = audio_->snapshot().positionSeconds;
        m.positionSeconds = position < song_->durationSeconds - 0.5 ? std::max (0.0, position) : 0.0;
        if (auto saved = settings::saveManifest (songFolder_, m); ! saved)
            log_->write (LogLevel::error, "library", "Song settings not saved", { { "error", saved.error() } });
    }

    void chooseLibraryFolder()
    {
        chooser_ = std::make_unique<juce::FileChooser> (TRANS ("Choose the song library folder"),
                                                        toFile (libraryRoot (settings_.library.root)));
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto folder = chooser.getResult();
                                   if (folder == juce::File())
                                       return;
                                   if (! isWritableDirectory (toPath (folder)))
                                   {
                                       juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                               TRANS ("This folder cannot be written"),
                                                                               folder.getFullPathName());
                                       return;
                                   }
                                   settings_.library.root = librarySetting (toPath (folder));
                                   log_->write (LogLevel::info, "library", "Library folder changed");
                                   saveSettings();
                                   refreshSettingsValues();
                                   juce::AlertWindow::showMessageBoxAsync (
                                       juce::MessageBoxIconType::InfoIcon, TRANS ("Song library"),
                                       TRANS ("New songs go to this folder. Songs already in the old folder stay there; move their folders over to keep them together."));
                               });
    }

    // --- diagnostics ------------------------------------------------------------------------

    std::string diagnosticReport (std::size_t logLines)
    {
        diagnostics::DiagnosticSummary s;
        s.appVersion = versionText() + (isPortable() ? " portable" : "");
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
                     + ", tempo " + std::to_string (static_cast<int> (settings_.practice.tempoPercent)) + "%"
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
        juce::SystemClipboard::copyTextToClipboard (toJuce (diagnosticReport (300)));
        log_->write (LogLevel::info, "ui", "Diagnostics copied to the clipboard");
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, TRANS ("Diagnostics copied"),
                                                TRANS ("A report with the last 300 log lines is on the clipboard. "
                                                       "Your user folder name is replaced. Paste it where you need it."));
    }

    void saveDiagnostics()
    {
        log_->write (LogLevel::info, "ui", "User pressed Save diagnostics");
        const auto name = "PianoAssist-diagnostics-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".txt";
        chooser_ = std::make_unique<juce::FileChooser> (
            TRANS ("Save diagnostics"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.txt");
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file == juce::File())
                                       return;
                                   const auto report = diagnosticReport (20'000);
                                   const auto path = toPath (file);
                                   const auto written = writeFileAtomically (
                                       path, std::vector<std::uint8_t> (report.begin(), report.end()));
                                   if (! written)
                                       log_->write (LogLevel::error, "ui", "Saving diagnostics failed", { { "error", written.error() } });
                                   else
                                       log_->write (LogLevel::info, "ui", "Diagnostics saved", { { "file", fileNameForLog (path) } });
                               });
    }

    // --- sounds -----------------------------------------------------------------------------

    void chooseSoundFont()
    {
        log_->write (LogLevel::info, "ui", "User pressed Load SoundFont");
        chooser_ = std::make_unique<juce::FileChooser> (TRANS ("Load a SoundFont"), lastFolder_, "*.sf2");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file != juce::File())
                                       instruments_->chooseSoundFont (toPath (file));
                               });
    }

    void choosePluginFile()
    {
        log_->write (LogLevel::info, "ui", "User pressed Add VST3 instrument file");
        const auto folder = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                                .getChildFile ("Common Files").getChildFile ("VST3");
        chooser_ = std::make_unique<juce::FileChooser> (TRANS ("Add a VST3 instrument"), folder, "*.vst3");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file != juce::File())
                                       instruments_->addPluginFile (file);
                               });
    }

    void choosePluginFolder()
    {
        log_->write (LogLevel::info, "ui", "User pressed Add VST3 folder");
        chooser_ = std::make_unique<juce::FileChooser> (TRANS ("Add a folder with VST3 instruments"),
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& chooser) {
                                   const auto folder = chooser.getResult();
                                   if (folder != juce::File())
                                       instruments_->addScanFolder (folder);
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
        showSongView();
    }

    // --- periodic ---------------------------------------------------------------------------

    void timerCallback() override
    {
        audio_->collectGarbage();
        const auto snap = audio_->snapshot();

        if (songSaveDueMs_ > 0.0 && juce::Time::getMillisecondCounterHiRes() >= songSaveDueMs_)
            saveSongNow();

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
        view.audioText = audioError_.empty() ? audio_->deviceDescription() : audioError_;
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
        pv.countIn = settings_.practice.countIn;
        pv.countInBeat = practice.countInBeat;
        pv.countInBeats = practice.countInBeats;
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
    bool helperMode_ = false;
    juce::String commandLine_;
    std::unique_ptr<diagnostics::RotatingLog> log_;
    settings::Settings settings_;
    std::unique_ptr<MidiInputService> midi_;
    std::unique_ptr<MainWindow> window_;
    ui::MainComponent* content_ = nullptr; // owned by window_
    std::unique_ptr<ToolWindow> settingsWindow_;
    ui::SettingsComponent* settings_ui_ = nullptr; // owned by settingsWindow_
    std::unique_ptr<ToolWindow> helpWindow_;
    std::unique_ptr<ToolWindow> libraryWindow_;
    ui::LibraryComponent* library_ui_ = nullptr; // owned by libraryWindow_
    std::vector<settings::SongEntry> librarySongs_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<UpdateChecker> updates_;
    juce::File lastFolder_ = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    std::shared_ptr<const timeline::Song> song_;
    std::string songTitle_;
    std::vector<std::string> songNotices_;
    std::filesystem::path songFolder_; ///< empty when the library is unavailable
    settings::SongManifest manifest_;
    std::vector<std::size_t> lastStateHashes_; ///< plug-in states already on disk
    double songSaveDueMs_ = 0.0;
    std::vector<bool> mutedParts_;    ///< "Other tracks" the user switched off
    std::vector<double> partVolumes_; ///< per track, 0-1

    ui::InputMonitorView lastInputView_;
    std::unique_ptr<AudioService> audio_;
    std::unique_ptr<LessonController> lesson_;
    std::unique_ptr<InstrumentService> instruments_;
    std::string audioError_;
};

} // namespace pianoassist::app

START_JUCE_APPLICATION (pianoassist::app::PianoAssistApplication)
