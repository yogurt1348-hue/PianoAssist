#include "AudioEngine.h"
#include "AudioBlockPipeline.h"
#include "MidiChannel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cadenza::audio
{
namespace
{
// Soft limiter: linear up to 0.9, then a tanh knee so peaks stay below ~1.0
// instead of hard-clipping when the master volume is pushed.
inline float softLimit(float x) noexcept
{
    constexpr float t = 0.9f;
    if (x >  t) return  t + std::tanh((x - t) * 2.0f) * (1.0f - t);
    if (x < -t) return -t + std::tanh((x + t) * 2.0f) * (1.0f - t);
    return x;
}
}

AudioEngine::AudioEngine()
    : m_synth(createSynthEngine())
{
    for (int ch = 0; ch < kNumChannels; ++ch) {
        m_lastBankMsb[ch].store(-1);
        m_lastBankLsb[ch].store(-1);
        m_lastProgram[ch].store(-1);
    }
}

AudioEngine::~AudioEngine()
{
    stopAudioDevice();
}

void AudioEngine::prepareToPlay(int samplesPerBlock, double sampleRate)
{
    m_transport.setSampleRate(sampleRate);
    m_metronome.prepare(sampleRate);
    if (m_synth) m_synth->prepare(sampleRate, 0);
    m_masterEffect.prepare(sampleRate, samplesPerBlock > 0 ? samplesPerBlock : 512);
    m_masterEq.prepare(sampleRate, 2);
    m_masterComp.prepare(sampleRate);
    // Master-bus compression is OFF by default: with an arranger the kick + bass
    // make a low-threshold comp duck the whole band on every beat, and adding a
    // right-hand melody pumps it harder ("loses power"). Peaks are already caught
    // cleanly by the final soft-limiter, so the mix stays loud without breathing.
    m_masterComp.setEnabled(false);
    m_masterGlue.prepare(sampleRate);
    m_polishedMaster.prepare(sampleRate);   // enabled state is set from settings

    m_currentSampleRate = sampleRate;
    m_currentBlockSize  = samplesPerBlock > 0 ? samplesPerBlock : 512;
    m_partScratch.setSize(2, m_currentBlockSize, false, false, true);
    for (int ch = 1; ch < kNumChannels; ++ch) {
        m_partCollector[ch].reset(sampleRate);
        m_partInstrument[ch].prepare(sampleRate, m_currentBlockSize);
        m_partGain[ch].store(1.0f);
    }
    m_masterCollector.reset(sampleRate);
    m_masterInstrument.prepare(sampleRate, m_currentBlockSize);
}

void AudioEngine::releaseResources()
{
    if (m_synth) m_synth->release();
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    const juce::ScopedNoDenormals noDenormals;
    auto* buffer = info.buffer;

    if (info.numSamples > m_currentBlockSize) {
        m_currentBlockSize = info.numSamples;
        m_partScratch.setSize(2, m_currentBlockSize, false, false, true);
    }

    // Convenience wrapper buffer over the requested slice.
    juce::AudioBuffer<float> view(buffer->getArrayOfWritePointers(),
                                  buffer->getNumChannels(),
                                  info.startSample,
                                  info.numSamples);

    // Live-playback timing: events for this block are fired BEFORE the synth
    // renders, so style note-ons/offs are audible in the same block instead of
    // one block late. See AudioBlockPipeline.h for the ordering rationale.
    runAudioBlock(
        [&] {  // 1) advance transport and let StyleEngine push due MIDI to the synth
            consumeTransportCommands();
            const int ticksAdvanced = m_transport.advance(info.numSamples);
            if (m_onTick && ticksAdvanced > 0)
                m_onTick(ticksAdvanced, m_transport);
            if (m_onBlock)
                m_onBlock(info.numSamples, m_currentSampleRate);   // Adaptive Song Mode
        },
        [&] {  // 2) render synth (clears buffer first, then renders queued notes)
            if (m_synth) m_synth->renderBlock(view);
            else view.clear();
            renderPartInstruments(view);   // sum any per-part VST instruments (no-op if none)
            renderMasterInstrument(view);  // sum the master multitimbral instrument (no-op if none)
        },
        [&] {  // 3) metronome clicks on top
            m_metronome.renderBlock(view, m_transport);
        },
        [&] {  // 4) master chain: EQ -> Airwindows console glue -> insert effect
            float* const* chans = view.getArrayOfWritePointers();
            const int nc = view.getNumChannels();
            const int ns = view.getNumSamples();
            m_masterEq.process(chans, nc, ns);     // 3-band EQ only
            m_masterComp.process(chans, nc, ns);   // gentle master glue/density
            m_masterGlue.process(chans, nc, ns);   // console glue (no-op: disabled by default)
            m_effectMidi.clear();
            m_masterEffect.process(view, m_effectMidi);

            // Master output volume + final soft limiter: lets the whole mix (drums
            // included) be pushed loud and stay clean instead of hard-clipping.
            const float gain = m_masterGain.load();

            // Fade-out: a per-sample multiplicative ramp on top of the master
            // gain. When it reaches silence, stop the transport right here on
            // the audio thread and hand completion back to the UI.
            float fadeStep = 1.0f;
            if (m_fadeActive.load()) {
                const double samples = std::max(1.0, m_fadeSeconds.load() * m_currentSampleRate);
                // Decay from 1.0 to -60 dB (0.001) over `samples` samples.
                fadeStep = (float) std::pow(0.001, 1.0 / samples);
            } else if (m_fadeGain < 1.0f) {
                m_fadeGain = 1.0f;   // fade cancelled — back to full level
            }

            // When Polished Master is on, the brickwall limiter in m_polishedMaster
            // (run just after this loop) owns peak control, so we apply only the
            // master gain + fade here. Otherwise keep the original tanh soft-limiter.
            const bool polished = m_polishedMaster.enabled();
            float fade = m_fadeGain;
            for (int s = 0; s < ns; ++s) {
                for (int c = 0; c < nc; ++c) {
                    const float v = chans[c][s] * gain * fade;
                    chans[c][s] = polished ? v : softLimit(v);
                }
                fade *= fadeStep;
            }
            m_fadeGain = fade;

            if (polished)
                m_polishedMaster.process(chans, nc, ns);

            if (m_fadeActive.load() && m_fadeGain <= 0.001f) {
                stopFromAudioThread();
                m_fadeGain = 1.0f;
                m_fadeActive.store(false);
                m_fadeCompleted.store(true);
            }
        });
}

void AudioEngine::startAudioDevice() { startAudioDevice(nullptr); }

void AudioEngine::startAudioDevice(const juce::XmlElement* savedState)
{
    // Restore the user's saved device choice if present; otherwise default stereo.
    juce::String error;
    if (savedState != nullptr)
        error = m_deviceManager.initialise(0, 2, savedState, true);
    else
        error = m_deviceManager.initialiseWithDefaultDevices(0, 2);

    // Tighter live latency: if the device opened with a large block, nudge it
    // down toward a live-play buffer so right-hand melody feels responsive. Only
    // ever reduce (never raise) so we respect a deliberately-larger saved choice.
    {
        auto setup = m_deviceManager.getAudioDeviceSetup();
        constexpr int kLiveBuffer = 256;
        if (setup.bufferSize > kLiveBuffer) {
            setup.bufferSize = kLiveBuffer;
            m_deviceManager.setAudioDeviceSetup(setup, true);
        }
    }

    m_sourcePlayer.setSource(this);
    m_deviceManager.addAudioCallback(&m_sourcePlayer);

    // Log the resulting audio output so a "no sound" problem can be diagnosed.
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("[Cadenza] AUDIO DEVICE ERROR: " + error);
    } else if (auto* dev = m_deviceManager.getCurrentAudioDevice()) {
        juce::Logger::writeToLog("[Cadenza] Audio device: \"" + dev->getName() + "\""
            + " open=" + juce::String(dev->isOpen() ? "yes" : "no")
            + " playing=" + juce::String(dev->isPlaying() ? "yes" : "no")
            + " sampleRate=" + juce::String(dev->getCurrentSampleRate())
            + " bufferSize=" + juce::String(dev->getCurrentBufferSizeSamples())
            + " outputChannels=" + juce::String(dev->getActiveOutputChannels().toInteger()));
    } else {
        juce::Logger::writeToLog("[Cadenza] AUDIO DEVICE: none opened (no output device available).");
    }
}

