#include "ScoreFollower.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cadenza::adaptive
{
namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();

PitchMask fullMask(const ReferencePerformance& ref, int group) noexcept
{
    return (group >= 0 && group < ref.groupCount()) ? ref.group(group).pitches : PitchMask {};
}
}

const char* toString(TrackingState state) noexcept
{
    switch (state) {
        case TrackingState::Idle:        return "IDLE";
        case TrackingState::Tracking:    return "TRACKING";
        case TrackingState::Uncertain:   return "UNCERTAIN";
        case TrackingState::Waiting:     return "WAITING";
        case TrackingState::Lost:        return "LOST";
        case TrackingState::Reacquiring: return "REACQUIRING";
    }
    return "?";
}

const char* toString(MatchKind kind) noexcept
{
    switch (kind) {
        case MatchKind::None:       return "none";
        case MatchKind::Advance:    return "advance";
        case MatchKind::ChordNote:  return "chord-note";
        case MatchKind::Backward:   return "backward";
        case MatchKind::Extra:      return "extra";
        case MatchKind::Reacquired: return "reacquired";
    }
    return "?";
}

ScoreFollower::ScoreFollower(ScoreFollowerConfig config)
{
    setConfig(config);
}

void ScoreFollower::setConfig(const ScoreFollowerConfig& config)
{
    m_config = config;
    m_config.windowBehind = std::clamp(m_config.windowBehind, 1, 64);
    m_config.maxJump = std::clamp(m_config.maxJump, 1, 32);
    m_config.windowAhead = std::clamp(m_config.windowAhead, m_config.maxJump, 128);
    m_config.maxBackward = std::clamp(m_config.maxBackward, 0, m_config.windowBehind);
    m_config.historyLength = std::clamp(m_config.historyLength, 2, 64);
    m_config.reacquireMinMatches = std::clamp(m_config.reacquireMinMatches, 2, m_config.historyLength);
    m_config.reacquireConfirmNotes = std::max(1, m_config.reacquireConfirmNotes);
    m_config.lostAfterMisses = std::max(1, m_config.lostAfterMisses);
    m_config.confidenceSmoothing = std::clamp(m_config.confidenceSmoothing, 0.01, 1.0);
    m_tempo.setConfig(m_config.tempo);
    reset();
}

void ScoreFollower::setReference(std::shared_ptr<const ReferencePerformance> reference)
{
    m_reference = std::move(reference);
    reset();
}

void ScoreFollower::reset()
{
    m_cells.assign(static_cast<std::size_t>(windowSize()), Cell {});
    m_next.assign(m_cells.size(), Cell {});
    m_history.assign(static_cast<std::size_t>(m_config.historyLength), HistoryNote {});
    m_historyHead = 0;
    m_historyCount = 0;
    m_tempo.reset(1.0);
    m_state = TrackingState::Idle;
    ++m_epoch;
    m_confidence = 0.0;
    m_reacquireMatches = 0;
    m_searchNotes = 0;
    m_matchedNotes = 0;
    m_extraNotes = 0;
    m_started = false;
    m_lastNoteLive = 0.0;

    const int groups = m_reference ? m_reference->groupCount() : 0;
    const int start = groups > 0 ? std::clamp(m_config.startGroup, 0, groups - 1) : 0;
    seedStart(start);
}

void ScoreFollower::seedStart(int startGroup)
{
    // The performer is expected to play `startGroup` next: seed "group before it,
    // fully played, untimed" (group -1 is the virtual position before the score).
    Cell seed;
    seed.valid = true;
    seed.score = 0.0;
    seed.quality = 0.6;   // a correct first note is trusted right away
    seed.timed = false;
    if (m_reference) seed.consumed = fullMask(*m_reference, startGroup - 1);
    seedAt(startGroup - 1, seed);
    m_position = startGroup - 1;
    m_lastAnchorGroup = startGroup - 1;
    m_anchorNominal = (m_reference && startGroup < m_reference->groupCount())
                          ? m_reference->group(startGroup).nominalSec : 0.0;
    m_anchorLive = 0.0;
}

