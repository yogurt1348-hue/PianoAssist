#pragma once

#include "core/Result.h"
#include "smf/SmfReader.h"
#include "smf/TempoMap.h"
#include "timeline/Timeline.h"

#include <string>
#include <vector>

namespace pianoassist::timeline
{

/** A source part is one (track, channel) pair: format 0 files put both hands
    and the band on different channels of a single track. */
struct Part
{
    enum class Assignment
    {
        rightHand,
        leftHand,
        splitAtMiddleC, ///< one piano part holding both hands
        backing,
        excluded,       ///< neither checked nor played (a guide or duplicate track)
    };

    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::string trackName;
    int program = -1; ///< first program change, -1 when none
    int bank = -1;    ///< bank select with that first program change, -1 when none
    std::size_t noteCount = 0;
    std::uint8_t lowestPitch = 127;
    std::uint8_t highestPitch = 0;
    double meanPitch = 0.0;
    Assignment assignment = Assignment::backing;
    std::string reason; ///< why the assignment was inferred; shown in diagnostics
    bool guessedFromPitch = false; ///< low confidence: the user should check it before practising
    bool setByUser = false;
};

const char* toString (Part::Assignment assignment) noexcept;

/** Notes below this pitch go to the left hand when a part is split. */
inline constexpr std::uint8_t handSplitPitch = 60; // C4

struct TimeSignatureAt
{
    double seconds = 0.0;
    std::int64_t tick = 0;
    int numerator = 4;
    int denominator = 4;
};

/** A program change of one part during the song (the first one included). */
struct ProgramChange
{
    double seconds = 0.0;
    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::uint8_t program = 0;
    int bank = -1;
};

struct SustainEvent
{
    double seconds = 0.0;
    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::uint8_t value = 0; ///< >= 64 is pedal down
};

/** The canonical, immutable song model built from a MIDI file. */
struct Song
{
    Timeline timeline;
    smf::TempoMap tempoMap;
    std::vector<TimeSignatureAt> timeSignatures;
    std::vector<Part> parts;
    std::vector<SustainEvent> sustainEvents; ///< sorted by time
    std::vector<ProgramChange> programChanges; ///< sorted by time
    std::vector<std::string> trackNames;
    std::vector<std::string> repairs;
    std::vector<std::string> warnings;
    int format = 1;
    double durationSeconds = 0.0;

    /** Index into parts for a note, or -1. */
    int partIndexOf (const Note& note) const noexcept;
    int partIndexOf (std::uint16_t track, std::uint8_t channel) const noexcept;
};

/** Builds the timeline: ticks to seconds through the tempo map, roles from
    the inferred part assignments. Never assumes a fixed tempo. */
Result<Song> buildSong (const smf::SmfFile& file);

/** Start time of every bar from the time signatures and tempo map, up to the
    end of the song. Bounded so a corrupt file cannot produce millions. */
std::vector<double> barStartSeconds (const Song& song, std::size_t maxBars = 20'000);

/** The song with one part reassigned by the user; note roles follow. Fails
    only for an index out of range. */
Result<Song> reassignPart (const Song& song, std::size_t partIndex, Part::Assignment assignment);

/** True when some hand assignment was only guessed from pitch. */
bool needsPartCheck (const Song& song) noexcept;

/** Infers each part's assignment. Exposed for tests. */
std::vector<Part> inferParts (const smf::SmfFile& file);

} // namespace pianoassist::timeline
