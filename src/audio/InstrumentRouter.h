#pragma once

#include "audio/IInstrumentRenderer.h"

#include <array>
#include <vector>

namespace pianoassist::audio
{

/** Which instrument plays each MIDI channel. nullptr on a channel means the
    main instrument. Immutable once published to the audio thread. */
struct InstrumentRouting
{
    IInstrumentRenderer* main = nullptr;
    std::array<IInstrumentRenderer*, 16> channels {};

    IInstrumentRenderer* forChannel (int channel) const noexcept
    {
        auto* r = channels[static_cast<std::size_t> (channel & 0x0F)];
        return r != nullptr ? r : main;
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
    static constexpr std::size_t maxInstruments = 17; ///< main + one per MIDI channel
    static constexpr std::size_t maxEvents = 4096;

    InstrumentRouter();

    void render (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept;
    /** Silences instruments of the old routing that the new one no longer uses. */
    static void silenceDropped (const InstrumentRouting& before, const InstrumentRouting& after) noexcept;
    static int activeVoices (const InstrumentRouting& routing) noexcept;

private:
    void renderChunk (const InstrumentRouting& routing, std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept;

    std::array<IInstrumentRenderer*, maxInstruments> distinct_ {};
    std::vector<std::vector<ScheduledMidiEvent>> eventsFor_;
    std::vector<std::size_t> counts_;
    std::vector<std::vector<float>> scratch_;
    std::vector<ScheduledMidiEvent> chunkEvents_;
};

} // namespace pianoassist::audio