void ScoreFollower::seekToGroup(int groupIndex)
{
    const int groups = m_reference ? m_reference->groupCount() : 0;
    if (groups == 0) return;
    const auto rate = m_tempo.rate();
    reset();
    m_tempo.reset(rate);   // keep the performer's tempo across a user seek
    seedStart(std::clamp(groupIndex, 0, groups - 1));
}

ScoreFollower::Cell* ScoreFollower::cellFor(std::vector<Cell>& cells, int group) noexcept
{
    const int k = group - m_base;
    return (k >= 0 && k < static_cast<int>(cells.size())) ? &cells[static_cast<std::size_t>(k)] : nullptr;
}

const ScoreFollower::Cell* ScoreFollower::cellFor(const std::vector<Cell>& cells, int group) const noexcept
{
    const int k = group - m_base;
    return (k >= 0 && k < static_cast<int>(cells.size())) ? &cells[static_cast<std::size_t>(k)] : nullptr;
}

void ScoreFollower::seedAt(int group, const Cell& seed)
{
    std::fill(m_cells.begin(), m_cells.end(), Cell {});
    m_base = std::max(-1, group - m_config.windowBehind);
    if (auto* c = cellFor(m_cells, group)) *c = seed;
}

void ScoreFollower::recenter(int bestGroup)
{
    const int newBase = std::max(-1, bestGroup - m_config.windowBehind);
    if (newBase == m_base) return;
    // Shift cells into m_next, then swap (no allocation).
    std::fill(m_next.begin(), m_next.end(), Cell {});
    for (int k = 0; k < static_cast<int>(m_cells.size()); ++k) {
        const int g = m_base + k;
        const int nk = g - newBase;
        if (nk >= 0 && nk < static_cast<int>(m_next.size()))
            m_next[static_cast<std::size_t>(nk)] = m_cells[static_cast<std::size_t>(k)];
    }
    m_cells.swap(m_next);
    m_base = newBase;
}

double ScoreFollower::writtenBpmAt(int group) const noexcept
{
    if (!m_reference || m_reference->empty()) return 120.0;
    const int g = std::clamp(group, 0, m_reference->groupCount() - 1);
    return m_reference->tempoMap().bpmAt(static_cast<double>(m_reference->group(g).tick));
}

double ScoreFollower::expectedLive(const Cell& from, int fromGroup, int toGroup) const noexcept
{
    const auto& ref = *m_reference;
    const double fromNominal = fromGroup >= 0 ? ref.group(fromGroup).nominalSec : ref.group(0).nominalSec;
    return from.enterLive + (ref.group(toGroup).nominalSec - fromNominal) / std::max(0.05, m_tempo.rate());
}

double ScoreFollower::timingCost(double expected, double actual, double nominalGap) const noexcept
{
    const double gapLive = std::max(0.0, nominalGap) / std::max(0.05, m_tempo.rate());
    const double tol = m_config.timingToleranceSec + m_config.timingToleranceRel * gapLive;
    double dev = actual - expected;
    if (dev > 0.0) dev *= m_config.lateTimingDiscount;
    const double x = dev / std::max(1e-3, tol);
    return 2.0 * m_config.timingWeight * (1.0 - std::exp(-0.5 * x * x));
}

int ScoreFollower::unplayedNotes(const Cell& c, int group) const noexcept
{
    if (group < 0 || !m_reference) return 0;
    const auto& pitches = m_reference->group(group).pitches;
    return pitches.count() - (pitches & c.consumed).count();
}

double ScoreFollower::holdNominal() const noexcept
{
    if (!m_reference || m_reference->empty()) return kInf;
    const int next = m_position + 1;
    if (next >= m_reference->groupCount()) return kInf;
    return m_reference->group(std::max(0, next)).nominalSec;
}

void ScoreFollower::setState(TrackingState s) noexcept
{
    m_state = s;
}