void AudioEngine::stopAudioDevice()
{
    m_deviceManager.removeAudioCallback(&m_sourcePlayer);
    m_sourcePlayer.setSource(nullptr);
}

void AudioEngine::play()
{
    if (!m_transportCommands.publish({ TransportCommandType::Play, 0.0 }))
        juce::Logger::writeToLog("[Cadenza] Transport command mailbox overflow; oldest command replaced.");
}

void AudioEngine::stop()
{
    if (!m_transportCommands.publish({ TransportCommandType::Stop, 0.0 }))
        juce::Logger::writeToLog("[Cadenza] Transport command mailbox overflow; oldest command replaced.");
    allNotesOff();
}

void AudioEngine::stopFromAudioThread()
{
    m_transport.stop();
    allNotesOff();
}

void AudioEngine::startFadeOut(double seconds)
{
    m_fadeSeconds.store((float) juce::jlimit(0.5, 30.0, seconds));
    m_fadeActive.store(true);
}

void AudioEngine::cancelFadeOut()
{
    m_fadeActive.store(false);   // the audio thread snaps m_fadeGain back to 1
}

void AudioEngine::setBpm(double bpm)
{
    if (!m_transportCommands.publish({ TransportCommandType::SetBpm, bpm }))
        juce::Logger::writeToLog("[Cadenza] Transport command mailbox overflow; oldest command replaced.");
}

