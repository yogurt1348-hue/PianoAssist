// AudioEngine — top-level juce::AudioSource for Cadenza.
// Owns the SynthEngine, Metronome, and Transport. Plugs into juce::AudioSourcePlayer
// which drives it from the JUCE AudioDeviceManager.

#pragma once

#include "MasterCompressor.h"
#include "MasterEq.h"
#include "MasterGlue.h"
#include "PolishedMaster.h"
#include "Metronome.h"
#include "PluginHost.h"
#include "SynthEngine.h"
#include "Transport.h"
#include "TransportCommandMailbox.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace cadenza::audio
{
class AudioEngine final : public juce::AudioSource
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // juce::AudioSource
    void prepareToPlay(int samplesPerBlock, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    // Lifecycle helpers
    void startAudioDevice();
    void stopAudioDevice();

    // Transport control
    void play();
    void stop();
    void stopFromAudioThread();

    // Fade-out: ramp the master down over `seconds`, then stop the transport
    // (audio thread) and restore the gain. The UI polls consumeFadeCompleted()
    // to sync the Play button when the fade finishes.
    void startFadeOut(double seconds);
    void cancelFadeOut();
    bool fadeActive() const noexcept { return m_fadeActive.load(); }
    bool consumeFadeCompleted() noexcept { return m_fadeCompleted.exchange(false); }
    void setBpm(double bpm);
    void setMetronomeEnabled(bool enabled) { m_metronome.setEnabled(enabled); }
    bool isMetronomeEnabled() const        { return m_metronome.isEnabled(); }

    // MIDI input — thread-safe.
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void programChange(int channel, int program);
    void controlChange(int channel, int cc, int value);
    void pitchBend(int channel, int value14);   // 14-bit, 8192 = centre
    void allNotesOff();

    // SoundFont loading (no-op for NullSynthEngine).
    bool loadSoundFont(const std::string& path);

    // Master 3-band EQ (low/mid/high gain in dB) applied to the final mix.
    void setEqGains(float lowDb, float midDb, float highDb) { m_masterEq.setGains(lowDb, midDb, highDb); }
    // Master compressor amount 0..100 (0 = bypass).
    void setCompAmount(int percent) { m_masterComp.setAmount(percent); }
    // Master output volume 0..127 (100 = unity). A final soft limiter keeps it
    // clean when pushed, so the whole mix (drums included) can hit harder.
    void setMasterVolume(int percent) { m_masterGain.store(juce::jlimit(0, 127, percent) / 100.0f); }
    // Master reverb amount 0..100 (maps to FluidSynth reverb level 0..~1.0).
    void setReverbLevel(int percent) { if (m_synth) m_synth->setReverbLevel(juce::jlimit(0, 100, percent) / 100.0); }
    // "Polished Master": optional final-stage polish (clean brickwall limiter +
    // light glue/air/width) applied after the master volume. When off, the chain
    // uses the original tanh soft-limiter unchanged.
    void setPolishedMaster(bool on) { m_polishedMaster.setEnabled(on); }
    bool polishedMaster() const { return m_polishedMaster.enabled(); }
    const char* synthEngineName() const noexcept;
    bool supportsSoundFonts() const noexcept;

    // Master insert VST3 effect (e.g. an amp/cab sim on the arranger output).
    bool loadMasterEffect(const std::string& path, std::string& error);
    void clearMasterEffect();
    bool hasMasterEffect() const;
    std::string masterEffectName() const;

    // Per-part VST3 INSTRUMENTS. A channel (1..16) with a loaded instrument has
    // its notes routed to that plugin (and its audio summed into the mix) instead
    // of the FluidSynth SoundFont. When none are loaded there is zero overhead.
    bool loadPartInstrument(int channel, const std::string& path, std::string& error);
    // Load a part instrument and restore a base64-encoded VST3 state (e.g. a
    // sforzando instance with an SFZ loaded). Idempotent on (path, state).
    bool loadPartInstrument(int channel, const std::string& path,
                            const std::string& stateBase64, std::string& error);
    // Capture the channel's current plugin state as base64 ("" if none loaded).
    std::string capturePartInstrumentState(int channel) const;
    void clearPartInstrument(int channel);
    void clearAllPartInstruments();
    bool hasPartInstrument(int channel) const;
    std::string partInstrumentName(int channel) const;
    std::string partInstrumentPath(int channel) const;   // path used to load (for persistence)
    void showPartInstrumentEditor(int channel);           // open the plugin's GUI (message thread)

    // ---- Master multitimbral instrument (one plugin for the whole band) ----
    // When loaded, EVERY part + the right hand routes to this single plugin on
    // its own MIDI channel (1..16), instead of FluidSynth / per-part instruments.
    // This is how a multitimbral module (SampleTank, Sound Canvas VA, Kontakt)
    // plays all parts at once. Falls back to the normal path when not loaded.
    bool loadMasterInstrument(const std::string& path, std::string& error);
    void clearMasterInstrument();
    bool hasMasterInstrument() const;
    std::string masterInstrumentName() const;
    std::string masterInstrumentPath() const;
    void showMasterInstrumentEditor();

    // Block callback for the style engine to push notes on the audio thread.
    using TickCallback = std::function<void(int ticksAdvanced, Transport&)>;
    void setOnTick(TickCallback cb) { m_onTick = std::move(cb); }

    // Optional per-block hook, called on the audio thread every block (playing or
    // not) right after the transport/StyleEngine events and before the synth
    // renders, so MIDI it sends is heard in the same block. Used by Adaptive Song
    // Mode, which runs its own adaptive playhead. Install once before the audio
    // device starts. Must be real-time safe.
    using BlockCallback = std::function<void(int numFrames, double sampleRate)>;
    void setOnBlock(BlockCallback cb) { m_onBlock = std::move(cb); }

    Transport& transport() noexcept { return m_transport; }
    SynthEngine& synth() noexcept   { return *m_synth; }

    // Output-device selection (so the user can avoid a coloured virtual device).
    juce::AudioDeviceManager& deviceManager() noexcept { return m_deviceManager; }
    // Restore a previously-saved device setup (createStateXml) before defaulting.
    void startAudioDevice(const juce::XmlElement* savedState);

private:
    std::unique_ptr<SynthEngine> m_synth;
    Metronome m_metronome;
    Transport m_transport;
    TransportCommandMailbox m_transportCommands;
    PluginHost m_masterEffect;
    MasterEq         m_masterEq;
    MasterCompressor m_masterComp;
    MasterGlue       m_masterGlue;
    PolishedMaster   m_polishedMaster;
    std::atomic<float> m_masterGain { 1.0f };   // master output volume (100% = unity)

    // Fade-out state. m_fadeActive / m_fadeSeconds are written by the message
    // thread; m_fadeGain is audio-thread-only; m_fadeCompleted is the handshake
    // back to the message thread.
    std::atomic<bool>  m_fadeActive { false };
    std::atomic<float> m_fadeSeconds { 8.0f };
    std::atomic<bool>  m_fadeCompleted { false };
    float m_fadeGain = 1.0f;                    // audio thread only

    // Per-part instrument hosting (channels 1..16; index 0 unused).
    static constexpr int kNumChannels = 17;
    void consumeTransportCommands();   // audio thread, before transport advance
    void renderPartInstruments(juce::AudioBuffer<float>& view);   // audio thread
    void renderMasterInstrument(juce::AudioBuffer<float>& view);  // audio thread
    void reapplyRememberedChannelPrograms();
    PluginHost               m_partInstrument[kNumChannels];
    juce::MidiMessageCollector m_partCollector[kNumChannels];
    std::atomic<bool>        m_partLoaded[kNumChannels] {};
    std::atomic<float>       m_partGain[kNumChannels] {};   // per-channel CC7 gain 0..1 (for VST parts)
    std::atomic<int>         m_partInstrumentCount { 0 };
    juce::AudioBuffer<float> m_partScratch;
    juce::MidiBuffer         m_partMidiScratch;
    std::string              m_partPath[kNumChannels];   // message-thread only
    std::string              m_partState[kNumChannels];  // base64 VST3 state (message-thread only)

    // Master multitimbral instrument (one plugin gets all 16 channels).
    PluginHost                 m_masterInstrument;
    juce::MidiMessageCollector m_masterCollector;
    std::atomic<bool>          m_masterLoaded { false };
    juce::MidiBuffer           m_masterMidiScratch;
    std::string                m_masterPath;   // message-thread only
    double m_currentSampleRate = 48000.0;
    int    m_currentBlockSize  = 512;
    juce::MidiBuffer m_effectMidi;   // scratch (empty) MIDI for effect processing

    std::array<std::atomic<int>, kNumChannels> m_lastBankMsb {};
    std::array<std::atomic<int>, kNumChannels> m_lastBankLsb {};
    std::array<std::atomic<int>, kNumChannels> m_lastProgram {};

    juce::AudioDeviceManager  m_deviceManager;
    juce::AudioSourcePlayer   m_sourcePlayer;

    TickCallback m_onTick;
    BlockCallback m_onBlock;
};
}
