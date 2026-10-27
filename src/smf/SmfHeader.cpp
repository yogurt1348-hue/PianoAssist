#include "smf/SmfHeader.h"

#include "smf/ByteReader.h"

#include <array>
#include <string>

namespace pianoassist::smf
{
namespace
{
bool chunkIdIs (std::span<const std::uint8_t> id, const char (&expected)[5])
{
    for (std::size_t i = 0; i < 4; ++i)
        if (id[i] != static_cast<std::uint8_t> (expected[i]))
            return false;
    return true;
}
} // namespace

Result<SmfHeader> readSmfHeader (std::span<const std::uint8_t> bytes)
{
    using R = Result<SmfHeader>;

    if (bytes.size() > maxSmfFileBytes)
        return R::fail ("The MIDI file is too large to open.");

    ByteReader reader (bytes);
    const auto id = reader.readBytes (4);
    if (! id || ! chunkIdIs (*id, "MThd"))
        return R::fail ("This is not a Standard MIDI File (missing MThd header).");

    const auto length = reader.readU32();
    if (! length || *length < 6)
        return R::fail ("The MIDI header is truncated or malformed.");

    SmfHeader header;
    const auto format = reader.readU16();
    const auto tracks = reader.readU16();
    const auto division = reader.readU16();
    if (! format || ! tracks || ! division || ! reader.skip (*length - 6))
        return R::fail ("The MIDI header is truncated or malformed.");

    if (*format > 2)
        return R::fail ("Unknown MIDI file format " + std::to_string (*format) + ".");
    if (*format == 2)
        return R::fail ("MIDI format 2 (independent sequences) is not supported. "
                        "Export the song as format 0 or 1.");
    if ((*division & 0x8000u) != 0)
        return R::fail ("SMPTE time division is not supported. "
                        "Export the song with musical (PPQ) timing.");
    if (*division == 0)
        return R::fail ("The MIDI header declares zero ticks per quarter note.");
    if (*tracks == 0)
        return R::fail ("The MIDI file contains no tracks.");
    if (*format == 0 && *tracks != 1)
        return R::fail ("A format 0 MIDI file must contain exactly one track.");

    header.format = *format;
    header.trackCount = *tracks;
    header.ticksPerQuarter = *division;

    // Walk the declared track chunks so a truncated file fails here, before
    // any later stage trusts the lengths. Unknown chunk types are skipped as
    // the SMF specification requires.
    std::uint16_t tracksFound = 0;
    while (tracksFound < header.trackCount)
    {
        const auto chunkId = reader.readBytes (4);
        const auto chunkLength = reader.readU32();
        if (! chunkId || ! chunkLength)
            return R::fail ("The MIDI file is truncated: expected "
                            + std::to_string (header.trackCount) + " tracks, found "
                            + std::to_string (tracksFound) + ".");
        if (! reader.skip (*chunkLength))
            return R::fail ("The MIDI file is truncated inside track "
                            + std::to_string (tracksFound + 1) + ".");
        if (chunkIdIs (*chunkId, "MTrk"))
            ++tracksFound;
    }

    return R::ok (header);
}

} // namespace pianoassist::smf
