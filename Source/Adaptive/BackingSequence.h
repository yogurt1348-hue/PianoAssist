// BackingSequence — the accompaniment tracks of an Adaptive Song, flattened
// into one tick-sorted event list on the *reference* timeline, plus an
// allocation-free renderer for the audio thread.
//
// The renderer is cursor based (the adaptive playhead is monotonic between
// seeks), tracks which notes it started (16 x 128 counters) so stop / seek /
// long holds release exactly those, and chases program / bank / controllers /
// pitch bend after a seek so a jump lands with the right sounds.
//
// Output goes through MidiEventSink — in the app that is AudioEngine's note
// API (FluidSynth / VST3 / master instrument), in tests a recorder.

#pragma once

#include "MidiFile.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cadenza::adaptive
{
// Receives backing events. Channels are 1-based (1..16), like AudioEngine.
class MidiEventSink
{
public:
    virtual ~MidiEventSink() = default;
    virtual void noteOn(int channel, int note, int velocity) noexcept = 0;
    virtual void noteOff(int channel, int note) noexcept = 0;
    virtual void controlChange(int channel, int controller, int value) noexcept = 0;
    virtual void programChange(int channel, int program) noexcept = 0;
    virtual void pitchBend(int channel, int value14) noexcept = 0;
};

struct BackingEvent
{
    double tick = 0.0;                 // reference timeline ticks
    MidiEventKind kind = MidiEventKind::NoteOn;
    std::uint8_t channel = 1;          // OUTPUT channel 1..16
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    int bend = 8192;
    std::uint16_t sourceTrack = 0;
};

// Which tracks (and channels inside them) are backing.
struct TrackSelection
{
    int track = 0;
    std::uint16_t channelMask = 0xFFFF;   // bit c = source channel c (0..15)
};

class BackingSequence
{
public:
    // Maps a source tick (backing file) to a reference-timeline tick.
    using TickMapper = double (*)(double sourceTick, const void* context);

    BackingSequence() = default;

    // `outputChannel[c]` = 1..16 output channel for source channel c, 0 = drop.
    // `mapper` == nullptr means identity.
    static BackingSequence build(const MidiFileData& file, const std::vector<TrackSelection>& tracks,
                                 const std::array<std::uint8_t, 16>& outputChannel,
                                 TickMapper mapper = nullptr, const void* mapperContext = nullptr);

    static std::array<std::uint8_t, 16> identityChannels() noexcept;

    const std::vector<BackingEvent>& events() const noexcept { return m_events; }
    bool empty() const noexcept { return m_events.empty(); }
    double endTick() const noexcept { return m_endTick; }
    std::uint16_t outputChannelMask() const noexcept { return m_outputMask; }   // bit c-1 = channel c used

    // ---- playback (audio thread; no allocation, no locks) ----
    // Release sounding notes, then position at `tick` and chase controller state.
    // Events exactly at `tick` will be rendered by the next inclusive render().
    void seek(double tick, MidiEventSink& sink) noexcept;
    // Emit events in (fromTick, toTick] ([fromTick, toTick] when inclusive).
    void render(double fromTick, double toTick, bool inclusive, MidiEventSink& sink) noexcept;
    // Note Off for every note this sequence started and still sounds.
    void releaseAll(MidiEventSink& sink) noexcept;
    int soundingNotes() const noexcept;

private:
    void emit(const BackingEvent& e, MidiEventSink& sink) noexcept;

    std::vector<BackingEvent> m_events;
    double m_endTick = 0.0;
    std::uint16_t m_outputMask = 0;

    std::size_t m_cursor = 0;
    std::array<std::array<std::uint8_t, 128>, 16> m_sounding {};
};
}
