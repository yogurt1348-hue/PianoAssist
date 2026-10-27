#pragma once

#include "ui/SoundView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** One track of the song as the mixer and the piano roll show it. */
struct TrackRowView
{
    std::string label;       ///< "Track 2 'Piano' · ch 1"
    std::string trackName;   ///< as in the file, may be empty
    std::string instrument;  ///< General MIDI name
    std::string detail;      ///< notes, range, why the role was guessed
    int track = 0;           ///< 1-based
    int channel = 1;         ///< 1-based
    int assignment = 3;      ///< 0 right, 1 left, 2 both (split at C4), 3 accompaniment, 4 excluded
    bool guessed = false;
    int soundId = 0;         ///< SoundView ids, 0 = main sound
    double volume = 1.0;
    bool muted = false;
    bool solo = false;
    bool hasEditor = false;  ///< the track has its own plug-in with a window
    std::string presetName;  ///< the preset chosen instead of the file's instrument, empty when none
    bool presetDrums = false;
    std::string soundProblem; ///< the chosen sound or preset is missing

    std::uint32_t colour = 0xff7fcf95; ///< 0xAARRGGBB, what the track is drawn in
    bool autoColour = true;  ///< the colour comes from the instrument
    bool visible = true;     ///< drawn on the piano roll (the "eye"); never changes sound or practice
    bool heard = true;       ///< the app plays it now: Mute, Solo, role and the accompaniment switches together
    bool checked = false;    ///< its notes are required while practising (played by the learner)
    int order = 0;           ///< place in the mixer

    /** Why the track is quiet: what the strip and the roll explain. */
    enum Quiet { audible = 0, mutedQuiet, otherSolo, backingOff, otherHandOff, playedByYou, excludedQuiet };
    int quiet = audible;

    juce::String displayName() const
    {
        return trackName.empty() ? juce::String::fromUTF8 (instrument.c_str()) : juce::String::fromUTF8 (trackName.c_str());
    }
};

/** One preset the track's source offers (SoundFont bank and program, or a VST3 program). */
struct PresetView
{
    int bank = -1;
    int program = 0;
    std::string name;
    bool drums = false;
};

/** Main output, for the mixer's Master strip. */
struct MasterView
{
    double gain = 0.8;   ///< main volume 0-1
    float peakLeft = 0.0f, peakRight = 0.0f; ///< measured output level since the last update, 0-1
};

/** "Tracks and instruments" as a mixer: Master on the left, then one
    vertical channel per track (colour, name, Mute, Solo, volume fader, MIDI
    activity), and an inspector for the selected track on the right.

    Role decides who plays a track and whether its notes are checked; Mute
    and Solo only decide what is heard; the eye only what the roll shows. */
class TracksComponent final : public juce::Component, private juce::Timer
{
public:
    struct Intents
    {
        std::function<void (std::size_t part, int assignment)> assignmentChanged;
        std::function<void (std::size_t part, int soundId)> soundChanged;
        std::function<void (std::size_t part, double volume)> volumeChanged;
        std::function<void (std::size_t part, bool muted)> muteChanged;
        std::function<void (std::size_t part, bool solo)> soloChanged;
        std::function<void (std::size_t part)> openEditor; ///< the track's own plug-in window
        std::function<void()> openMainEditor;
        std::function<void()> openSoundSettings;
        std::function<void (int part)> selectPart;          ///< -1 = none; changes nothing that sounds
        std::function<void (std::size_t part, std::optional<std::uint32_t> colour)> colourChanged; ///< nullopt = automatic
        std::function<void (std::size_t part, bool visible)> visibleChanged;
        std::function<void (std::size_t part)> showOnly;
        std::function<void()> showAll;
        std::function<void()> clearSolos;
        std::function<void (std::size_t part, int delta)> moveTrack; ///< in the mixer order
        std::function<void (double gain)> masterChanged;
        std::function<void (bool shown)> inspectorShownChanged;
        std::function<void (std::size_t part, int bank, int program)> presetChosen;
        std::function<void (std::size_t part)> useMidiInstrument;
    };

    explicit TracksComponent (Intents intents);
    ~TracksComponent() override;

    void setRows (std::vector<TrackRowView> rows);
    void setSelected (int part);
    void setSound (const SoundView& view);
    void setMaster (const MasterView& view);
    /** Per track: a note of it is playing now (from the song, not audio). */
    void setActivity (const std::vector<bool>& active);
    void setInspectorShown (bool shown);
    /** Presets of the selected track's source. sourceKind: main, builtIn, soundFont, plugin. */
    void setPresets (int part, std::vector<PresetView> presets, std::string sourceKind);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Short text for the side panel: who plays what. */
    static juce::String summary (const std::vector<TrackRowView>& rows);
    static juce::String roleName (int assignment);
    static juce::String quietText (const TrackRowView& row);

    static constexpr int stripWidth = 128;
    static constexpr int inspectorWidth = 320;

private:
    class Strip;
    class MasterStrip;
    class Inspector;
    friend class Strip;
    friend class Inspector;

    void timerCallback() override;
    void rebuildStrips();
    void showColourMenu (std::size_t part, juce::Component& anchor);
    void showStripMenu (std::size_t part);
    void fillSoundBox (juce::ComboBox& box) const;
    juce::String soundName (int soundId) const;
    juce::String routingText (std::size_t part) const;

    Intents intents_;
    std::vector<TrackRowView> rows_;
    std::vector<bool> activity_;
    SoundView sound_;
    int selected_ = -1;
    bool inspectorShown_ = true;

    juce::Label hint_;
    juce::TextButton clearSolos_, showAll_, inspectorToggle_;
    std::unique_ptr<MasterStrip> master_;
    juce::Viewport stripsView_;
    juce::Component stripsContent_;
    juce::OwnedArray<Strip> strips_;
    std::unique_ptr<Inspector> inspector_;
    juce::Viewport inspectorView_;
    int presetsPart_ = -1;
    std::vector<PresetView> presets_;
    std::string presetsSource_;
    juce::Label empty_;
};

} // namespace pianoassist::ui
