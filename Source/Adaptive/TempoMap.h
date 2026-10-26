// TempoMap — musical time (PPQ ticks) <-> nominal time (seconds at the written
// tempo), plus time-signature lookups.
//
// "Nominal seconds" is how long the score takes when played exactly at its
// written tempo map. Adaptive Song Mode measures the performer as a *rate*
// relative to nominal time (rate 1.0 = as written), so tempo changes written
// into the MIDI file are handled for free.
//
// Pure C++, immutable after construction, safe to share between threads.

#pragma once

#include "MidiFile.h"

#include <cstdint>
#include <vector>

namespace cadenza::adaptive
{
struct BarBeat
{
    int bar = 0;          // 0-based
    int beat = 0;         // 0-based within the bar (notated beat unit)
    double beatFraction = 0.0;
};

class TempoMap
{
public:
    // Constant 120 BPM 4/4 at the given PPQ.
    explicit TempoMap(int ppq = 480);
    // Tempo events need not be sorted or start at 0; missing start = 120 BPM.
    TempoMap(int ppq, std::vector<TempoEvent> tempos, std::vector<TimeSignatureEvent> timeSignatures = {});

    static TempoMap fromMidiFile(const MidiFileData& file);

    int ppq() const noexcept { return m_ppq; }
    bool isConstant() const noexcept { return m_segments.size() <= 1; }
    const std::vector<TempoEvent>& tempos() const noexcept { return m_tempos; }
    const std::vector<TimeSignatureEvent>& timeSignatures() const noexcept { return m_timeSignatures; }

    double ticksToSeconds(double tick) const noexcept;
    double secondsToTicks(double seconds) const noexcept;
    double bpmAt(double tick) const noexcept;
    // Nominal ticks per second at `tick`.
    double ticksPerSecondAt(double tick) const noexcept;

    TimeSignatureEvent timeSignatureAt(double tick) const noexcept;
    BarBeat barBeatAt(double tick) const noexcept;

    // Same tempo curve in nominal seconds within `toleranceSec` over [0, untilTick]?
    // (Used to check that two files can share one timeline.)
    bool equivalentTo(const TempoMap& other, double untilTickOfThis, double toleranceSec,
                      double* maxDifferenceSec = nullptr) const;

private:
    struct Segment
    {
        double tick = 0.0;
        double seconds = 0.0;
        double secondsPerTick = 0.0;
    };

    std::size_t segmentForTick(double tick) const noexcept;
    std::size_t segmentForSeconds(double seconds) const noexcept;

    int m_ppq = 480;
    std::vector<TempoEvent> m_tempos;
    std::vector<TimeSignatureEvent> m_timeSignatures;
    std::vector<Segment> m_segments;
};
}
