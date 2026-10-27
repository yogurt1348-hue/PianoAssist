#pragma once

#include "core/Result.h"

#include <cstdint>
#include <span>

namespace pianoassist::smf
{

/** Largest MIDI file the app will open. Real piano arrangements are far
    smaller; the bound protects against hostile or corrupt input. */
inline constexpr std::size_t maxSmfFileBytes = 16u * 1024u * 1024u;

struct SmfHeader
{
    std::uint16_t format = 0;          ///< 0, 1 or 2 as stored in the file.
    std::uint16_t trackCount = 0;      ///< Declared MTrk chunk count.
    std::uint16_t ticksPerQuarter = 0; ///< PPQ; SMPTE division is rejected.
};

/** Validates the MThd chunk and that the declared MTrk chunks fit inside the
    buffer. Track event parsing arrives in phase 2. Format 2 and SMPTE
    division are rejected with actionable text for the MVP. */
Result<SmfHeader> readSmfHeader (std::span<const std::uint8_t> bytes);

} // namespace pianoassist::smf
