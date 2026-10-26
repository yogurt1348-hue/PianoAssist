#include "app/AppPaths.h"
#include "app/MidiInputService.h"
#include "core/Utf8Path.h"
#include "diagnostics/RotatingLog.h"
#include "diagnostics/SessionReport.h"
#include "settings/Settings.h"
#include "ui/MainComponent.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <exception>
#include <memory>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;

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
        setResizeLimits (640, 400, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
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

class PianoAssistApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
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
        });
        midi_->setViewListener ([content] (const ui::InputMonitorView& view) { content->setInputMonitor (view); });
        midi_->setChoiceListener ([this] (const midi_io::MidiDeviceInfo& device) { rememberInput (device); });

        window_ = std::make_unique<MainWindow> (getApplicationName() + " " + getApplicationVersion(), content, *log_);
        log_->write (LogLevel::info, "startup", "Main window shown");

        midi_->start();
        log_->write (LogLevel::info, "startup", "Startup complete");
    }

    void shutdown() override
    {
        if (log_ != nullptr)
            log_->write (LogLevel::info, "session", "Shutdown begin");
        midi_.reset();
        window_.reset();
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
    void rememberInput (const midi_io::MidiDeviceInfo& device)
    {
        if (settings_.midiInput.deviceId == device.identifier && settings_.midiInput.lastSeenName == device.name)
            return;
        settings_.midiInput = { device.identifier, device.name };
        if (const auto error = settings::saveToFile (settingsFile(), settings_); ! error.empty())
            log_->write (LogLevel::error, "settings", error);
        else
            log_->write (LogLevel::info, "settings", "Remembered MIDI input", { { "device", device.name } });
    }

    std::unique_ptr<diagnostics::RotatingLog> log_;
    settings::Settings settings_;
    std::unique_ptr<MidiInputService> midi_;
    std::unique_ptr<MainWindow> window_;
};

} // namespace pianoassist::app

START_JUCE_APPLICATION (pianoassist::app::PianoAssistApplication)