void AudioEngine::consumeTransportCommands()
{
    std::array<TransportCommand, TransportCommandMailbox::capacity> commands {};
    const auto count = m_transportCommands.tryTakeAll(commands);
    for (std::size_t i = 0; i < count; ++i) {
        switch (commands[i].type) {
            case TransportCommandType::Play:
                if (!m_transport.playing())
                    m_transport.startFromBeginning();
                break;
            case TransportCommandType::Stop:
                m_transport.stop();
                break;
            case TransportCommandType::SetBpm:
                m_transport.setBpm(commands[i].bpm);
                break;
        }
    }
}

// Cadenza channel -> a 1-based MIDI channel 1..16 for the master multitimbral
// plugin. Cadenza channels are already the GM-style 1-based channels (drums=10),
// which is exactly what a multitimbral plugin expects per part — so pass them
// straight through (NOT the 0-based internal synth channel).
static int masterMidiChannel(int cadenzaChannel) noexcept
{
    return juce::jlimit(1, 16, cadenzaChannel);
}

void AudioEngine::noteOn(int channel, int note, int velocity)
{
    if (m_masterLoaded.load()) {
        auto m = juce::MidiMessage::noteOn(masterMidiChannel(channel), note, (juce::uint8) velocity);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_masterCollector.addMessageToQueue(m);
        return;
    }
    if (channel > 0 && channel < kNumChannels && m_partLoaded[channel].load()) {
        auto m = juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_partCollector[channel].addMessageToQueue(m);   // thread-safe intake
        return;
    }
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->noteOn(*synthChannel, note, velocity);
    }
}

