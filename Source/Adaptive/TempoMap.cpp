#include "TempoMap.h"

#include <algorithm>
#include <cmath>

namespace cadenza::adaptive
{
TempoMap::TempoMap(int ppq) : TempoMap(ppq, {}, {}) {}

TempoMap::TempoMap(int ppq, std::vector<TempoEvent> tempos, std::vector<TimeSignatureEvent> timeSignatures)
    : m_ppq(ppq > 0 ? ppq : 480)
{
    std::stable_sort(tempos.begin(), tempos.end(),
                     [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });
    // Drop invalid entries; when several share a tick, the last one wins.
    for (const auto& t : tempos) {
        if (t.microsPerQuarter == 0 || t.tick < 0) continue;
        if (!m_tempos.empty() && m_tempos.back().tick == t.tick) m_tempos.back() = t;
        else m_tempos.push_back(t);
    }
    if (m_tempos.empty() || m_tempos.front().tick > 0)
        m_tempos.insert(m_tempos.begin(), TempoEvent { 0, 500000 });

    double seconds = 0.0;
    for (std::size_t i = 0; i < m_tempos.size(); ++i) {
        const double spt = m_tempos[i].microsPerQuarter * 1e-6 / m_ppq;
        if (i > 0) {
            const auto& prev = m_segments.back();
            seconds = prev.seconds + (static_cast<double>(m_tempos[i].tick) - prev.tick) * prev.secondsPerTick;
            if (std::abs(prev.secondsPerTick - spt) < 1e-15) continue;   // redundant tempo event
        }
        m_segments.push_back({ static_cast<double>(m_tempos[i].tick), seconds, spt });
    }

    std::stable_sort(timeSignatures.begin(), timeSignatures.end(),
                     [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) { return a.tick < b.tick; });
    for (const auto& ts : timeSignatures) {
        if (ts.numerator <= 0 || ts.denominator <= 0 || ts.tick < 0) continue;
        if (!m_timeSignatures.empty() && m_timeSignatures.back().tick == ts.tick) m_timeSignatures.back() = ts;
        else m_timeSignatures.push_back(ts);
    }
    if (m_timeSignatures.empty() || m_timeSignatures.front().tick > 0)
        m_timeSignatures.insert(m_timeSignatures.begin(), TimeSignatureEvent { 0, 4, 4 });
}

TempoMap TempoMap::fromMidiFile(const MidiFileData& file)
{
    return TempoMap(file.ppq, file.tempos, file.timeSignatures);
}

std::size_t TempoMap::segmentForTick(double tick) const noexcept
{
    // Segments are few (tempo events); upper_bound is O(log n).
    auto it = std::upper_bound(m_segments.begin(), m_segments.end(), tick,
                               [](double t, const Segment& s) { return t < s.tick; });
    return it == m_segments.begin() ? 0 : static_cast<std::size_t>(std::distance(m_segments.begin(), it) - 1);
}

std::size_t TempoMap::segmentForSeconds(double seconds) const noexcept
{
    auto it = std::upper_bound(m_segments.begin(), m_segments.end(), seconds,
                               [](double s, const Segment& seg) { return s < seg.seconds; });
    return it == m_segments.begin() ? 0 : static_cast<std::size_t>(std::distance(m_segments.begin(), it) - 1);
}

double TempoMap::ticksToSeconds(double tick) const noexcept
{
    const auto& s = m_segments[segmentForTick(tick)];
    return s.seconds + (tick - s.tick) * s.secondsPerTick;   // extrapolates linearly before 0
}

double TempoMap::secondsToTicks(double seconds) const noexcept
{
    const auto& s = m_segments[segmentForSeconds(seconds)];
    return s.tick + (seconds - s.seconds) / s.secondsPerTick;
}

double TempoMap::bpmAt(double tick) const noexcept
{
    const auto& s = m_segments[segmentForTick(tick)];
    return 60.0 / (s.secondsPerTick * m_ppq);
}

double TempoMap::ticksPerSecondAt(double tick) const noexcept
{
    return 1.0 / m_segments[segmentForTick(tick)].secondsPerTick;
}

TimeSignatureEvent TempoMap::timeSignatureAt(double tick) const noexcept
{
    TimeSignatureEvent current = m_timeSignatures.front();
    for (const auto& ts : m_timeSignatures) {
        if (static_cast<double>(ts.tick) > tick) break;
        current = ts;
    }
    return current;
}

BarBeat TempoMap::barBeatAt(double tick) const noexcept
{
    BarBeat result;
    if (tick < 0) tick = 0;
    double barStart = 0.0;
    int bar = 0;
    for (std::size_t i = 0; i < m_timeSignatures.size(); ++i) {
        const auto& ts = m_timeSignatures[i];
        const double beatTicks = m_ppq * 4.0 / ts.denominator;
        const double barTicks = beatTicks * ts.numerator;
        // Time-signature changes are expected on bar lines; if not, start counting there.
        const double segStart = std::max(barStart, static_cast<double>(ts.tick));
        const double segEnd = (i + 1 < m_timeSignatures.size())
                                  ? static_cast<double>(m_timeSignatures[i + 1].tick) : tick + 1.0;
        if (tick < segEnd || i + 1 == m_timeSignatures.size()) {
            const double into = tick - segStart;
            const int bars = static_cast<int>(std::floor(into / barTicks));
            result.bar = bar + bars;
            const double inBar = into - bars * barTicks;
            const double beats = inBar / beatTicks;
            result.beat = static_cast<int>(std::floor(beats));
            result.beatFraction = beats - result.beat;
            return result;
        }
        bar += static_cast<int>(std::ceil((segEnd - segStart) / barTicks - 1e-9));
        barStart = segEnd;
    }
    return result;
}

bool TempoMap::equivalentTo(const TempoMap& other, double untilTick, double toleranceSec,
                            double* maxDifferenceSec) const
{
    // Compare the musical-position -> time curves at every tempo-change point of
    // either map (positions expressed as quarter notes so PPQ may differ) and at the end.
    std::vector<double> quarters;
    for (const auto& t : m_tempos) quarters.push_back(static_cast<double>(t.tick) / m_ppq);
    for (const auto& t : other.m_tempos) quarters.push_back(static_cast<double>(t.tick) / other.m_ppq);
    quarters.push_back(untilTick / m_ppq);

    double maxDiff = 0.0;
    const double endQ = untilTick / m_ppq;
    for (double q : quarters) {
        if (q > endQ) continue;
        const double a = ticksToSeconds(q * m_ppq);
        const double b = other.ticksToSeconds(q * other.m_ppq);
        maxDiff = std::max(maxDiff, std::abs(a - b));
    }
    if (maxDifferenceSec != nullptr) *maxDifferenceSec = maxDiff;
    return maxDiff <= toleranceSec;
}
}
