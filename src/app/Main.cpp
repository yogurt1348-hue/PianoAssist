#include "app/AppPaths.h"
#include "diagnostics/RotatingLog.h"
#include "diagnostics/SessionReport.h"
#include "settings/Settings.h"
#include "ui/MainComponent.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;

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
    }
    if (result.status != settings::LoadStatus::loaded)
        if (const auto error = settings::saveToFile (path, result.settings); ! error.empty())
            log.write (LogLevel::error, "settings", error);

    log.write (LogLevel::info, "settings", "Settings ready",
               { { "storedSchemaVersion", std::to_string (result.storedSchemaVersion) },
                 { "practiceProfile", result.settings.practiceProfile.name } });
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
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (
                              juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ui::MainComponent(), true);
        setResizable (true, true);
        setResizeLimits (640, 400, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
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
        log_ = std::make_unique<diagnostics::RotatingLog> (diagnostics::RotatingLog::Options { logDirectory() });
        settings_ = loadSettings (*log_);
        diagnostics::logSessionStart (*log_, describeSession (settings_));
        window_ = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        window_.reset();
        if (log_ != nullptr)
            log_->write (LogLevel::info, "session", "Session ended");
        log_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<diagnostics::RotatingLog> log_;
    settings::Settings settings_;
    std::unique_ptr<MainWindow> window_;
};

} // namespace pianoassist::app

START_JUCE_APPLICATION (pianoassist::app::PianoAssistApplication)