void AudioEngine::noteOff(int channel, int note)
{
    if (m_masterLoaded.load()) {
        auto m = juce::MidiMessage::noteOff(masterMidiChannel(channel), note);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_masterCollector.addMessageToQueue(m);
        return;
    }
    if (channel > 0 && channel < kNumChannels && m_partLoaded[channel].load()) {
        auto m = juce::MidiMessage::noteOff(1, note);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_partCollector[channel].addMessageToQueue(m);
        return;
    }
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->noteOff(*synthChannel, note);
    }
}

void AudioEngine::programChange(int channel, int program)
{
    if (channel > 0 && channel < kNumChannels)
        m_lastProgram[channel].store(std::clamp(program, 0, 127));

    if (m_masterLoaded.load()) {
        auto m = juce::MidiMessage::programChange(masterMidiChannel(channel), program);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_masterCollector.addMessageToQueue(m);
        return;
    }
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->programChange(*synthChannel, program);
    }
}

void AudioEngine::controlChange(int channel, int cc, int value)
{
    if (channel > 0 && channel < kNumChannels) {
        if (cc == 0)
            m_lastBankMsb[channel].store(std::clamp(value, 0, 127));
        else if (cc == 32)
            m_lastBankLsb[channel].store(std::clamp(value, 0, 127));
    }

    // CC7 (volume) doubles as the per-part gain for VST-instrument channels, so
    // the mixer fader / mute / solo (sent as effective CC7) affects them too.
    if (cc == 7 && channel > 0 && channel < kNumChannels)
        m_partGain[channel].store(std::clamp(value, 0, 127) / 127.0f);

    if (m_masterLoaded.load()) {
        auto m = juce::MidiMessage::controllerEvent(masterMidiChannel(channel), cc, value);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_masterCollector.addMessageToQueue(m);
        return;
    }
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->controlChange(*synthChannel, cc, value);
    }
}

void AudioEngine::pitchBend(int channel, int value14)
{
    if (m_masterLoaded.load()) {
        auto m = juce::MidiMessage::pitchWheel(masterMidiChannel(channel), juce::jlimit(0, 16383, value14));
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_masterCollector.addMessageToQueue(m);
        return;
    }
    // Pitch bend goes to the internal synth only; VST-instrument parts would
    // need a MIDI pitch-bend in their collector (not wired yet — rare on the
    // accompaniment channels that use VSTs).
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->pitchBend(*synthChannel, value14);
    }
}

void AudioEngine::allNotesOff()
{
    if (m_synth) m_synth->allNotesOff();

    const double timestamp = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool masterLoaded = m_masterLoaded.load();
    for (int channel = 1; channel < kNumChannels; ++channel) {
        if (masterLoaded) {
            auto masterNotesOff = juce::MidiMessage::allNotesOff(channel);
            masterNotesOff.setTimeStamp(timestamp);
            m_masterCollector.addMessageToQueue(masterNotesOff);

            auto masterSoundOff = juce::MidiMessage::allSoundOff(channel);
            masterSoundOff.setTimeStamp(timestamp);
            m_masterCollector.addMessageToQueue(masterSoundOff);
        }

        if (!m_partLoaded[channel].load())
            continue;

        auto notesOff = juce::MidiMessage::allNotesOff(1);
        notesOff.setTimeStamp(timestamp);
        m_partCollector[channel].addMessageToQueue(notesOff);

        auto soundOff = juce::MidiMessage::allSoundOff(1);
        soundOff.setTimeStamp(timestamp);
        m_partCollector[channel].addMessageToQueue(soundOff);
    }
}

bool AudioEngine::loadSoundFont(const std::string& path)
{
    if (!m_synth || !m_synth->loadSoundFont(path))
        return false;
    reapplyRememberedChannelPrograms();
    return true;
}

void AudioEngine::reapplyRememberedChannelPrograms()
{
    if (!m_synth)
        return;

    for (int channel = 1; channel < kNumChannels; ++channel) {
        const int bankMsb = m_lastBankMsb[channel].load();
        const int bankLsb = m_lastBankLsb[channel].load();
        const int program = m_lastProgram[channel].load();
        if (bankMsb >= 0)
            m_synth->controlChange(channel - 1, 0, bankMsb);
        if (bankLsb >= 0)
            m_synth->controlChange(channel - 1, 32, bankLsb);
        if (program >= 0)
            m_synth->programChange(channel - 1, program);
    }
}

