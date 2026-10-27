#include "audio/PluginInstrumentRenderer.h"

#include <algorithm>

namespace pianoassist::audio
{
namespace
{
constexpr int drumChannel = 9; // MIDI channel 10
constexpr int maxEventsPerBlock = 2048;
} // namespace

PluginInstrumentRenderer::PluginInstrumentRenderer (std::unique_ptr<juce::AudioPluginInstance> plugin)
    : plugin_ (std::move (plugin))
{
}

PluginInstrumentRenderer::~PluginInstrumentRenderer()
{
    if (prepared_)
        plugin_->releaseResources();
}

void PluginInstrumentRenderer::prepare (double sampleRate, int maxBlockFrames)
{
    if (prepared_)
        plugin_->releaseResources();
    maxBlock_ = std::max (1, maxBlockFrames);
    // Instruments need no input; ask for stereo out where the plug-in allows.
    plugin_->enableAllBuses();
    plugin_->setRateAndBufferSizeDetails (sampleRate, maxBlock_);
    plugin_->prepareToPlay (sampleRate, maxBlock_);
    prepared_ = true;

    const int channels = std::max ({ 2, plugin_->getTotalNumInputChannels(), plugin_->getTotalNumOutputChannels() });
    scratch_.setSize (channels, maxBlock_);
    midi_.ensureSize (static_cast<std::size_t> (maxEventsPerBlock) * 4u);
    chunkMidi_.ensureSize (static_cast<std::size_t> (maxEventsPerBlock) * 4u);
}

void PluginInstrumentRenderer::processChunk (int start, int count, RenderBlock block) noexcept
{
    chunkMidi_.clear();
    for (const auto metadata : midi_)
        if (metadata.samplePosition >= start && metadata.samplePosition < start + count)
            chunkMidi_.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition - start);

    juce::AudioBuffer<float> view (scratch_.getArrayOfWritePointers(), scratch_.getNumChannels(), count);
    view.clear();
    plugin_->processBlock (view, chunkMidi_);

    const int pluginOutputs = std::max (1, plugin_->getTotalNumOutputChannels());
    for (int ch = 0; ch < block.numChannels; ++ch)
        if (block.channels[ch] != nullptr)
            std::copy_n (view.getReadPointer (std::min (ch, pluginOutputs - 1)), count, block.channels[ch] + start);
}

void PluginInstrumentRenderer::render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept
{
    if (! prepared_ || block.channels == nullptr)
        return;

    midi_.clear();
    if (resetPending_.exchange (false))
        plugin_->reset();
    if (notesOffPending_.exchange (false))
        for (int channel = 1; channel <= 16; ++channel)
        {
            midi_.addEvent (juce::MidiMessage::controllerEvent (channel, 64, 0), 0);
            midi_.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
        }
    int added = 0;
    for (const auto& e : events)
    {
        if (++added > maxEventsPerBlock)
            break; // the buffer was reserved for this many; never allocate here
        const int kind = e.status & 0xF0;
        if (! sendDrums_ && (e.status & 0x0F) == drumChannel && (kind == 0x80 || kind == 0x90))
            continue;
        if (kind == 0xC0)
            continue;
        const int offset = std::clamp (e.sampleOffset, 0, std::max (0, block.numFrames - 1));
        const std::uint8_t bytes[3] { e.status, e.data1, e.data2 };
        midi_.addEvent (bytes, kind == 0xC0 ? 2 : 3, offset);
    }

    // The device may deliver a larger block than announced; split it.
    for (int start = 0; start < block.numFrames; start += maxBlock_)
        processChunk (start, std::min (maxBlock_, block.numFrames - start), block);
}

void PluginInstrumentRenderer::allNotesOff() noexcept
{
    notesOffPending_.store (true);
}

void PluginInstrumentRenderer::silence() noexcept
{
    notesOffPending_.store (true);
    resetPending_.store (true);
}

int PluginInstrumentRenderer::reportedLatencyFrames() const noexcept
{
    return plugin_->getLatencySamples();
}

} // namespace pianoassist::audio
