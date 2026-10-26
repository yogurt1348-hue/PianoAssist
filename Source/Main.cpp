#include "Diagnostics/AppLog.h"
#include "MainComponent.h"
#include "UI/CadenzaLookAndFeel.h"

#include <juce_gui_extra/juce_gui_extra.h>

class CadenzaApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Cadenza"; }
    const juce::String getApplicationVersion() override { return CADENZA_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        // Route all juce::Logger::writeToLog(...) output to a readable file at
        // %APPDATA%/Cadenza/cadenza.log (a fresh file each launch; the previous
        // run is kept as cadenza-previous.log) so playback / MIDI / mixer
        // behaviour can be diagnosed after the fact.
        cadenza::diagnostics::rotateLogs();
        m_logger.reset(new juce::FileLogger(cadenza::diagnostics::currentLogFile(),
                                            "=== Cadenza session start === version " + getApplicationVersion()
                                                + ", " + juce::SystemStats::getOperatingSystemName()));
        juce::Logger::setCurrentLogger(m_logger.get());

        // Install the app-wide "sleek dark DAW" theme before any window is built
        // so it cascades to every component and popup menu.
        m_lookAndFeel = std::make_unique<cadenza::ui::CadenzaLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(m_lookAndFeel.get());

        m_mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        m_mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        m_lookAndFeel.reset();
        juce::Logger::setCurrentLogger(nullptr);
        m_logger.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(std::move(name),
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<juce::FileLogger> m_logger;
    std::unique_ptr<cadenza::ui::CadenzaLookAndFeel> m_lookAndFeel;
};

START_JUCE_APPLICATION(CadenzaApplication)

