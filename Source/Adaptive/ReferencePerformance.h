// ReferencePerformance — the part the live performer plays, as a musical map.
//
// Built from one or more tracks of a MIDI file. It is never played back; the
// ScoreFollower aligns live notes against it. Everything is in musical time
// (PPQ ticks of the source file) with nominal seconds from the tempo map.
//
// Near-simultaneous onsets are merged into NoteGroups (chords), using a
// configurable window measured from the group's first onset, so a chord does
// not require all fingers to land in the same microsecond, while a fast run
// does not chain into one giant group.

#pragma once

#include "MidiFile.h"
#include "PitchMask.h"
#include "TempoMap.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cadenza::adaptive
{
struct ReferenceNote
{
    int pitch = 60;
    std::int64_t onTick = 0;
    std::int64_t offTick = 0;
    int velocity = 100;
    int channel = 0;      // 0..15
    int track = 0;        // source track index
    double onSec = 0.0;   // nominal seconds
};

struct NoteGroup
{
    std::int64_t tick = 0;            // earliest onset in the group
    std::int64_t lastOnsetTick = 0;   // latest onset in the group
    double nominalSec = 0.0;          // nominal time of `tick`
    PitchMask pitches;                // unique expected pitches
    int firstNote = 0;                // index into ReferencePerformance::notes()
    int noteCount = 0;                // notes merged (duplicates included)
    int maxVelocity = 0;

    int size() const noexcept { return pitches.count(); }
};

struct ReferenceBuildOptions
{
    std::vector<int> tracks;               // empty = every track with notes
    int channelFilter = -1;                // 0..15 to keep one channel, -1 = all
    bool excludeDrumChannel = true;        // ignore GM drum channel (9) notes
    double chordGroupingWindowMs = 35.0;   // nominal ms from the group's first onset
    int chordGroupingWindowTicks = -1;     // >= 0 overrides the ms window
};

class ReferencePerformance
{
public:
    ReferencePerformance() = default;

    static ReferencePerformance build(const MidiFileData& file, const ReferenceBuildOptions& options,
                                      std::string* error = nullptr);
    // From already-extracted notes (tests / other sources).
    static ReferencePerformance fromNotes(const TempoMap& tempoMap, std::vector<ReferenceNote> notes,
                                          const ReferenceBuildOptions& options = {});

    bool empty() const noexcept { return m_groups.empty(); }
    int ppq() const noexcept { return m_tempoMap.ppq(); }
    const TempoMap& tempoMap() const noexcept { return m_tempoMap; }
    const std::vector<ReferenceNote>& notes() const noexcept { return m_notes; }
    const std::vector<NoteGroup>& groups() const noexcept { return m_groups; }
    int groupCount() const noexcept { return static_cast<int>(m_groups.size()); }
    const NoteGroup& group(int i) const noexcept { return m_groups[static_cast<std::size_t>(i)]; }

    // First group whose tick >= `tick` (groupCount() if none).
    int groupAtOrAfter(double tick) const noexcept;
    // Last group whose tick <= `tick` (-1 if none).
    int groupAtOrBefore(double tick) const noexcept;

    std::int64_t endTick() const noexcept { return m_endTick; }

private:
    void buildGroups(const ReferenceBuildOptions& options);

    TempoMap m_tempoMap;
    std::vector<ReferenceNote> m_notes;
    std::vector<NoteGroup> m_groups;
    std::int64_t m_endTick = 0;
};
}
