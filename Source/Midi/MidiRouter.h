// MidiRouter — JUCE adapter around arranger::ArrangerMidiRouter.
//
// Owns a juce::MidiInput and forwards every incoming juce::MidiMessage to
// the pure-C++ ArrangerMidiRouter, which handles:
//   * chord-zone vs melody-zone split (with Full-Keyboard modes)
//   * 18-template chord recognition + slash chords
//   * Syncro Start/Stop events (first chord note in / last chord note out)
//   * Chord Memory
//   * Per-note reference counting (handles duplicate note-on events)
//
// This wrapper only adapts JUCE I/O. All MIDI/arranger logic lives in
// cadenza_core where it's fully unit-tested.

#pragma once

#include "ChordRecognizer.h"
#include "ArrangerMidiRouter.h"
#include "LiveMelodyVoice.h"
#include "RightHand.h"
#include "MidiControlMap.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace cadenza::midi
{
// Snapshot fired on every incoming MIDI message — used for the debug panel.
// Captures the raw message + the router's interpretation of it.
struct MidiDebugEvent
{
    std::string deviceName;
    int status = 0;        // raw status byte (0x80..0xFF), 0 if message had no bytes
    int note = -1;         // MIDI note number, -1 if not a note message
    int velocity = -1;     // 0..127 for note-on/off, -1 otherwise
    std::string route;     // "chord", "melody", "ignored"
    std::string chordName; // current detected chord display name; "" if none
    std::string sync;      // "start" if syncro just started, "stop" if just stopped, "none" otherwise
};

class MidiRouter final : public juce::MidiInputCallback
{
public:
    using NoteCallback  = std::function<void(int channel, int note, int velocity, bool isNoteOn)>;
    using ChordCallback = std::function<void(const std::optional<Chord>& chord, const std::string& displayName)>;
    using SyncCallback  = std::function<void(bool started)>;  // true = started, false = stopped
    using DebugCallback = std::function<void(const MidiDebugEvent&)>;

    MidiRouter();
    ~MidiRouter() override;

    bool openInput(const juce::String& deviceName);
    void closeInputs();
    juce::StringArray availableInputs() const;

    // Open the MIDI input device(s) that should be active and aren't yet (and log
    // it). Safe to call repeatedly — used at startup and periodically for hot-plug
    // so a keyboard plugged in after launch is picked up. Which devices count as
    // "should be active" depends on the selected input (see setSelectedInput).
    // Returns the number of inputs now open.
    int refreshInputs();

    // Choose which input port to use. Empty string = AUTO: open the main keyboard
    // port(s) and ignore Windows secondary ports ("MIDIIN2 (Device)", etc.) that
    // carry pad / DAW-control data and would pollute chord detection. A non-empty
    // name opens exactly that one port. Applies immediately (resets held notes).
    void setSelectedInput(const juce::String& deviceName);
    juce::String selectedInput() const { return m_selectedInput; }

    // True for Windows secondary MIDI ports ("MIDIIN2 (Device)", "MIDIIN3 (...)").
    static bool isAuxPort(const juce::String& deviceName);

    void setSplitPoint(int midiNote) noexcept;
    int  splitPoint() const noexcept;

    // Live Octave control: shifts the primary right-hand voice (Right 1) by N
    // octaves. Does not affect chord detection or style/accompaniment playback.
    void setLiveOctave(int octaves) noexcept { m_rightHand.setLayerOctave(0, octaves); }
    int  liveOctave() const noexcept { return m_rightHand.layerOctave(0); }

    // Global transpose: shifts every right-hand layer by N semitones, matching the
    // accompaniment transpose. Chord detection is unaffected.
    void setLiveTranspose(int semitones) noexcept { m_rightHand.setTranspose(semitones); m_leftVoice.setTranspose(semitones); }
    int  liveTranspose() const noexcept { return m_rightHand.transpose(); }

    // Left-hand split voice: sounds the keys BELOW the split point (chord zone) on
    // its own channel, alongside chord detection. Off by default.
    void setLeftEnabled(bool on) noexcept { m_leftEnabled.store(on); }
    bool leftEnabled() const noexcept { return m_leftEnabled.load(); }
    void setLeftChannel(int channel) noexcept { m_leftVoice.setChannel(channel); }
    int  leftChannel() const noexcept { return m_leftVoice.channel(); }
    void setLeftOctave(int octaves) noexcept { m_leftVoice.setOctave(octaves); }
    int  leftOctave() const noexcept { return m_leftVoice.octave(); }

    // Dedicated Cadenza channel the primary right-hand voice (Right 1) plays on.
    int  melodyChannel() const noexcept { return m_rightHand.layerChannel(0); }
    void setMelodyChannel(int channel) noexcept { m_rightHand.setLayerChannel(0, channel); }

    // Right 1 / Right 2 / Right 3 layered voices (layer 0..2). Right 1 is on by
    // default; enabling a layer stacks its voice on top of the right hand.
    static constexpr int kNumRightLayers = RightHand::kNumLayers;
    void setRightLayerEnabled(int layer, bool on) noexcept { m_rightHand.setLayerEnabled(layer, on); }
    bool rightLayerEnabled(int layer) const noexcept { return m_rightHand.layerEnabled(layer); }
    void setRightLayerChannel(int layer, int ch) noexcept { m_rightHand.setLayerChannel(layer, ch); }
    int  rightLayerChannel(int layer) const noexcept { return m_rightHand.layerChannel(layer); }
    void setRightLayerOctave(int layer, int oct) noexcept { m_rightHand.setLayerOctave(layer, oct); }
    int  rightLayerOctave(int layer) const noexcept { return m_rightHand.layerOctave(layer); }

    // Play an on-screen / virtual-keyboard note through the live melody voice so
    // it gets the same Octave shift, dedicated channel and matched note-off as a
    // hardware key. Notes at/above the split point are treated as melody; this
    // does NOT feed chord detection. Returns the synth event to play, or nullopt
    // (e.g. a below-split note, which the caller may sound directly). Thread-safe.
    std::vector<LiveMelodyEvent> handleVirtualMelodyNote(int note, int velocity, bool isOn);

    // Inject a note from a native on-screen keyboard, running the SAME path as a
    // hardware key: split routing, chord detection (below split), and the live
    // melody voice (above split). Fires the note + chord callbacks. Thread-safe.
    void injectNote(int note, int velocity, bool isOn);

    // Pass-through to the underlying arranger router.
    void setChordDetectionMode(arranger::ChordDetectionMode mode) noexcept;
    void setChordMemory(bool enabled) noexcept;
    void setSyncroStopOnRelease(bool enabled) noexcept;

    // --- Style Recorder capture mode ---
    // While enabled, every incoming note (hardware or on-screen) bypasses the
    // split/chord/melody routing and goes to the capture callback instead, so
    // the whole keyboard plays/records the part being recorded. The callback
    // fires on the MIDI thread.
    using CaptureCallback = std::function<void(int note, int velocity, bool isNoteOn)>;
    void setCaptureCallback(CaptureCallback cb) { m_onCapture = std::move(cb); }
    void setCaptureMode(bool on) noexcept { m_captureMode.store(on); }
    bool captureMode() const noexcept { return m_captureMode.load(); }

    // --- Adaptive Song Mode ---
    // Raw tap: every played note (hardware or on-screen) with its timestamp in
    // seconds on the juce::Time::getMillisecondCounterHiRes() clock, before any
    // split/chord routing. Fires on the MIDI thread; install once at startup.
    using RawNoteCallback = std::function<void(int note, int velocity, bool isOn, double timeSec)>;
    void setRawNoteCallback(RawNoteCallback cb) { m_onRawNote = std::move(cb); }
    // Performance mode: the whole keyboard plays the Right 1/2/3 voices and the
    // arranger side (split, chord detection, Syncro Start/Stop, left voice) is
    // bypassed, so a pianist can play a full part over an adaptive backing.
    void setPerformanceMode(bool on) noexcept { m_performanceMode.store(on); }
    bool performanceMode() const noexcept { return m_performanceMode.load(); }
    // Piano pedals (CC64 sustain, CC66 sostenuto, CC67 soft) from the keyboard,
    // forwarded only in performance mode (the arranger path is unchanged).
    using PedalCallback = std::function<void(int controller, int value)>;
    void setPedalCallback(PedalCallback cb) { m_onPedal = std::move(cb); }

    // Returns the display name of the currently-detected chord, or "" if none.
    // Thread-safe; acquires the internal publish mutex.
    std::string currentChordDisplayName() const;

    // Pitch-bend / modulation wheels from the hardware keyboard (forwarded so the
    // app can apply them to the manual voice channels). Fire on the MIDI thread.
    using WheelCallback = std::function<void(int)>;
    void setPitchBendCallback(WheelCallback cb)  { m_onPitchBend  = std::move(cb); }
    void setModulationCallback(WheelCallback cb) { m_onModulation = std::move(cb); }

    void setNoteCallback(NoteCallback cb)   { m_onNote  = std::move(cb); }
    void setChordCallback(ChordCallback cb) { m_onChord = std::move(cb); }
    void setSyncCallback(SyncCallback cb)   { m_onSync  = std::move(cb); }
    void setDebugCallback(DebugCallback cb) { m_onDebug = std::move(cb); }

    // --- MIDI control mapping (hardware buttons -> arranger actions) ---
    // m_onControl fires a command string ("mainA", "fillAA", "play", ...) from a
    // mapped button. m_onControlLearn reports the next pressed trigger while armed.
    // Both fire on the MIDI thread — marshal to the message thread in the handler.
    using ControlCallback = std::function<void(const std::string& command)>;
    using ControlLearnCallback = std::function<void(int trigger)>;
    void setControlCallback(ControlCallback cb)      { m_onControl = std::move(cb); }
    void setControlLearnCallback(ControlLearnCallback cb) { m_onControlLearn = std::move(cb); }
    void armControlLearn(bool on) noexcept           { m_learnArmed.store(on); }
    void setControlMap(std::map<int, std::string> entries) {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        m_controlMap.setEntries(std::move(entries));
    }

    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg) override;

private:
    // Translate arranger::ChordRecognitionResult into the cadenza::midi::Chord
    // struct already consumed by StyleEngine. Non-static: the slash-chord bass
    // is only published in FingeredOnBass mode (needs the router's mode).
    std::optional<Chord> toCadenzaChord(const std::optional<arranger::ChordRecognitionResult>& result) const;
    static ChordQuality toCadenzaQuality(const std::string& s);

    juce::OwnedArray<juce::MidiInput> m_inputs;
    juce::StringArray m_openIdentifiers;       // identifiers of devices already opened
    juce::String m_selectedInput;              // "" = auto (main port, skip aux ports)
    juce::String m_lastDeviceSignature;        // to log the device list only when it changes

    arranger::ArrangerMidiRouter m_router;
    mutable std::mutex m_publishMutex;

    std::string  m_lastChordName;
    RightHand    m_rightHand;
    LiveMelodyVoice   m_leftVoice;             // left-hand split voice (chord zone)
    std::atomic<bool> m_leftEnabled { false };
    WheelCallback m_onPitchBend;
    WheelCallback m_onModulation;
    NoteCallback  m_onNote;
    ChordCallback m_onChord;
    SyncCallback  m_onSync;
    DebugCallback m_onDebug;

    MidiControlMap m_controlMap;            // guarded by m_publishMutex
    std::atomic<bool> m_learnArmed { false };
    std::atomic<bool> m_captureMode { false };
    std::atomic<bool> m_performanceMode { false };
    RawNoteCallback m_onRawNote;
    PedalCallback m_onPedal;
    CaptureCallback m_onCapture;
    ControlCallback m_onControl;
    ControlLearnCallback m_onControlLearn;
};
}
