// ScoreFollower — online alignment of a live MIDI performance to a
// ReferencePerformance (symbolic score following).
//
// Algorithm (see docs/ADAPTIVE_SONG_ARCHITECTURE.md §6):
//   * TRACKING: a windowed Viterbi / local-alignment DP over note groups around
//     the current position. Every live Note On updates each cell of the window
//     with stay (another note of the same chord), advance (next group(s), with
//     skip and timing costs), backward and extra-note transitions.
//   * LOST: a global Smith-Waterman alignment of the recent live history
//     against the whole score finds where the performer is; the DP is then
//     re-seeded there (REACQUIRING -> TRACKING).
//   * A tempo estimator turns matched group onsets into a rate (1.0 = as written).
//
// Deterministic, bounded work per note (no allocation after reset() for the
// tracking path). NOT for the audio thread: call from the MIDI or a worker
// thread; publish snapshot() to the audio thread (see TripleBuffer).

#pragma once

#include "PitchMask.h"
#include "ReferencePerformance.h"
#include "TempoEstimator.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace cadenza::adaptive
{
enum class TrackingState : std::uint8_t
{
    Idle,          // nothing matched yet (waiting for the performer to start)
    Tracking,      // confident
    Uncertain,     // matching, but confidence dropped: no aggressive corrections
    Waiting,       // performer stopped advancing: accompaniment must not run away
    Lost,          // position unknown: searching the whole score
    Reacquiring,   // a candidate position was found, confirming it
};

const char* toString(TrackingState state) noexcept;

enum class MatchKind : std::uint8_t
{
    None,          // no note processed yet
    Advance,       // entered a new group
    ChordNote,     // another note of the current group
    Backward,      // jumped back a little (replayed something)
    Extra,         // wrong / extra note, position unchanged
    Reacquired,    // global search re-located the performer
};

const char* toString(MatchKind kind) noexcept;

struct ScoreFollowerConfig
{
    // --- window ---
    int windowBehind = 3;              // groups kept behind the best position
    int windowAhead = 10;              // groups kept ahead of the best position
    int maxJump = 5;                   // groups one note may advance (=> max skipped groups + 1)
    int maxBackward = 2;               // groups one note may move back

    // --- scores / penalties ---
    double matchScore = 1.0;
    double extraNotePenalty = 0.7;     // wrong or extra note
    double skipNotePenalty = 0.45;     // per expected-but-unplayed note jumped over
    double backwardPenalty = 1.2;      // per group moved back
    double timingWeight = 0.6;         // max cost of a badly-timed advance (x2)
    double timingToleranceSec = 0.10;  // absolute part of the timing tolerance
    double timingToleranceRel = 0.30;  // relative to the expected inter-onset interval
    double lateTimingDiscount = 0.35;  // late notes are less suspicious than early ones
    double chordSpreadSec = 0.18;      // later "same chord" notes cost spreadPenalty
    double spreadPenalty = 0.4;

    // --- confidence + state machine ---
    double confidenceSmoothing = 0.3;  // EMA weight of a new note's quality
    double ambiguityMargin = 1.0;      // score margin regarded as unambiguous
    double uncertainBelow = 0.45;
    double trackingAbove = 0.6;
    double lostBelow = 0.2;
    int    lostAfterMisses = 5;        // consecutive extra notes => LOST
    double waitGraceSec = 0.35;        // late by this + waitRelative x IOI => WAITING
    double waitRelative = 0.5;

    // --- re-acquisition ---
    int    historyLength = 10;         // recent live onsets kept for the global search
    int    reacquireMinMatches = 4;    // matched notes needed to accept a candidate
    double reacquireMargin = 1.0;      // over the best competing location
    int    reacquireConfirmNotes = 3;  // consecutive matches to return to TRACKING

    // --- start ---
    int  startGroup = 0;               // where the performer is expected to start
    bool startAnywhere = false;        // locate the first notes with the global search

    TempoEstimatorConfig tempo;
};

// Compact state published to the transport (trivially copyable).
struct FollowerSnapshot
{
    TrackingState state = TrackingState::Idle;
    std::uint32_t epoch = 0;          // bumps on reset / seek / re-acquisition (hard re-anchor)
    int groupIndex = -1;              // last matched group, -1 before the first match
    double anchorNominalSec = 0.0;    // nominal time of that group's onset
    double anchorLiveSec = 0.0;       // live time it was played
    double holdNominalSec = 0.0;      // nominal time of the next unplayed group (or song end)
    double rate = 1.0;                // performer rate relative to the written tempo
    double confidence = 0.0;
    double lastNoteLiveSec = 0.0;
};

// Per-note diagnostics (what the follower decided and why).
struct FollowerNoteResult
{
    int pitch = -1;
    double liveSec = 0.0;
    MatchKind kind = MatchKind::None;
    int groupIndex = -1;              // best group after this note
    double expectedLiveSec = 0.0;     // when that group was expected (0 if unknown)
    double groupTick = 0.0;
    double rate = 1.0;
    double confidence = 0.0;
    double margin = 0.0;
    TrackingState state = TrackingState::Idle;
};

class ScoreFollower
{
public:
    explicit ScoreFollower(ScoreFollowerConfig config = {});

    void setReference(std::shared_ptr<const ReferencePerformance> reference);
    const std::shared_ptr<const ReferencePerformance>& reference() const noexcept { return m_reference; }
    void setConfig(const ScoreFollowerConfig& config);
    const ScoreFollowerConfig& config() const noexcept { return m_config; }

    // Back to IDLE at config().startGroup (keeps reference + config).
    void reset();
    // User seek: expect the performer at `groupIndex` next (bumps epoch).
    void seekToGroup(int groupIndex);

    // Live Note On (velocity > 0). `liveSec` must be monotonic (same clock as update()).
    FollowerNoteResult onNoteOn(int pitch, int velocity, double liveSec);
    // Time-based transitions (WAITING). Call periodically (e.g. 20-100 Hz).
    void update(double nowSec);

    TrackingState state() const noexcept { return m_state; }
    double confidence() const noexcept { return m_confidence; }
    double rate() const noexcept { return m_tempo.rate(); }
    int currentGroup() const noexcept { return m_position; }
    const TempoEstimator& tempoEstimator() const noexcept { return m_tempo; }
    FollowerSnapshot snapshot() const noexcept;

    // Estimated score position at `nowSec` (ticks): last matched onset advanced
    // at the estimated rate, never past the next unplayed group.
    double scorePositionTicks(double nowSec) const noexcept;
    // Written BPM x rate at the current position.
    double estimatedBpm() const noexcept;

    int matchedNotes() const noexcept { return m_matchedNotes; }
    int extraNotes() const noexcept { return m_extraNotes; }

private:
    struct Cell
    {
        bool valid = false;
        double score = 0.0;
        PitchMask consumed;            // pitches of this group already matched
        double enterLive = 0.0;        // live time of the group's first matched note
        double lastLive = 0.0;         // live time of the latest note matched here
        double quality = 0.0;          // EMA of per-note quality along this path
        int misses = 0;                // consecutive extra notes along this path
        int matchesInRow = 0;
        MatchKind lastKind = MatchKind::None;
        bool timed = false;            // enterLive is meaningful
    };

    struct HistoryNote { int pitch = 0; double live = 0.0; };
    struct GlobalCell { double score = 0.0; PitchMask consumed; int matches = 0; double enter = 0.0; bool valid = false; };

    int windowSize() const noexcept { return m_config.windowBehind + m_config.windowAhead + 2; }
    Cell* cellFor(std::vector<Cell>& cells, int group) noexcept;
    const Cell* cellFor(const std::vector<Cell>& cells, int group) const noexcept;
    void seedAt(int group, const Cell& seed);
    void seedStart(int startGroup);
    void recenter(int bestGroup);
    double expectedLive(const Cell& from, int fromGroup, int toGroup) const noexcept;
    double timingCost(double expected, double actual, double nominalGap) const noexcept;
    int unplayedNotes(const Cell& c, int group) const noexcept;
    bool globalSearch(double nowLive, int& outGroup, Cell& outCell, int& outMatches);
    void setState(TrackingState s) noexcept;
    double writtenBpmAt(int group) const noexcept;
    double holdNominal() const noexcept;

    ScoreFollowerConfig m_config;
    std::shared_ptr<const ReferencePerformance> m_reference;
    TempoEstimator m_tempo;

    std::vector<Cell> m_cells, m_next;   // window, index 0 = group m_base (m_base may be -1 = before start)
    int m_base = -1;

    std::vector<HistoryNote> m_history;  // ring, capacity historyLength
    std::size_t m_historyHead = 0;
    std::size_t m_historyCount = 0;
    std::vector<GlobalCell> m_globalPrev, m_globalCur;   // scratch rows for the global search
    int m_searchNotes = 0;               // recent notes the global search may use

    TrackingState m_state = TrackingState::Idle;
    std::uint32_t m_epoch = 0;
    int m_position = -1;                 // best group (-1 = before start)
    int m_lastAnchorGroup = -1;
    double m_anchorLive = 0.0;
    double m_anchorNominal = 0.0;
    double m_confidence = 0.0;
    double m_lastNoteLive = 0.0;
    int m_reacquireMatches = 0;
    int m_matchedNotes = 0;
    int m_extraNotes = 0;
    bool m_started = false;
};
}
