#pragma once

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
};

/** include decides which notes sound (by role, part...); sustain events
    always pass. */
PlaybackEventList buildPlaybackEvents (const timeline::Song& song, std::uint64_t id,
                                       const std::function<bool (const timeline::Note&)>& include = {});

} // namespace pianoassist::audio
