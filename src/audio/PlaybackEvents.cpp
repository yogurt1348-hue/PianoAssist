#include "audio/PlaybackEvents.h"

#include <algorithm>

namespace pianoassist::audio
{
namespace
{
int orderAtSameTime (std::uint8_t status, std::uint8_t data2)
{
    const auto kind = status & 0xF0u;
    if (kind == 0x80u || (kind == 0x90u && data2 == 0))
        return 0; // note-off
    if (kind == 0xB0u)
        return 1; // controllers before new notes
    return 2;
}
} // namespace

PlaybackEventList buildPlaybackEvents (const timeline::Song& song, std::uint64_t id,
                                       const std::function<bool (timeline::NoteRole)>& includeRole)
{
    PlaybackEventList list;
    list.id = id;
    list.durationSeconds = song.durationSeconds;
    list.events.reserve (song.timeline.notes().size() * 2 + song.sustainEvents.size());

    for (const auto& note : song.timeline.notes())
    {
        if (includeRole && ! includeRole (note.role))
            continue;
        const auto channel = static_cast<std::uint8_t> (note.channel & 0x0F);
        list.events.push_back ({ note.startSeconds, static_cast<std::uint8_t> (0x90u | channel), note.pitch,
                                 std::max<std::uint8_t> (1, note.velocity) });
        list.events.push_back ({ note.endSeconds, static_cast<std::uint8_t> (0x80u | channel), note.pitch, 64 });
    }
    for (const auto& sustain : song.sustainEvents)
        list.events.push_back ({ sustain.seconds, static_cast<std::uint8_t> (0xB0u | (sustain.channel & 0x0F)), 64,
                                 sustain.value });

    std::stable_sort (list.events.begin(), list.events.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b) {
        if (a.seconds != b.seconds)
            return a.seconds < b.seconds;
        return orderAtSameTime (a.status, a.data2) < orderAtSameTime (b.status, b.data2);
    });
    return list;
}

} // namespace pianoassist::audio
