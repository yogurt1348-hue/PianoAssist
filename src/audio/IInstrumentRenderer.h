#pragma once

#include <cstdint>
#include <span>

namespace pianoassist::audio
{

/** A MIDI event scheduled at a sample offset inside the next render block. */
struct ScheduledMidiEvent
{
    std::int32_t sampleOffset = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct RenderBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numFrames = 0;
};

/** Boundary between lesson logic and sound. The internal synth implements it
    now; a VST3 host can implement it later without touching lesson_engine.

    allNotesOff() releases voices: release tails may still sound. silence()
    is the explicit panic action and resets voices where the renderer can. */
class IInstrumentRenderer
{
public:
    virtual ~IInstrumentRenderer() = default;

    virtual void prepare (double sampleRate, int maxBlockFrames) = 0;
    virtual void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;
    virtual void silence() noexcept = 0;
    virtual int reportedLatencyFrames() const noexcept = 0;
};

} // namespace pianoassist::audio
