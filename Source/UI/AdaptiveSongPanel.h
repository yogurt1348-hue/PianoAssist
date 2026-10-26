// AdaptiveSongPanel — the minimal UI of Adaptive Song Mode.
//
//   Load MIDI (+ optional separate backing MIDI), pick the Reference track
//   (what you play) and the Backing tracks (what Cadenza plays), MIDI input,
//   start behaviour, a few tuning sliders, Play/Stop and live status
//   (tracking state, position, performer/band BPM, confidence, phase error).
//
// It talks to the engine through AdaptiveSongEngine and to the rest of the app
// through Callbacks (mode switching, MIDI input selection, file choosers).

#pragma once

#include "../Adaptive/AdaptiveSongEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace cadenza::ui
{
class AdaptiveSongPanel final : public juce::Component,
                                private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<cadenza::adaptive::AdaptiveSongEngine*()> engine;   // nullptr once the app is gone
        std::function<void(bool)> setModeActive;          // enter/leave Adaptive Song Mode
        std::function<juce::StringArray()> midiInputs;
        std::function<juce::String()> selectedMidiInput;  // "" = auto
        std::function<void(const juce::String&)> selectMidiInput;
        std::function<std::uint16_t()> reservedChannels;  // live keyboard output channels (bit c-1)
    };

    explicit AdaptiveSongPanel(Callbacks callbacks);
    ~AdaptiveSongPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseMidi(bool backing);
    void refreshTracks();
    void rebuild();
    void togglePlay();
    void exportLog();
    cadenza::adaptive::AdaptiveSessionConfig configFromControls() const;

    Callbacks m_cb;
    std::unique_ptr<juce::FileChooser> m_chooser;

    juce::TextButton m_loadMidi { "Load MIDI..." }, m_loadBacking { "Separate backing MIDI..." }, m_clearBacking { "X" };
    juce::Label m_fileLabel, m_backingLabel;
    juce::Label m_refCaption, m_refChannelCaption, m_backingCaption, m_inputCaption, m_startCaption;
    juce::ComboBox m_refTrack, m_refChannel, m_input, m_startMode, m_alignment;
    juce::Viewport m_backingView;
    juce::Component m_backingList;
    std::vector<std::unique_ptr<juce::ToggleButton>> m_backingToggles;
    std::vector<int> m_backingTrackIndex;

    juce::Label m_smoothingCaption, m_sensitivityCaption, m_waitCaption;
    juce::Slider m_smoothing, m_sensitivity, m_wait;
    juce::ToggleButton m_diagnostics { "Diagnostics log" };
    juce::TextButton m_openLog { "Open log folder" }, m_exportLog { "Save log to Desktop" };

    juce::TextButton m_play { "Play" };
    juce::Label m_state, m_position, m_bpm, m_confidence, m_phase, m_notes, m_warnings;
    double m_confidenceValue = 0.0;
    bool m_wasPlaying = false;
};
}
