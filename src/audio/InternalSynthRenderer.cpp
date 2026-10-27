#include "audio/InternalSynthRenderer.h"

#include <array>
#include <cmath>

namespace pianoassist::audio
{
namespace
{
constexpr int voiceCount = 48;
constexpr int percussionChannel = 10; // 1-based, as JUCE counts channels

struct PianoSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int channel) override { return channel != percussionChannel; }
};

class PianoVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override { return dynamic_cast<PianoSound*> (sound) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        const double hz = juce::MidiMessage::getMidiNoteInHertz (note);
        const double rate = getSampleRate();
        for (std::size_t h = 0; h < phaseDelta_.size(); ++h)
        {
            phase_[h] = 0.0;
            const double partial = hz * static_cast<double> (h + 1);
            phaseDelta_[h] = partial < rate * 0.45 ? juce::MathConstants<double>::twoPi * partial / rate : 0.0;
        }
        // Higher notes decay faster, like strings.
        const float decay = juce::jlimit (0.6f, 4.0f, 4.0f - static_cast<float> (note - 21) * 0.035f);
        envelope_.setSampleRate (rate);
        envelope_.setParameters ({ 0.004f, decay, 0.0f, 0.3f });
        envelope_.noteOn();
        level_ = 0.18f * std::pow (velocity, 1.4f);
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            envelope_.noteOff();
        }
        else
        {
            envelope_.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int start, int count) override
    {
        if (! isVoiceActive())
            return;

        static constexpr std::array<double, 4> weights { 1.0, 0.45, 0.22, 0.1 };
        for (int i = 0; i < count; ++i)
        {
            double sample = 0.0;
            for (std::size_t h = 0; h < phase_.size(); ++h)
            {
                sample += weights[h] * std::sin (phase_[h]);
                phase_[h] += phaseDelta_[h];
            }
            const auto value = static_cast<float> (sample) * level_ * envelope_.getNextSample();
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, start + i, value);

            if (! envelope_.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    std::array<double, 4> phase_ {};
    std::array<double, 4> phaseDelta_ {};
    juce::ADSR envelope_;
    float level_ = 0.0f;
};
} // namespace

InternalSynthRenderer::InternalSynthRenderer()
{
    for (int i = 0; i < voiceCount; ++i)
        synth_.addVoice (new PianoVoice());
    synth_.addSound (new PianoSound());
}

void InternalSynthRenderer::prepare (double sampleRate, int maxBlockFrames)
{
    synth_.setCurrentPlaybackSampleRate (sampleRate);
    midi_.ensureSize (static_cast<std::size_t> (std::max (maxBlockFrames, 4096)) * 4);
}

void InternalSynthRenderer::render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept
{
    midi_.clear();
    for (const auto& e : events)
    {
        const juce::uint8 bytes[3] { e.status, e.data1, e.data2 };
        midi_.addEvent (bytes, 3, e.sampleOffset);
    }

    // Wraps the caller's channel pointers; JUCE keeps up to 32 in place.
    juce::AudioBuffer<float> buffer (block.channels, block.numChannels, block.numFrames);
    buffer.clear();
    synth_.renderNextBlock (buffer, midi_, 0, block.numFrames);
}

void InternalSynthRenderer::allNotesOff() noexcept
{
    synth_.allNotesOff (0, true);
}

void InternalSynthRenderer::silence() noexcept
{
    for (int channel = 1; channel <= 16; ++channel)
        synth_.handleSustainPedal (channel, false);
    synth_.allNotesOff (0, false);
}

int InternalSynthRenderer::activeVoiceCount() const noexcept
{
    int active = 0;
    for (int i = 0; i < synth_.getNumVoices(); ++i)
        if (synth_.getVoice (i)->isVoiceActive())
            ++active;
    return active;
}

} // namespace pianoassist::audio
