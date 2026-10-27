#pragma once

#include "audio/IInstrumentRenderer.h"

#include <array>
#include <vector>

namespace pianoassist::audio
{

/** Which instrument plays each part lane and each MIDI channel. A part with
    no instrument of its own follows its channel; a channel with none uses
    the main instrument. Immutable once published to the audio thread. */
struct InstrumentRouting
{
    IInstrumentRenderer* main = nullptr;
    std::array<IInstrumentRenderer*, 16> channels {};
    std::array<IInstrumentRenderer*, lane::maxParts> parts {};

    IInstrumentRenderer* forChannel (int channel) const noexcept
    {
        auto* r = channels[static_cast<std::size_t> (channel & 0x0F)];
        return r != nullptr ? r : main;
    }
    IInstrumentRenderer* forEvent (std::uint8_t laneId, int channel) const noexcept
    {
        if (laneId == lane::preview)
            return main;
        if (laneId < lane::maxParts && parts[laneId] != nullptr)
            return parts[laneId];
        return forChannel (channel);
    }
    bool uses (const IInstrumentRenderer* renderer) const noexcept;
};

/** Splits one block of scheduled events by channel, lets each instrument
    render its channels, and mixes the results. Buffers are allocated once in
    the constructor; render() never allocates. Audio thread only. */
class InstrumentRouter
{
public:
    static constexpr int chunkFrames = 4096;
    static constexpr int maxChannels = 2;
    static constexpr std::size_t maxInstruments = 1 + 16 + lane::maxParts; ///< main, channels, parts
    static constexpr std::size_t maxEvents = 4096;

    InstrumentRouter();

    /** previous: the routing of the last block when it just changed; events
        marked viaPrevious go to its instrument if that one still plays
        (one no longer used is silenced anyway). */
    void render (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events, RenderBlock block,
                 const InstrumentRouting* previous = nullptr) noexcept;
    /** Silences instruments of the old routing that the new one no longer uses. */
    static void silenceDropped (const InstrumentRouting& before, const InstrumentRouting& after) noexcept;
    static int activeVoices (const InstrumentRouting& routing) noexcept;

private:
    void renderChunk (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events, RenderBlock block,
                      const InstrumentRouting* previous) noexcept;

    std::array<IInstrumentRenderer*, maxInstruments> distinct_ {};
    std::vector<std::vector<ScheduledMidiEvent>> eventsFor_;
    std::vector<std::size_t> counts_;
    std::vector<std::vector<float>> scratch_;
    std::vector<ScheduledMidiEvent> chunkEvents_;
};

} // namespace pianoassist::audio
