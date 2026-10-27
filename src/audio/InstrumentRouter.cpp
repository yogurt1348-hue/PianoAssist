#include "audio/InstrumentRouter.h"

#include <algorithm>

namespace pianoassist::audio
{
namespace
{
/** Collects the distinct instruments of a routing, main first. */
std::size_t distinctOf (const InstrumentRouting& routing, std::array<IInstrumentRenderer*, InstrumentRouter::maxInstruments>& out) noexcept
{
    std::size_t count = 0;
    const auto add = [&] (IInstrumentRenderer* r) {
        if (r == nullptr || std::find (out.begin(), out.begin() + static_cast<std::ptrdiff_t> (count), r) != out.begin() + static_cast<std::ptrdiff_t> (count))
            return;
        out[count++] = r;
    };
    add (routing.main);
    for (auto* r : routing.channels)
        add (r);
    return count;
}
} // namespace

bool InstrumentRouting::uses (const IInstrumentRenderer* renderer) const noexcept
{
    return renderer != nullptr
           && (main == renderer || std::find (channels.begin(), channels.end(), renderer) != channels.end());
}

InstrumentRouter::InstrumentRouter()
    : eventsFor_ (maxInstruments, std::vector<ScheduledMidiEvent> (maxEvents)),
      counts_ (maxInstruments, 0),
      scratch_ (maxChannels, std::vector<float> (chunkFrames)),
      chunkEvents_ (maxEvents)
{
}

void InstrumentRouter::render (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events,
                               RenderBlock block) noexcept
{
    if (block.numFrames <= chunkFrames)
        return renderChunk (routing, events, block);

    // Rare: a device block larger than the scratch buffers. Render in chunks.
    for (int start = 0; start < block.numFrames; start += chunkFrames)
    {
        const int count = std::min (chunkFrames, block.numFrames - start);
        std::size_t n = 0;
        for (const auto& e : events)
            if (e.sampleOffset >= start && e.sampleOffset < start + count && n < chunkEvents_.size())
                chunkEvents_[n++] = { e.sampleOffset - start, e.status, e.data1, e.data2 };
        std::array<float*, 8> pointers {};
        const int channels = std::min (block.numChannels, static_cast<int> (pointers.size()));
        for (int ch = 0; ch < channels; ++ch)
            pointers[static_cast<std::size_t> (ch)] = block.channels[ch] != nullptr ? block.channels[ch] + start : nullptr;
        renderChunk (routing, std::span<const ScheduledMidiEvent> (chunkEvents_.data(), n), { pointers.data(), channels, count });
    }
}

void InstrumentRouter::renderChunk (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events,
                                    RenderBlock block) noexcept
{
    const auto instruments = distinctOf (routing, distinct_);
    if (instruments == 0)
    {
        for (int ch = 0; ch < block.numChannels; ++ch)
            if (block.channels[ch] != nullptr)
                std::fill_n (block.channels[ch], block.numFrames, 0.0f);
        return;
    }
    if (instruments == 1)
        return distinct_[0]->render (events, block); // the common case: no split, no mixing

    std::fill (counts_.begin(), counts_.end(), 0);
    for (const auto& e : events)
    {
        auto* target = routing.forChannel (e.status & 0x0F);
        for (std::size_t i = 0; i < instruments; ++i)
            if (distinct_[i] == target && counts_[i] < maxEvents)
                eventsFor_[i][counts_[i]++] = e;
    }

    // The main instrument writes the output; the others add to it.
    distinct_[0]->render (std::span<const ScheduledMidiEvent> (eventsFor_[0].data(), counts_[0]), block);
    const int channels = std::min (block.numChannels, maxChannels);
    std::array<float*, maxChannels> scratch { scratch_[0].data(), scratch_[1].data() };
    for (std::size_t i = 1; i < instruments; ++i)
    {
        distinct_[i]->render (std::span<const ScheduledMidiEvent> (eventsFor_[i].data(), counts_[i]),
                              { scratch.data(), channels, block.numFrames });
        for (int ch = 0; ch < channels; ++ch)
            if (block.channels[ch] != nullptr)
                for (int f = 0; f < block.numFrames; ++f)
                    block.channels[ch][f] += scratch[static_cast<std::size_t> (ch)][f];
    }
    // Outputs beyond stereo copy the left channel.
    for (int ch = maxChannels; ch < block.numChannels; ++ch)
        if (block.channels[ch] != nullptr && block.channels[0] != nullptr)
            std::copy_n (block.channels[0], block.numFrames, block.channels[ch]);
}

void InstrumentRouter::silenceDropped (const InstrumentRouting& before, const InstrumentRouting& after) noexcept
{
    std::array<IInstrumentRenderer*, maxInstruments> old {};
    const auto count = distinctOf (before, old);
    for (std::size_t i = 0; i < count; ++i)
        if (! after.uses (old[i]))
            old[i]->silence();
}

int InstrumentRouter::activeVoices (const InstrumentRouting& routing) noexcept
{
    std::array<IInstrumentRenderer*, maxInstruments> all {};
    const auto count = distinctOf (routing, all);
    int voices = 0;
    for (std::size_t i = 0; i < count; ++i)
        voices += all[i]->activeVoiceCount();
    return voices;
}

} // namespace pianoassist::audio
