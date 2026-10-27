#pragma once

#include "audio/IInstrumentRenderer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace pianoassist::audio
{

/** A hosted VST3 instrument behind the renderer boundary. The lesson logic
    never sees it: gating stays timeline-driven and the plug-in only turns
    scheduled MIDI into sound. Drum notes (channel 10) are not sent, because
    most instrument plug-ins would play them as pitched notes. */
class PluginInstrumentRenderer final : public IInstrumentRenderer
{
public:
    explicit PluginInstrumentRenderer (std::unique_ptr<juce::AudioPluginInstance> plugin);
    ~PluginInstrumentRenderer() override;

    juce::AudioPluginInstance& plugin() noexcept { return *plugin_; }

    void prepare (double sampleRate, int maxBlockFrames) override;
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override;
    void allNotesOff() noexcept override;
    void silence() noexcept override;
    int reportedLatencyFrames() const noexcept override;

private:
    void processChunk (int start, int count, RenderBlock block) noexcept;

    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    juce::AudioBuffer<float> scratch_;
    juce::MidiBuffer midi_;
    juce::MidiBuffer chunkMidi_;
    int maxBlock_ = 0;
    bool prepared_ = false;
    std::atomic<bool> notesOffPending_ { false };
    std::atomic<bool> resetPending_ { false };
};

} // namespace pianoassist::audio
