// AdaptiveSong — builds what Adaptive Song Mode plays from MIDI files:
// which track(s) the performer plays (reference) and which ones Cadenza plays
// (backing), on one shared reference timeline.
//
// Two layouts are supported:
//   * one multitrack file: pick a reference track (+ optional channel) and
//     any set of backing tracks;
//   * reference.mid + backing.mid: the backing timeline is mapped onto the
//     reference one. PPQ differences are rescaled exactly; differing tempo
//     maps or lengths are REPORTED (warnings), never silently assumed equal.
//
// Pure C++. Built on the message thread; the result is immutable and shared.

#pragma once

#include "BackingSequence.h"
#include "MidiFile.h"
#include "ReferencePerformance.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cadenza::adaptive
{
struct TrackInfo
{
    int index = 0;
    std::string name;
    int noteCount = 0;
    std::uint16_t channelMask = 0;     // channels with notes
    bool drums = false;                // uses the GM drum channel (10)
    std::optional<int> program;        // first program change on its main channel
    std::int64_t firstTick = 0;
    std::int64_t lastTick = 0;
    int lowestNote = 127;
    int highestNote = 0;
    double polyphony = 0.0;            // average notes per onset (keyboard parts are > 1)
};

std::vector<TrackInfo> describeTracks(const MidiFileData& file);
std::string describeTrack(const TrackInfo& info);   // "2: Bass (ch 2, 312 notes)"

// Generic heuristic (no song-specific rules): prefer a track whose name says
// piano/keys, then GM piano programs (0-7), then the busiest polyphonic
// non-drum track. Returns -1 if the file has no notes.
int suggestReferenceTrack(const MidiFileData& file);
// Every non-empty track except the reference ones.
std::vector<int> suggestBackingTracks(const MidiFileData& file, const std::vector<int>& referenceTracks);

enum class TimelineAlignment : std::uint8_t
{
    ByBeats,     // same musical positions (quarter notes); PPQ rescaled
    BySeconds,   // same absolute time; mapped through both tempo maps
};

struct AdaptiveSongOptions
{
    std::vector<int> referenceTracks;            // in the reference file
    int referenceChannel = -1;                   // -1 = all channels of those tracks
    std::vector<TrackSelection> backingTracks;   // in the backing file (or the same file)
    ReferenceBuildOptions reference;             // grouping window etc. (tracks/channel taken from above)
    TimelineAlignment alignment = TimelineAlignment::ByBeats;
    std::uint16_t reservedOutputChannels = 0;    // bit c-1: output channel c must not be used (live voices)
    double tempoMismatchToleranceSec = 0.05;
    double lengthMismatchToleranceBeats = 4.0;
};

struct AdaptiveSong
{
    std::shared_ptr<const ReferencePerformance> reference;
    std::shared_ptr<const BackingSequence> backing;
    std::array<std::uint8_t, 16> channelMap {};  // backing source channel -> output channel (0 = dropped)
    double startNominalSec = 0.0;                // first reference event
    double endNominalSec = 0.0;                  // end of the longer of reference/backing
    bool twoFiles = false;
    bool timelineMismatch = false;               // tempo maps differ (two-file mode)
    std::vector<std::string> warnings;
};

struct AdaptiveSongBuildResult
{
    bool ok = false;
    std::string error;
    AdaptiveSong song;
};

// `backingFile` == nullptr: backing tracks come from `referenceFile`.
AdaptiveSongBuildResult buildAdaptiveSong(const MidiFileData& referenceFile, const MidiFileData* backingFile,
                                          const AdaptiveSongOptions& options);

// Output channels for the backing: identity, except source channels whose
// output would collide with a reserved channel move to a free one (never 10
// for melodic parts, drums stay on 10). Returns 0 for a channel that could not be placed.
std::array<std::uint8_t, 16> assignOutputChannels(std::uint16_t sourceChannelsUsed, std::uint16_t reservedOutput);
}
