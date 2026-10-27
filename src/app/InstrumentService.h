#pragma once

#include "app/AudioService.h"
#include "app/PluginScanner.h"
#include "diagnostics/RotatingLog.h"
#include "settings/Settings.h"
#include "ui/SoundView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace pianoassist::audio
{
class SoundFontRenderer;
}

namespace pianoassist::app
{

/** Chooses what the song sounds like. The main sound plays every channel
    unless a track has its own: the built-in sound, a SoundFont (.sf2), or a
    VST3 instrument. Each track with its own VST3 gets a separate instance;
    tracks left on the main sound reach it on their own MIDI channel, so a
    multitimbral instrument can be set up per channel in its window.

    VST3 files are scanned in a helper process (see PluginScanner), from the
    standard folders and the user's own. Remembers the main choice globally
    and the plug-in's settings per plug-in; track choices and their plug-in
    states belong to the song and are handed out for saving. Message thread only. */
class InstrumentService final
{
public:
    /** Track sound for the song library: choice plus the plug-in's state. */
    struct TrackSound
    {
        settings::SoundSettings choice; ///< kind "main" follows the main sound
        juce::MemoryBlock state;        ///< VST3 state, empty when none
    };

    InstrumentService (diagnostics::RotatingLog& log, AudioService& audio, settings::SoundSettings& settings,
                       std::function<void()> saveSettings);
    ~InstrumentService();

    /** Restores the remembered main sound; falls back to the built-in one. */
    void restore();
    /** Saves the main plug-in's own settings. Call before shutdown. */
    void saveState();

    void chooseMain (int soundId); ///< SoundView ids
    void chooseSoundFont (const std::filesystem::path& path);
    void addPluginFile (const juce::File& file);
    void startScan();
    /** Forgets skipped and known plug-ins and scans everything again. */
    void rescanAll();
    void addScanFolder (const juce::File& folder);
    void removeScanFolder (std::size_t index);
    /** -1 opens the main instrument's window, otherwise the track's. */
    void showEditor (int part = -1);

    /** A new song: its tracks' MIDI channels and saved sounds (may be empty). */
    void setSong (std::vector<int> channels, std::vector<TrackSound> sounds);
    void choosePart (std::size_t part, int soundId); ///< 0 = main sound
    /** A preset instead of the file's instrument for this track: SoundFont
        bank and program, or a VST3 program (bank ignored). Only this track
        changes; its notes and the MIDI file stay as they are. */
    void choosePartPreset (std::size_t part, int bank, int program);
    /** Back to what the MIDI file selects, program changes included. */
    void useMidiInstrument (std::size_t part);

    struct Preset
    {
        int bank = -1;      ///< SoundFont bank; -1 for VST3 programs
        int program = 0;
        std::string name;
        bool drums = false;
    };
    /** What the track's source offers: SoundFont presets, or the VST3's own
        program list (empty when the plug-in chooses sounds in its window). */
    std::vector<Preset> partPresets (std::size_t part) const;
    /** The track's chosen preset, or nullopt when it follows the MIDI file. */
    std::optional<Preset> partPreset (std::size_t part) const;
    /** Source of every track, for the routing text: "main", "builtIn", "soundFont", "plugin". */
    std::string partSourceName (std::size_t part) const;
    std::string partProblem (std::size_t part) const;
    std::vector<int> partSoundIds() const;           ///< for the tracks list, 0 = main
    std::vector<bool> partHasEditor() const;         ///< tracks with their own plug-in window
    /** Current choices and plug-in states, for the song library. */
    std::vector<TrackSound> trackSounds() const;

    ui::SoundView view() const;
    std::function<void()> onChange;
    std::function<void()> onTrackSoundsChanged;

private:
    struct Slot
    {
        settings::SoundSettings choice;
        int handle = AudioService::builtInHandle; ///< what actually plays
        bool ownsHandle = false;                   ///< a plug-in instance of its own
        std::string problem;
        /** The saved plug-in state of a choice that could not load (plug-in
            missing or failing). Kept so saving the song does not lose it. */
        juce::MemoryBlock keptState;
    };

    void instruments (std::vector<juce::PluginDescription>& out) const;
    const juce::PluginDescription* findPlugin (const std::string& id) const;
    int soundIdOf (const settings::SoundSettings& choice) const;
    settings::SoundSettings choiceFor (int soundId) const;
    /** Makes the slot play its choice; returns false and sets problem on failure. */
    bool realise (Slot& slot, const juce::MemoryBlock* state, bool drums, bool ownInstance = false);
    /** Makes the track play choice: the new instance is ready before the old one goes. */
    void replacePart (std::size_t part, settings::SoundSettings choice, const juce::MemoryBlock* state);
    audio::SoundFontRenderer* sharedSoundFont (const std::string& path) const;
    void release (Slot& slot);
    int soundFontHandle (const std::filesystem::path& path, std::string& error);
    void releaseUnusedSoundFonts();
    void applyRouting();
    void closeEditor();
    void saveKnownList();
    juce::StringArray scanFolders() const;
    juce::MemoryBlock readStateFile (const std::string& pluginId) const;
    void changed (std::string status);

    diagnostics::RotatingLog& log_;
    AudioService& audio_;
    settings::SoundSettings& settings_;
    std::function<void()> saveSettings_;

    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList known_;
    PluginScanner scanner_;
    std::vector<std::string> problems_;
    std::string scanProgress_;
    int added_ = 0;

    Slot main_;
    std::vector<Slot> parts_;
    std::vector<int> channels_;
    std::map<std::string, int> soundFonts_; ///< path -> handle, shared by every slot using it
    std::string soundFontPath_;             ///< the SoundFont offered in the lists
    std::string soundFontName_;

    std::unique_ptr<juce::DocumentWindow> editorWindow_;
    int editorPart_ = -2;
    std::string status_;
};

} // namespace pianoassist::app
