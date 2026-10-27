#pragma once

#include "app/AudioService.h"
#include "diagnostics/RotatingLog.h"
#include "settings/Settings.h"
#include "ui/SoundView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <memory>

namespace pianoassist::app
{

/** Chooses what the song sounds like: the built-in synth, a SoundFont (.sf2)
    or a VST3 instrument. Finds VST3 instruments in the standard folders,
    remembers the choice and the plug-in's own settings, and shows the
    plug-in's window. Message thread only.

    Scanning loads each plug-in inside this process. A file that crashes the
    scan is remembered (dead man's pedal) and skipped next time. */
class InstrumentService final : private juce::Timer
{
public:
    InstrumentService (diagnostics::RotatingLog& log, AudioService& audio, settings::SoundSettings& settings,
                       std::function<void()> saveSettings);
    ~InstrumentService() override;

    /** Restores the remembered instrument; falls back to the built-in synth. */
    void restore();
    /** Saves the plug-in's own settings. Call before shutdown. */
    void saveState();

    void chooseBuiltIn();
    void chooseSoundFont (const std::filesystem::path& path);
    void choosePlugin (std::size_t index);
    void addPluginFile (const juce::File& file);
    void startScan();
    void showEditor();

    ui::SoundView view() const;
    std::function<void()> onChange;

private:
    void timerCallback() override;
    bool loadPlugin (const juce::PluginDescription& description);
    void instruments (std::vector<juce::PluginDescription>& out) const;
    void closeEditor();
    void saveKnownList();
    void changed (std::string status);

    diagnostics::RotatingLog& log_;
    AudioService& audio_;
    settings::SoundSettings& settings_;
    std::function<void()> saveSettings_;

    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList known_;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner_;
    std::unique_ptr<juce::DocumentWindow> editorWindow_;
    std::string soundFontName_;
    std::string status_;
    int selected_ = 0; ///< SoundView ids
};

} // namespace pianoassist::app
