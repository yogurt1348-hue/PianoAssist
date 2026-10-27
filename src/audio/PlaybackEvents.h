#pragma once

#include "audio/IInstrumentRenderer.h"
#include "timeline/Song.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace pianoassist::audio
{

/** One MIDI event at a song time, ready for the audio thread. */
struct PlaybackEvent
{
    double seconds = 0.0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t lane = 254; ///< the part (lane::ofPart), or lane::byChannel
};

/** A part's sound at the start of the song: what the file selects before its first note. */
struct LaneStart
{
    std::int16_t program = -1; ///< -1 when the file sets none
    std::int16_t bank = -1;    ///< MSB * 128 + LSB, -1 when none
    std::uint8_t channel = 0;
};

/** Immutable, time-sorted event list built on the control thread. At equal
    times note-offs come first so repeated notes retrigger cleanly. */
struct PlaybackEventList
{
    std::vector<PlaybackEvent> events;
    double durationSeconds = 0.0;
    std::uint64_t id = 0; ///< lets the control thread see which list the audio thread uses
    /** GM program (0-127) per channel from the song's parts, -1 when none.
        Sent to the instrument whenever the list or the instrument changes,
        so seeking never loses the sound of a channel. */
    std::array<std::int16_t, 16> programs { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    /** Per part lane: the sound before any program change in the list. The
        list also holds the file's later program changes (and bank selects)
        at their times, so seek and loop restore the sound of that moment. */
    std::array<LaneStart, lane::maxParts> lanes {};
};

/** include decides which notes sound (by role, part...); sustain events
    always pass. gain (0-1) scales a note's velocity; 0 leaves it out. */
PlaybackEventList buildPlaybackEvents (const timeline::Song& song, std::uint64_t id,
                                       const std::function<bool (const timeline::Note&)>& include = {},
                                       const std::function<double (const timeline::Note&)>& gain = {});

} // namespace pianoassist::audio