FollowerNoteResult ScoreFollower::onNoteOn(int pitch, int /*velocity*/, double liveSec)
{
    FollowerNoteResult result;
    result.pitch = pitch;
    result.liveSec = liveSec;
    m_lastNoteLive = liveSec;

    if (!m_reference || m_reference->empty() || !PitchMask::valid(pitch)) {
        result.kind = MatchKind::Extra;
        result.state = m_state;
        return result;
    }
    const auto& ref = *m_reference;
    const int groups = ref.groupCount();

    m_history[m_historyHead] = { pitch, liveSec };
    m_historyHead = (m_historyHead + 1) % m_history.size();
    m_historyCount = std::min(m_historyCount + 1, m_history.size());
    if (m_state == TrackingState::Lost || (m_state == TrackingState::Idle && m_config.startAnywhere))
        m_searchNotes = std::min<int>(m_searchNotes + 1, static_cast<int>(m_history.size()));

    // ---- LOST / start-anywhere: global re-acquisition ----
    if (m_searchNotes >= m_config.reacquireMinMatches
        && (m_state == TrackingState::Lost || (m_state == TrackingState::Idle && m_config.startAnywhere))) {
        int found = -1;
        int matches = 0;
        Cell cell;
        if (globalSearch(liveSec, found, cell, matches)) {
            seedAt(found, cell);
            m_position = found;
            m_lastAnchorGroup = found;
            m_anchorNominal = ref.group(found).nominalSec;
            m_anchorLive = cell.enterLive;
            m_tempo.breakSegment();
            m_tempo.addAnchor(m_anchorNominal, m_anchorLive, writtenBpmAt(found));
            ++m_epoch;
            m_reacquireMatches = 0;
            m_searchNotes = 0;
            m_started = true;
            m_confidence = cell.quality;
            setState(TrackingState::Reacquiring);
            result.kind = MatchKind::Reacquired;
            result.groupIndex = found;
            result.groupTick = static_cast<double>(ref.group(found).tick);
            result.rate = m_tempo.rate();
            result.confidence = m_confidence;
            result.state = m_state;
            return result;
        }
    }

    // ---- windowed DP step ----
    const double alpha = m_config.confidenceSmoothing;
    const auto blend = [alpha](double q, double sample) { return q + alpha * (sample - q); };

    for (auto& c : m_next) c = Cell {};
    for (int k = 0; k < static_cast<int>(m_cells.size()); ++k) {
        const int j = m_base + k;
        auto& out = m_next[static_cast<std::size_t>(k)];
        const auto consider = [&out](const Cell& cand) {
            if (cand.valid && (!out.valid || cand.score > out.score)) out = cand;
        };
        const Cell& self = m_cells[static_cast<std::size_t>(k)];
        const bool realGroup = j >= 0 && j < groups;
        const bool inGroup = realGroup && ref.group(j).pitches.test(pitch);

        // (c) extra / wrong note: stay put.
        if (self.valid) {
            Cell c = self;
            c.score -= m_config.extraNotePenalty;
            c.misses += 1;
            c.matchesInRow = 0;
            c.quality = blend(self.quality, 0.0);
            c.lastKind = MatchKind::Extra;
            consider(c);
        }
        if (!inGroup) continue;

        // (a) another note of the same group (chord / broken chord).
        if (self.valid && !self.consumed.test(pitch)) {
            const bool late = self.timed && (liveSec - self.enterLive) > m_config.chordSpreadSec;
            Cell c = self;
            c.score += m_config.matchScore - (late ? m_config.spreadPenalty : 0.0);
            c.consumed.set(pitch);
            c.lastLive = liveSec;
            c.misses = 0;
            c.matchesInRow += 1;
            c.quality = blend(self.quality, late ? 0.6 : 1.0);
            c.lastKind = MatchKind::ChordNote;
            consider(c);
        }

        // (b) advance from an earlier group i (skipping j - i - 1 groups).
        int skippedBetween = 0;   // notes of groups strictly between i and j
        for (int i = j - 1; i >= std::max(m_base, j - m_config.maxJump); --i) {
            if (i < j - 1 && i + 1 >= 0) skippedBetween += ref.group(i + 1).size();
            const Cell* from = cellFor(m_cells, i);
            if (from == nullptr || !from->valid) continue;
            const int skipped = unplayedNotes(*from, i) + skippedBetween;
            double tc = 0.0;
            if (from->timed) {
                const double nominalGap = ref.group(j).nominalSec - (i >= 0 ? ref.group(i).nominalSec : 0.0);
                tc = timingCost(expectedLive(*from, i, j), liveSec, nominalGap);
            }
            Cell c;
            c.valid = true;
            c.score = from->score + m_config.matchScore - m_config.skipNotePenalty * skipped - tc;
            c.consumed.set(pitch);
            c.enterLive = liveSec;
            c.lastLive = liveSec;
            c.timed = true;
            c.misses = 0;
            c.matchesInRow = from->matchesInRow + 1;
            const double q = std::clamp(1.0 - tc / (2.0 * std::max(1e-6, m_config.timingWeight)) * 0.5
                                            - 0.15 * skipped, 0.0, 1.0);
            c.quality = blend(from->quality, q);
            c.lastKind = MatchKind::Advance;
            consider(c);
        }

        // (d) small backward move (performer replayed something).
        for (int i = j + 1; i <= j + m_config.maxBackward; ++i) {
            const Cell* from = cellFor(m_cells, i);
            if (from == nullptr || !from->valid) continue;
            Cell c;
            c.valid = true;
            c.score = from->score + m_config.matchScore - m_config.backwardPenalty * (i - j);
            c.consumed.set(pitch);
            c.enterLive = liveSec;
            c.lastLive = liveSec;
            c.timed = true;
            c.matchesInRow = 1;
            c.quality = blend(from->quality, 0.5);
            c.lastKind = MatchKind::Backward;
            consider(c);
        }
    }

    // Best cell (ties -> the earlier group: never run ahead on a coin flip).
    int bestK = -1;
    for (int k = 0; k < static_cast<int>(m_next.size()); ++k)
        if (m_next[static_cast<std::size_t>(k)].valid
            && (bestK < 0 || m_next[static_cast<std::size_t>(k)].score > m_next[static_cast<std::size_t>(bestK)].score + 1e-9))
            bestK = k;
    if (bestK < 0) {   // cannot happen (the extra transition keeps cells alive); be safe
        reset();
        result.kind = MatchKind::Extra;
        result.state = m_state;
        return result;
    }
    const double bestScore = m_next[static_cast<std::size_t>(bestK)].score;
    double second = -kInf;
    for (int k = 0; k < static_cast<int>(m_next.size()); ++k) {
        auto& c = m_next[static_cast<std::size_t>(k)];
        if (!c.valid) continue;
        c.score -= bestScore;   // normalise: best = 0
        if (std::abs(k - bestK) >= 2) second = std::max(second, c.score);
    }
    m_cells.swap(m_next);
    const int best = m_base + bestK;
    const Cell bestCell = m_cells[static_cast<std::size_t>(bestK)];
    const double margin = std::isfinite(second) ? -second : 10.0;

    // ---- position / anchors / tempo ----
    const int previous = m_position;
    m_position = best;
    const bool matched = bestCell.lastKind != MatchKind::Extra;
    if (matched) ++m_matchedNotes; else ++m_extraNotes;

    if (best >= 0 && matched) {
        if (best > m_lastAnchorGroup) {
            m_anchorNominal = ref.group(best).nominalSec;
            m_anchorLive = bestCell.enterLive;
            m_tempo.addAnchor(m_anchorNominal, m_anchorLive, writtenBpmAt(best));
            m_lastAnchorGroup = best;
        } else if (best < m_lastAnchorGroup) {
            // Moved back: re-anchor there, the old tempo segment no longer applies.
            m_anchorNominal = ref.group(best).nominalSec;
            m_anchorLive = bestCell.enterLive;
            m_tempo.breakSegment();
            m_tempo.addAnchor(m_anchorNominal, m_anchorLive, writtenBpmAt(best));
            m_lastAnchorGroup = best;
        }
    }
    (void) previous;

    // ---- confidence ----
    const double ambiguity = 0.6 + 0.4 * std::clamp(margin / std::max(1e-6, m_config.ambiguityMargin), 0.0, 1.0);
    m_confidence = std::clamp(bestCell.quality * ambiguity, 0.0, 1.0);

    // ---- state machine (note-driven transitions) ----
    switch (m_state) {
        case TrackingState::Idle:
            if (matched && best >= 0) {
                m_started = true;
                setState(m_confidence >= m_config.uncertainBelow ? TrackingState::Tracking : TrackingState::Uncertain);
            }
            break;
        case TrackingState::Tracking:
        case TrackingState::Uncertain:
        case TrackingState::Waiting:
            if (bestCell.misses >= m_config.lostAfterMisses || m_confidence < m_config.lostBelow) {
                setState(TrackingState::Lost);
                m_searchNotes = std::min(bestCell.misses, static_cast<int>(m_history.size()));
            } else if (matched) {
                if (m_confidence >= m_config.trackingAbove) setState(TrackingState::Tracking);
                else if (m_confidence < m_config.uncertainBelow) setState(TrackingState::Uncertain);
                else if (m_state == TrackingState::Waiting) setState(TrackingState::Tracking);
            } else if (m_confidence < m_config.uncertainBelow) {
                setState(TrackingState::Uncertain);
            }
            break;
        case TrackingState::Reacquiring:
            if (matched) {
                if (++m_reacquireMatches >= m_config.reacquireConfirmNotes)
                    setState(m_confidence >= m_config.uncertainBelow ? TrackingState::Tracking : TrackingState::Uncertain);
            } else if (bestCell.misses >= 2) {
                setState(TrackingState::Lost);
                m_searchNotes = std::min(bestCell.misses, static_cast<int>(m_history.size()));
            }
            break;
        case TrackingState::Lost:
            // The local window can still recover on its own (performer came back nearby).
            if (matched && bestCell.matchesInRow >= m_config.reacquireConfirmNotes + 1
                && m_confidence >= m_config.uncertainBelow) {
                setState(TrackingState::Tracking);
                m_searchNotes = 0;
            }
            break;
    }

    recenter(std::max(best, -1));

    result.kind = bestCell.lastKind;
    result.groupIndex = best;
    result.groupTick = best >= 0 ? static_cast<double>(ref.group(best).tick) : 0.0;
    result.rate = m_tempo.rate();
    result.confidence = m_confidence;
    result.margin = margin;
    result.state = m_state;
    if (best >= 0 && previous >= 0 && best > previous)
        result.expectedLiveSec = m_anchorLive;
    return result;
}

