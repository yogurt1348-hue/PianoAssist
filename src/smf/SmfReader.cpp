#include "smf/SmfReader.h"

#include "smf/ByteReader.h"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <tuple>

namespace pianoassist::smf
{
namespace
{
struct OpenNote
{
    std::int64_t startTick;
    std::uint8_t velocity;
};

class TrackParser
{
public:
    TrackParser (std::uint16_t index, SmfFile& file, std::size_t& eventBudget)
        : index_ (index), file_ (file), eventBudget_ (eventBudget) {}

    std::optional<std::string> parse (std::span<const std::uint8_t> data)
    {
        ByteReader reader (data);
        std::int64_t tick = 0;
        std::uint8_t runningStatus = 0;
        bool ended = false;

        while (! reader.atEnd() && ! ended)
        {
            if (eventBudget_-- == 0)
                return "The MIDI file has too many events to open.";

            const auto delta = reader.readVariableLength();
            if (! delta)
                return where (reader) + "has a malformed time delta.";
            tick += *delta;

            auto status = reader.readU8();
            if (! status)
                return where (reader) + "ends in the middle of an event.";

            if (*status < 0x80)
            {
                if (runningStatus == 0)
                    return where (reader) + "has data without a status byte.";
                // Running status: this byte is the first data byte.
                if (const auto error = channelEvent (runningStatus, *status, reader, tick))
                    return error;
                continue;
            }

            if (*status == 0xFF)
            {
                // The specification cancels running status here, but many
                // sequencers keep using it after a meta event; keep it.
                const auto type = reader.readU8();
                const auto length = reader.readVariableLength();
                if (! type || ! length)
                    return where (reader) + "has a malformed meta event.";
                const auto payload = reader.readBytes (*length);
                if (! payload)
                    return where (reader) + "is truncated inside a meta event.";
                if (const auto error = metaEvent (*type, *payload, tick, ended))
                    return error;
                continue;
            }

            if (*status == 0xF0 || *status == 0xF7)
            {
                runningStatus = 0;
                const auto length = reader.readVariableLength();
                if (! length || ! reader.skip (*length))
                    return where (reader) + "is truncated inside a system exclusive message.";
                continue;
            }

            if (*status >= 0xF0)
                return where (reader) + "contains an unsupported system message.";

            runningStatus = *status;
            const auto first = reader.readU8();
            if (! first || *first >= 0x80)
                return where (reader) + "has a channel message with missing data.";
            if (const auto error = channelEvent (*status, *first, reader, tick))
                return error;
        }

        if (! ended)
            file_.repairs.push_back ("Track " + std::to_string (index_ + 1) + " has no end-of-track marker.");

        auto& track = file_.tracks.back();
        track.endTick = tick;
        closeOpenNotes (tick);
        return std::nullopt;
    }

private:
    std::string where (const ByteReader& reader) const
    {
        return "Track " + std::to_string (index_ + 1) + " (byte " + std::to_string (reader.position()) + ") ";
    }

    std::optional<std::string> channelEvent (std::uint8_t status, std::uint8_t data1, ByteReader& reader,
                                             std::int64_t tick)
    {
        const std::uint8_t kind = status & 0xF0u;
        const std::uint8_t channel = status & 0x0Fu;
        std::uint8_t data2 = 0;
        if (kind != 0xC0u && kind != 0xD0u)
        {
            const auto second = reader.readU8();
            if (! second || *second >= 0x80)
                return where (reader) + "has a channel message with missing data.";
            data2 = *second;
        }

        if (kind == 0x90u && data2 > 0)
        {
            open_[{ channel, data1 }].push_back ({ tick, data2 });
        }
        else if (kind == 0x80u || kind == 0x90u)
        {
            auto& stack = open_[{ channel, data1 }];
            if (! stack.empty())
            {
                // First on, first off: overlapping repeats of one pitch pair in order.
                addNote (channel, data1, stack.front(), tick);
                stack.pop_front();
            }
        }
        else if (kind == 0xB0u && data1 == 64)
        {
            file_.sustainEvents.push_back ({ index_, channel, data2, tick });
        }
        else if (kind == 0xC0u)
        {
            file_.programChanges.push_back ({ index_, channel, data1, tick });
        }
        return std::nullopt;
    }