const char* AudioEngine::synthEngineName() const noexcept
{
    return m_synth ? m_synth->engineName() : "NoSynthEngine";
}

bool AudioEngine::supportsSoundFonts() const noexcept
{
    return m_synth != nullptr && m_synth->supportsSoundFonts();
}

bool AudioEngine::loadMasterEffect(const std::string& path, std::string& error)
{
    juce::String err;
    const bool ok = m_masterEffect.loadFromFile(juce::String(path), err);
    if (!ok) error = err.toStdString();
    return ok;
}

void AudioEngine::clearMasterEffect()        { m_masterEffect.clear(); }
bool AudioEngine::hasMasterEffect() const    { return m_masterEffect.isLoaded(); }
std::string AudioEngine::masterEffectName() const { return m_masterEffect.name().toStdString(); }

// ---- per-part VST instruments ----

void AudioEngine::renderPartInstruments(juce::AudioBuffer<float>& view)
{
    if (m_partInstrumentCount.load() <= 0)
        return;   // common case: no per-part instruments — zero work

    const int ns = view.getNumSamples();
    const int nc = juce::jmin(view.getNumChannels(), 2);

    for (int ch = 1; ch < kNumChannels; ++ch) {
        if (!m_partLoaded[ch].load())
            continue;

        juce::AudioBuffer<float> scratch(m_partScratch.getArrayOfWritePointers(), 2, ns);
        scratch.clear();
        m_partMidiScratch.clear();
        m_partCollector[ch].removeNextBlockOfMessages(m_partMidiScratch, ns);
        m_partInstrument[ch].process(scratch, m_partMidiScratch);   // VST instrument: MIDI -> audio

        const float gain = m_partGain[ch].load();
        for (int c = 0; c < nc; ++c)
            view.addFrom(c, 0, scratch, c, 0, ns, gain);   // mixer fader / mute / solo
    }
}

void AudioEngine::renderMasterInstrument(juce::AudioBuffer<float>& view)
{
    if (!m_masterLoaded.load())
        return;   // common case when not using a master instrument
    const int ns = view.getNumSamples();
    const int nc = juce::jmin(view.getNumChannels(), 2);

    juce::AudioBuffer<float> scratch(m_partScratch.getArrayOfWritePointers(), 2, ns);
    scratch.clear();
    m_masterMidiScratch.clear();
    m_masterCollector.removeNextBlockOfMessages(m_masterMidiScratch, ns);
    m_masterInstrument.process(scratch, m_masterMidiScratch);   // multitimbral VST: MIDI -> audio
    for (int c = 0; c < nc; ++c)
        view.addFrom(c, 0, scratch, c, 0, ns);
}

bool AudioEngine::loadMasterInstrument(const std::string& path, std::string& error)
{
    if (m_masterLoaded.load() && m_masterPath == path)
        return true;   // idempotent
    m_masterInstrument.prepare(m_currentSampleRate, m_currentBlockSize);
    juce::String err;
    if (!m_masterInstrument.loadFromFile(juce::String(path), err)) {
        error = err.toStdString();
        return false;
    }
    m_masterPath = path;
    m_masterLoaded.store(true);
    return true;
}

void AudioEngine::clearMasterInstrument()
{
    m_masterLoaded.store(false);
    m_masterInstrument.clear();
    m_masterPath.clear();
}

bool AudioEngine::hasMasterInstrument() const { return m_masterLoaded.load(); }
std::string AudioEngine::masterInstrumentName() const { return m_masterInstrument.name().toStdString(); }
std::string AudioEngine::masterInstrumentPath() const { return m_masterPath; }
void AudioEngine::showMasterInstrumentEditor() { m_masterInstrument.showEditor("Master Instrument (whole band)"); }

