#pragma once

#include "timeline/Song.h"

#include <cstdint>
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
};

/** Immutable, time-sorted event list built on the control thread. At equal
    times note-offs come first so repeated notes retrigger cleanly. */
struct PlaybackEventList
{
    std::vector<PlaybackEvent> events;
    double durationSeconds = 0.0;
    std::uint64_t id = 0; ///< lets the control thread see which list the audio thread uses
};

PlaybackEventList buildPlaybackEvents (const timeline::Song& song, std::uint64_t id);

} // namespace pianoassist::audio