bool ScoreFollower::globalSearch(double /*nowLive*/, int& outGroup, Cell& outCell, int& outMatches)
{
    const auto& ref = *m_reference;
    const int groups = ref.groupCount();
    const int use = std::min<int>(m_searchNotes, static_cast<int>(m_historyCount));
    if (use < m_config.reacquireMinMatches) return false;

    using G = GlobalCell;
    // Scratch rows reused between searches (allocation only when the reference grows).
    auto& prev = m_globalPrev;
    auto& cur = m_globalCur;
    prev.assign(static_cast<std::size_t>(groups), G {});
    cur.assign(static_cast<std::size_t>(groups), G {});

    for (int h = use - 1; h >= 0; --h) {
        const auto& note = m_history[(m_historyHead + m_history.size() - 1 - static_cast<std::size_t>(h)) % m_history.size()];
        for (int j = 0; j < groups; ++j) {
            G best;
            const auto& gp = ref.group(j).pitches;
            const bool in = gp.test(note.pitch);
            const G& self = prev[static_cast<std::size_t>(j)];
            if (self.valid && self.score - m_config.extraNotePenalty > 0.0) {
                best = self;
                best.score -= m_config.extraNotePenalty;
            }
            if (in) {
                if (m_config.matchScore > best.score) {          // local start here
                    best = G { m_config.matchScore, {}, 1, note.live, true };
                    best.consumed.set(note.pitch);
                }
                if (self.valid && !self.consumed.test(note.pitch) && self.score + m_config.matchScore > best.score) {
                    best = self;
                    best.score += m_config.matchScore;
                    best.consumed.set(note.pitch);
                    best.matches += 1;
                }
                int between = 0;
                for (int i = j - 1; i >= std::max(0, j - m_config.maxJump); --i) {
                    if (i < j - 1) between += ref.group(i + 1).size();
                    const G& from = prev[static_cast<std::size_t>(i)];
                    if (!from.valid) continue;
                    const int skipped = (ref.group(i).size() - (ref.group(i).pitches & from.consumed).count()) + between;
                    const double s = from.score + m_config.matchScore - m_config.skipNotePenalty * skipped;
                    if (s > best.score) {
                        best = G { s, {}, from.matches + 1, note.live, true };
                        best.consumed.set(note.pitch);
                    }
                }
            }
            cur[static_cast<std::size_t>(j)] = best;
        }
        prev.swap(cur);
    }

    int bestJ = -1;
    for (int j = 0; j < groups; ++j)
        if (prev[static_cast<std::size_t>(j)].valid
            && (bestJ < 0 || prev[static_cast<std::size_t>(j)].score > prev[static_cast<std::size_t>(bestJ)].score + 1e-9))
            bestJ = j;
    if (bestJ < 0 || prev[static_cast<std::size_t>(bestJ)].matches < m_config.reacquireMinMatches) return false;

    // Competing distinct locations within the margin: identical repeated
    // passages. Prefer the nearest one at/after the last known position.
    const double bestScore = prev[static_cast<std::size_t>(bestJ)].score;
    const int hint = std::max(0, m_position);
    int chosen = bestJ;
    int ties = 0;
    for (int j = 0; j < groups; ++j) {
        const auto& c = prev[static_cast<std::size_t>(j)];
        if (!c.valid || c.matches < m_config.reacquireMinMatches) continue;
        if (std::abs(j - bestJ) <= m_config.maxJump) continue;
        if (bestScore - c.score < m_config.reacquireMargin) {
            ++ties;
            const auto dist = [hint](int g) { return g >= hint ? g - hint : (hint - g) + 1000000; };
            if (dist(j) < dist(chosen)) chosen = j;
        }
    }
    (void) ties;

    const auto& c = prev[static_cast<std::size_t>(chosen)];
    outGroup = chosen;
    outMatches = c.matches;
    outCell = Cell {};
    outCell.valid = true;
    outCell.score = 0.0;
    outCell.consumed = c.consumed;
    outCell.enterLive = c.enter;
    outCell.lastLive = m_lastNoteLive;
    outCell.timed = true;
    outCell.quality = ties > 0 ? 0.45 : 0.6;
    outCell.matchesInRow = c.matches;
    outCell.lastKind = MatchKind::Reacquired;
    return true;
}