bool AudioEngine::loadPartInstrument(int channel, const std::string& path, std::string& error)
{
    if (channel <= 0 || channel >= kNumChannels) { error = "invalid channel"; return false; }
    // Idempotent: if this exact plugin is already loaded on this channel, keep it.
    // This makes re-applying a style (or switching to one that reuses the same
    // plugin) cheap instead of tearing the instance down and rebuilding it.
    if (m_partLoaded[channel].load() && m_partPath[channel] == path)
        return true;
    m_partInstrument[channel].prepare(m_currentSampleRate, m_currentBlockSize);
    juce::String err;
    if (!m_partInstrument[channel].loadFromFile(juce::String(path), err)) {
        error = err.toStdString();
        return false;
    }
    m_partPath[channel] = path;
    m_partState[channel].clear();
    if (!m_partLoaded[channel].exchange(true))
        m_partInstrumentCount.fetch_add(1);
    return true;
}

bool AudioEngine::loadPartInstrument(int channel, const std::string& path,
                                     const std::string& stateBase64, std::string& error)
{
    if (channel <= 0 || channel >= kNumChannels) { error = "invalid channel"; return false; }
    // Idempotent on (path, state): re-applying the same voice+state is a no-op.
    if (m_partLoaded[channel].load() && m_partPath[channel] == path && m_partState[channel] == stateBase64)
        return true;
    m_partInstrument[channel].prepare(m_currentSampleRate, m_currentBlockSize);
    juce::String err;
    if (!m_partInstrument[channel].loadFromFile(juce::String(path), err)) {
        error = err.toStdString();
        return false;
    }
    if (!stateBase64.empty()) {
        juce::MemoryBlock block;
        juce::MemoryOutputStream out(block, false);
        if (juce::Base64::convertFromBase64(out, juce::String(stateBase64))) {
            out.flush();
            m_partInstrument[channel].setStateBlob(block);
        }
    }
    m_partPath[channel]  = path;
    m_partState[channel] = stateBase64;
    if (!m_partLoaded[channel].exchange(true))
        m_partInstrumentCount.fetch_add(1);
    return true;
}

std::string AudioEngine::capturePartInstrumentState(int channel) const
{
    if (channel <= 0 || channel >= kNumChannels || !m_partLoaded[channel].load())
        return {};
    const juce::MemoryBlock block = m_partInstrument[channel].getStateBlob();
    if (block.getSize() == 0)
        return {};
    return juce::Base64::toBase64(block.getData(), block.getSize()).toStdString();
}

void AudioEngine::clearPartInstrument(int channel)
{
    if (channel <= 0 || channel >= kNumChannels) return;
    if (m_partLoaded[channel].exchange(false))
        m_partInstrumentCount.fetch_sub(1);
    m_partInstrument[channel].clear();
    m_partPath[channel].clear();
    m_partState[channel].clear();
}

void AudioEngine::clearAllPartInstruments()
{
    for (int ch = 1; ch < kNumChannels; ++ch)
        clearPartInstrument(ch);
}

std::string AudioEngine::partInstrumentPath(int channel) const
{
    if (channel <= 0 || channel >= kNumChannels) return {};
    return m_partPath[channel];
}

bool AudioEngine::hasPartInstrument(int channel) const
{
    return channel > 0 && channel < kNumChannels && m_partLoaded[channel].load();
}

std::string AudioEngine::partInstrumentName(int channel) const
{
    if (channel <= 0 || channel >= kNumChannels) return {};
    return m_partInstrument[channel].name().toStdString();
}

void AudioEngine::showPartInstrumentEditor(int channel)
{
    if (channel <= 0 || channel >= kNumChannels) return;
    m_partInstrument[channel].showEditor(
        juce::String(m_partInstrument[channel].name()) + "  (part " + juce::String(channel) + ")");
}
}
