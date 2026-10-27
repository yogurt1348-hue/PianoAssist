#pragma once

#include "core/Result.h"
#include "smf/SmfHeader.h"
#include "smf/TempoMap.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pianoassist::smf
{

/** Hard limits on untrusted files; real piano arrangements are far below. */
inline constexpr std::size_t maxSmfEvents = 4'000'000;
inline constexpr std::size_t maxSmfNotes = 1'000'000;

struct SmfNote
{
    std::uint16_t track = 0;
    std::uint8_t channel = 0; ///< 0-15
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 0;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;

    friend bool operator== (const SmfNote&, const SmfNote&) = default;
};

struct SmfTimeSignature
{
    std::int64_t tick = 0;
    std::uint8_t numerator = 4;
    std::uint8_t denominator = 4; ///< actual value, e.g. 8 for x/8

    friend bool operator== (const SmfTimeSignature&, const SmfTimeSignature&) = default;
};

struct SmfProgramChange
{
    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::uint8_t program = 0;
    std::int64_t tick = 0;
    int bank = -1; ///< bank select in force (MSB * 128 + LSB), -1 when the track sent none
};

struct SmfSustain
{
    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::uint8_t value = 0;
    std::int64_t tick = 0;
};

struct SmfTrack
{
    std::uint16_t index = 0;
    std::string name;              ///< sequence/track name meta event, may be empty
    std::int64_t endTick = 0;
};

/** Everything the app uses from a Standard MIDI File, still in ticks. */
struct SmfFile
{
    SmfHeader header;
    std::vector<SmfTrack> tracks;
    std::vector<SmfNote> notes;            ///< sorted by start tick, track, pitch
    std::vector<TempoChange> tempoChanges; ///< as found; TempoMap normalizes
    std::vector<SmfTimeSignature> timeSignatures;
    std::vector<SmfProgramChange> programChanges;
    std::vector<SmfSustain> sustainEvents;
    std::vector<std::string> repairs;      ///< predictable fixes applied, for the user and the log
    std::vector<std::string> warnings;     ///< oddities that need no repair
    std::int64_t endTick = 0;
};

/** Parses format 0/1 files. Malformed data fails with actionable text; only
    predictable problems are repaired (missing note-offs are closed at the end
    of their track) and every repair is reported. */
Result<SmfFile> readSmf (std::span<const std::uint8_t> bytes);

} // namespace pianoassist::smf