    std::optional<std::string> metaEvent (std::uint8_t type, std::span<const std::uint8_t> payload, std::int64_t tick,
                                          bool& ended)
    {
        switch (type)
        {
            case 0x2F:
                ended = true;
                break;
            case 0x03:
                if (file_.tracks.back().name.empty())
                    file_.tracks.back().name.assign (payload.begin(), payload.end());
                break;
            case 0x51:
                if (payload.size() != 3)
                    return "Track " + std::to_string (index_ + 1) + " has a malformed tempo event.";
                {
                    const auto micros = static_cast<std::uint32_t> ((payload[0] << 16) | (payload[1] << 8) | payload[2]);
                    if (micros == 0)
                        return "Track " + std::to_string (index_ + 1) + " has a zero tempo.";
                    file_.tempoChanges.push_back ({ tick, micros });
                }
                break;
            case 0x58:
                if (payload.size() < 2 || payload[1] > 7 || payload[0] == 0)
                    return "Track " + std::to_string (index_ + 1) + " has a malformed time signature.";
                file_.timeSignatures.push_back ({ tick, payload[0], static_cast<std::uint8_t> (1u << payload[1]) });
                break;
            default:
                break;
        }
        return std::nullopt;
    }

    void addNote (std::uint8_t channel, std::uint8_t pitch, const OpenNote& open, std::int64_t endTick)
    {
        file_.notes.push_back ({ index_, channel, pitch, open.velocity, open.startTick, endTick });
    }

    void closeOpenNotes (std::int64_t trackEnd)
    {
        std::size_t repaired = 0;
        for (auto& [key, stack] : open_)
            for (const auto& open : stack)
            {
                addNote (key.first, key.second, open, std::max (trackEnd, open.startTick));
                ++repaired;
            }
        if (repaired > 0)
            file_.repairs.push_back ("Track " + std::to_string (index_ + 1) + ": closed " + std::to_string (repaired)
                                     + " note(s) without a note-off at the end of the track.");
        open_.clear();
    }

    std::uint16_t index_;
    SmfFile& file_;
    std::size_t& eventBudget_;
    std::map<std::pair<std::uint8_t, std::uint8_t>, std::deque<OpenNote>> open_;
};

bool chunkIdIs (std::span<const std::uint8_t> id, const char (&expected)[5])
{
    return std::equal (id.begin(), id.end(), expected, expected + 4,
                       [] (std::uint8_t a, char b) { return a == static_cast<std::uint8_t> (b); });
}
} // namespace

Result<SmfFile> readSmf (std::span<const std::uint8_t> bytes)
{
    using R = Result<SmfFile>;

    auto header = readSmfHeader (bytes);
    if (! header)
        return R::fail (header.error());

    SmfFile file;
    file.header = header.value();

    ByteReader reader (bytes);
    // readSmfHeader already validated the header and every chunk bound.
    reader.skip (4);
    const auto headerLength = reader.readU32();
    reader.skip (*headerLength);

    std::size_t eventBudget = maxSmfEvents;
    std::uint16_t trackIndex = 0;
    while (trackIndex < file.header.trackCount)
    {
        const auto id = reader.readBytes (4);
        const auto length = reader.readU32();
        const auto data = reader.readBytes (*length);
        if (! chunkIdIs (*id, "MTrk"))
            continue;

        file.tracks.push_back ({ trackIndex, {}, 0 });
        TrackParser parser (trackIndex, file, eventBudget);
        if (const auto error = parser.parse (*data))
            return R::fail (*error);
        if (file.notes.size() > maxSmfNotes)
            return R::fail ("The MIDI file has too many notes to open.");
        file.endTick = std::max (file.endTick, file.tracks.back().endTick);
        ++trackIndex;
    }

    std::stable_sort (file.notes.begin(), file.notes.end(), [] (const SmfNote& a, const SmfNote& b) {
        return std::tie (a.startTick, a.track, a.pitch) < std::tie (b.startTick, b.track, b.pitch);
    });
    std::stable_sort (file.timeSignatures.begin(), file.timeSignatures.end(),
                      [] (const auto& a, const auto& b) { return a.tick < b.tick; });
    for (const auto& note : file.notes)
        file.endTick = std::max (file.endTick, note.endTick);

    if (file.notes.empty())
        file.warnings.push_back ("The file contains no notes.");
    return R::ok (std::move (file));
}

} // namespace pianoassist::smf
