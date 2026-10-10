// AdaptiveSongEngine — JUCE-side owner of Adaptive Song Mode.
//
// Wires the pure-C++ AdaptiveSongSession into the app:
//   * AudioEngine block hook  -> session.processBlock (audio thread), with an
//     AudioEngine-backed MidiEventSink so backing MIDI reaches the existing
//     FluidSynth / per-part VST3 / master-instrument rendering unchanged;
//   * MidiRouter raw-note tap -> session.onLiveNote (MIDI thread);
//   * file loading, song building and diagnostics logging (message thread).

#pragma once

#include "AdaptiveSong.h"
#include "AdaptiveSongSession.h"
#include "MidiFile.h"
#include "../Audio/AudioEngine.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace cadenza::adaptive
{
class AdaptiveSongEngine
{
public:
    explicit AdaptiveSongEngine(cadenza::audio::AudioEngine& audio);

    // Install the audio-thread hook. Call once, before the audio device starts.
    void install();

    // Clock shared by MIDI timestamps and the audio hook (seconds).
    static double nowSeconds() noexcept;

    // ---- message thread ----
    bool loadMidi(const juce::File& file, juce::String& error);          // the song / reference file
    bool loadBackingMidi(const juce::File& file, juce::String& error);   // optional separate backing file
    void clearBackingMidi();
    bool hasMidi() const noexcept { return m_midi != nullptr; }
    bool hasSeparateBacking() const noexcept { return m_backingMidi != nullptr; }
    const MidiFileData* midi() const noexcept { return m_midi.get(); }
    const MidiFileData* backingMidi() const noexcept { return m_backingMidi.get(); }
    juce::String midiName() const { return m_midiName; }
    juce::String backingName() const { return m_backingName; }

    // Build the song from the current file(s) and hand it to the session (stops playback).
    AdaptiveSongBuildResult apply(const AdaptiveSongOptions& options);
    bool songReady() const noexcept { return m_session.loaded(); }

    void setConfig(const AdaptiveSessionConfig& config) { m_session.setConfig(config); }
    const AdaptiveSessionConfig& config() const noexcept { return m_session.config(); }

    void play();
    void stop();
    bool playRequested() const noexcept { return m_session.playRequested(); }
    AdaptiveSongStatus status() const { return m_session.status(nowSeconds()); }

    // Periodic (20 Hz timer): follower timeouts + diagnostics / state logging.
    void timerTick();
    // Per-note development diagnostics in the log. Default: on in debug builds or
    // when the environment variable CADENZA_ADAPTIVE_DEBUG=1 is set.
    void setDiagnosticsLogging(bool on);
    bool diagnosticsLogging() const noexcept { return m_logDiagnostics; }

    // ---- MIDI thread ----
    void onLiveNote(int note, int velocity, bool isOn, double timeSec)
    {
        m_session.onLiveNote(note, velocity, isOn, timeSec);
    }

private:
    class EngineSink final : public MidiEventSink
    {
    public:
        explicit EngineSink(cadenza::audio::AudioEngine& a) : m_audio(a) {}
        void noteOn(int ch, int note, int vel) noexcept override { m_audio.noteOn(ch, note, vel); }
        void noteOff(int ch, int note) noexcept override { m_audio.noteOff(ch, note); }
        void controlChange(int ch, int cc, int v) noexcept override { m_audio.controlChange(ch, cc, v); }
        void programChange(int ch, int p) noexcept override { m_audio.programChange(ch, p); }
        void pitchBend(int ch, int v) noexcept override { m_audio.pitchBend(ch, v); }

    private:
        cadenza::audio::AudioEngine& m_audio;
    };

    static std::unique_ptr<MidiFileData> readMidi(const juce::File& file, juce::String& error);

    cadenza::audio::AudioEngine& m_audio;
    EngineSink m_sink;
    AdaptiveSongSession m_session;
    std::unique_ptr<MidiFileData> m_midi;
    std::unique_ptr<MidiFileData> m_backingMidi;
    juce::String m_midiName, m_backingName;
    bool m_logDiagnostics = false;
    TrackingState m_loggedState = TrackingState::Idle;
};
}
