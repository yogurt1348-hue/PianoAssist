#pragma once

#include <cstdint>
#include <string_view>

namespace pianoassist::timeline
{

/** Who is expected to play a note. Inferred at load time, correctable by the
    user later; inference can be wrong, so it is always visible. */
enum class NoteRole : std::uint8_t
{
    unknown,
    rightHand,
    leftHand,
    backing,
};

constexpr std::string_view toString (NoteRole role) noexcept
{
    switch (role)
    {
        case NoteRole::rightHand: return "rightHand";
        case NoteRole::leftHand:  return "leftHand";
        case NoteRole::backing:   return "backing";
        case NoteRole::unknown:   break;
    }
    return "unknown";
}

using NoteId = std::uint32_t;

/** One normalized note. Ticks are kept for exact musical positions; seconds
    come from the tempo map and drive playback, rendering and gating. */
struct Note
{
    NoteId id = 0;
    std::uint16_t trackId = 0;
    std::uint8_t channel = 0;  ///< 0-15
    std::uint8_t pitch = 60;   ///< MIDI 0-127
    std::uint8_t velocity = 100;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    NoteRole role = NoteRole::unknown;

    friend bool operator== (const Note&, const Note&) = default;
};

} // namespace pianoassist::timeline
