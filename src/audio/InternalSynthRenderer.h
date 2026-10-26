#pragma once

#include "audio/IInstrumentRenderer.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace pianoassist::audio
{

/** Small built-in piano-like synth (additive harmonics with a decaying
    envelope) on juce::Synthesiser. Channel 10 percussion is not voiced.
    Note-offs let voices release naturally; silence() cuts them at once. */
class InternalSynthRenderer final : public IInstrumentRenderer
{
public:
    InternalSynthRenderer();

    void prepare (double sampleRate, int maxBlockFrames) override;
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override;
    void allNotesOff() noexcept override;
    void silence() noexcept override;
    int reportedLatencyFrames() const noexcept override { return 0; }

    int activeVoiceCount() const noexcept;

private:
    juce::Synthesiser synth_;
    juce::MidiBuffer midi_;
};

} // namespace pianoassist::audio
