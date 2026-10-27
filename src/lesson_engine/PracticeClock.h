#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace pianoassist::lesson
{

/** What the transport looked like when a key press arrived. */
struct TransportClock
{
    bool waiting = false;              ///< held at a gate boundary
    double positionSeconds = 0.0;      ///< song position (the boundary while waiting)
    std::int64_t arrivalMicros = 0;    ///< wall time the hold began
    double blockStartPosition = 0.0;   ///< song position at the start of the last audio block
    std::int64_t blockStartMicros = 0; ///< wall time of that block
    double rate = 1.0;                 ///< tempo: song seconds per wall second
};

/** Maps a press to song time. While the transport waits, song time is
    frozen at the boundary and the wall time since the hold measures how
    late the press is; while playing, the press is placed relative to the
    last audio block. Wall time becomes song time at the current tempo, and
    the input delay (calibration) is subtracted. After a long wait the gate
    is passed "late"; once the song resumes, presses map to the resumed
    position again, so a wait never shifts later notes. */
inline double songTimeOfPress (const TransportClock& c, std::int64_t pressMicros, double inputOffsetMs) noexcept
{
    const double offset = inputOffsetMs / 1000.0 * c.rate;
    if (c.waiting)
        return c.positionSeconds + static_cast<double> (pressMicros - c.arrivalMicros) / 1.0e6 * c.rate - offset;
    return c.blockStartPosition + static_cast<double> (pressMicros - c.blockStartMicros) / 1.0e6 * c.rate - offset;
}

/** A practice range (A-B). */
struct LoopRange
{
    bool enabled = false;
    double start = 0.0;
    double end = 0.0;

    bool valid() const noexcept { return enabled && end - start >= 0.05; }
};

/** Whether the transport should go back to the loop start now.

    - It goes back once the position reaches the end of the range, or when
      the song finished and the range ends at the song's end.
    - A learner's note that starts inside the range must be played first:
      while its gate is pending (start <= onset < end) the transport may hold
      or run a little past the end, and goes back once the gate is passed.
    Notes that start before A are not replayed after the jump; notes still
    sounding at B are released by the jump, and the pedal with them. */
inline bool loopShouldJump (const LoopRange& loop, bool running, bool justFinished, double positionSeconds,
                            double songDurationSeconds, std::optional<double> pendingGateOnset) noexcept
{
    if (! loop.valid())
        return false;
    const bool atEnd = (running && positionSeconds >= loop.end - 1.0e-3)
                       || (justFinished && loop.end >= songDurationSeconds - 0.05);
    if (! atEnd)
        return false;
    if (pendingGateOnset && *pendingGateOnset >= loop.start - 1.0e-6 && *pendingGateOnset < loop.end)
        return false;
    return true;
}

/** Count-in: one bar of clicks before the song starts or a loop repeats. */
struct CountIn
{
    int beats = 4;
    double beatSeconds = 0.5; ///< wall seconds per click, tempo applied

    double totalSeconds() const noexcept { return beats * beatSeconds; }
};

/** One bar at the given tempo and time signature, slowed or sped up by the
    practice tempo. Very fast beats (under 0.25 s) are counted every other beat. */
inline CountIn makeCountIn (double bpm, int numerator, int denominator, double rate) noexcept
{
    CountIn c;
    bpm = bpm > 1.0 ? bpm : 120.0;
    rate = std::clamp (rate, 0.25, 2.0);
    c.beats = std::clamp (numerator, 1, 12);
    const int unit = denominator > 0 ? denominator : 4;
    c.beatSeconds = 60.0 / bpm * 4.0 / unit / rate;
    if (c.beatSeconds < 0.25 && c.beats % 2 == 0)
    {
        c.beats /= 2;
        c.beatSeconds *= 2.0;
    }
    return c;
}

} // namespace pianoassist::lesson
