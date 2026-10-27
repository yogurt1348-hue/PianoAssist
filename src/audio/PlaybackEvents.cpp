#include "audio/PlaybackEvents.h"

#include <algorithm>
#include <cmath>

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
        return 1; // controllers (bank select too) before programs and new notes
    if (kind == 0xC0u)
        return 2;
    return 3;
}
} // namespace

PlaybackEventList buildPlaybackEvents (const timeline::Song& song, std::uint64_t id,
                                       const std::function<bool (const timeline::Note&)>& include,
                                       const std::function<double (const timeline::Note&)>& gain)
{
    PlaybackEventList list;
    list.id = id;
    list.durationSeconds = song.durationSeconds;
    list.events.reserve (song.timeline.notes().size() * 2 + song.sustainEvents.size());

    for (const auto& note : song.timeline.notes())
    {
        if (include && ! include (note))
            continue;
        const double scale = gain ? std::clamp (gain (note), 0.0, 1.0) : 1.0;
        if (scale <= 0.0)
            continue;
        const auto velocity = static_cast<std::uint8_t> (std::clamp (std::lround (note.velocity * scale), 1L, 127L));
        const auto channel = static_cast<std::uint8_t> (note.channel & 0x0F);
        const auto lane = lane::ofPart (song.partIndexOf (note));
        list.events.push_back ({ note.startSeconds, static_cast<std::uint8_t> (0x90u | channel), note.pitch, velocity, lane });
        list.events.push_back ({ note.endSeconds, static_cast<std::uint8_t> (0x80u | channel), note.pitch, 64, lane });
    }
    for (std::size_t i = 0; i < song.parts.size(); ++i)
    {
        const auto& part = song.parts[i];
        if (part.program >= 0 && part.program <= 127 && list.programs[part.channel & 0x0F] < 0)
            list.programs[part.channel & 0x0F] = static_cast<std::int16_t> (part.program);
        if (i < lane::maxParts)
        {
            auto& start = list.lanes[i];
            start.channel = static_cast<std::uint8_t> (part.channel & 0x0F);
            start.program = static_cast<std::int16_t> (part.program >= 0 && part.program <= 127 ? part.program : -1);
            start.bank = static_cast<std::int16_t> (part.bank >= 0 && part.bank < 16384 ? part.bank : -1);
        }
    }

    // A pedal or program change belongs to its track's part on that channel;
    // one sent on a track without notes applies to every part on the channel.
    const auto lanesFor = [&song] (std::uint16_t track, std::uint8_t channel) {
        std::vector<std::uint8_t> lanes;
        if (const int part = song.partIndexOf (track, channel); part >= 0)
            lanes.push_back (lane::ofPart (part));
        else
            for (std::size_t i = 0; i < song.parts.size(); ++i)
                if (song.parts[i].channel == channel)
                    lanes.push_back (lane::ofPart (static_cast<int> (i)));
        if (lanes.empty())
            lanes.push_back (lane::byChannel);
        return lanes;
    };
    for (const auto& sustain : song.sustainEvents)
        for (const auto l : lanesFor (sustain.track, sustain.channel))
            list.events.push_back ({ sustain.seconds, static_cast<std::uint8_t> (0xB0u | (sustain.channel & 0x0F)), 64,
                                     sustain.value, l });
    for (const auto& change : song.programChanges)
        for (const auto l : lanesFor (change.track, change.channel))
        {
            const auto status = static_cast<std::uint8_t> (0xB0u | (change.channel & 0x0F));
            if (change.bank >= 0)
            {
                list.events.push_back ({ change.seconds, status, 0, static_cast<std::uint8_t> ((change.bank >> 7) & 0x7F), l });
                list.events.push_back ({ change.seconds, status, 32, static_cast<std::uint8_t> (change.bank & 0x7F), l });
            }
            list.events.push_back ({ change.seconds, static_cast<std::uint8_t> (0xC0u | (change.channel & 0x0F)), change.program, 0, l });
        }

    std::stable_sort (list.events.begin(), list.events.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b) {
        if (a.seconds != b.seconds)
            return a.seconds < b.seconds;
        return orderAtSameTime (a.status, a.data2) < orderAtSameTime (b.status, b.data2);
    });
    return list;
}

} // namespace pianoassist::audio
