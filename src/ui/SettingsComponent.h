#pragma once

#include "ui/InputMonitorView.h"
#include "ui/MainComponent.h"
#include "ui/SoundView.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace pianoassist::ui
{

/** The settings window's content: five pages. Every change is applied and
    saved at once; there is no OK/Cancel. */
class SettingsComponent final : public juce::Component
{
public:
    struct Values
    {
        int tolerance = 1;
        bool waitForNotes = false;
        double inputOffsetMs = 0.0;
        bool lightTheme = false;
        double uiScale = 1.0;
        std::string language = "ru";
        double pixelsPerSecond = 120.0;
        std::string libraryFolder;   ///< UTF-8
        std::string dataFolder;      ///< UTF-8, read-only information
        bool portable = false;
        bool includeTestBuilds = false;
        std::string version;
        std::string audioDescription;
    };

    struct Intents
    {
        std::function<void (const std::string& identifier)> selectInput;
        std::function<void()> rescanInputs;
        std::function<void (double ms)> inputOffsetChanged;
        std::function<void()> audioDeviceChanged; ///< save the output choice
        std::function<void (int soundId)> soundChosen;
        std::function<void()> loadSoundFont;
        std::function<void()> addPluginFile;
        std::function<void()> addPluginFolder;
        std::function<void (std::size_t index)> removePluginFolder;
        std::function<void()> scanPlugins;
        std::function<void()> rescanAllPlugins;
        std::function<void()> showInstrumentWindow;
        std::function<void (int tolerance)> toleranceChanged;
        std::function<void (bool wait)> waitForNotesChanged;
        std::function<void()> chooseLibraryFolder;
        std::function<void()> openLibraryFolder;
        std::function<void (bool light)> themeChanged;
        std::function<void (double scale)> uiScaleChanged;
        std::function<void (const std::string& language)> languageChanged;
        std::function<void (double pixelsPerSecond)> zoomChanged;
        std::function<void (bool include)> includeTestBuildsChanged;
        std::function<void()> checkForUpdates;
        std::function<void()> copyDiagnostics;
        std::function<void()> saveDiagnostics;
        std::function<void()> openLogFolder;
        std::function<void()> openDataFolder;
    };

    SettingsComponent (Intents intents, juce::AudioDeviceManager& devices);
    ~SettingsComponent() override;

    void showPage (SettingsPage page);
    void setValues (const Values& values);
    void setInputMonitor (const InputMonitorView& view);
    void setSound (const SoundView& view);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    static constexpr int sideWidth = 200;

    class Page;
    class MidiAudioPage;
    class SoundsPage;
    class PracticePage;
    class AppearancePage;
    class DiagnosticsPage;

    Intents intents_;
    std::unique_ptr<MidiAudioPage> midiAudio_;
    std::unique_ptr<SoundsPage> sounds_;
    std::unique_ptr<PracticePage> practice_;
    std::unique_ptr<AppearancePage> appearance_;
    std::unique_ptr<DiagnosticsPage> diagnostics_;
    std::array<Page*, 5> pages_ {};
    std::array<juce::TextButton, 5> pageButtons_;
    int current_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
};

} // namespace pianoassist::ui