void ScoreFollower::update(double nowSec)
{
    if (!m_reference || m_reference->empty() || !m_started) return;
    if (m_state != TrackingState::Tracking && m_state != TrackingState::Uncertain) return;
    const int next = m_position + 1;
    if (next <= 0 || next >= m_reference->groupCount()) return;
    const double rate = std::max(0.05, m_tempo.rate());
    const double ioi = (m_reference->group(next).nominalSec - m_anchorNominal) / rate;
    const double expected = m_anchorLive + ioi;
    if (nowSec > expected + m_config.waitGraceSec + m_config.waitRelative * std::max(0.0, ioi))
        setState(TrackingState::Waiting);
}

FollowerSnapshot ScoreFollower::snapshot() const noexcept
{
    FollowerSnapshot s;
    s.state = m_state;
    s.epoch = m_epoch;
    s.groupIndex = m_position;
    s.anchorNominalSec = m_anchorNominal;
    s.anchorLiveSec = m_anchorLive;
    s.holdNominalSec = m_started ? holdNominal() : m_anchorNominal;
    s.rate = m_tempo.rate();
    s.confidence = m_confidence;
    s.lastNoteLiveSec = m_lastNoteLive;
    return s;
}

double ScoreFollower::scorePositionTicks(double nowSec) const noexcept
{
    if (!m_reference || m_reference->empty()) return 0.0;
    const auto& map = m_reference->tempoMap();
    if (!m_started) return map.secondsToTicks(m_anchorNominal);
    double nominal = m_anchorNominal + std::max(0.0, nowSec - m_anchorLive) * m_tempo.rate();
    nominal = std::min(nominal, holdNominal());
    return map.secondsToTicks(nominal);
}

double ScoreFollower::estimatedBpm() const noexcept
{
    return m_tempo.rate() * writtenBpmAt(std::max(0, m_position));
}
}
