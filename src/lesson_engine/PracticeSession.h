#pragma once

#include "lesson_engine/Gate.h"
#include "lesson_engine/PracticeProfile.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace pianoassist::lesson
{

/** How one key press was judged. */
enum class PressVerdict
{
    acceptedEarly,  ///< required, before the onset, inside the early window
    acceptedOnTime, ///< required, inside [onset - early, onset + late]
    acceptedLate,   ///< required, after the late window (the gate waited)
    alreadyMatched, ///< required pitch, but its multiplicity is used up
    wrongPitch,     ///< not required by the current gate
    tooEarly,       ///< a required pitch, but before the early window: ignored
    noGate,         ///< no gate left (song finished in practice)
};

const char* toString (PressVerdict verdict) noexcept;

struct PressResult
{
    PressVerdict verdict = PressVerdict::noGate;
    std::uint8_t pitch = 0;
    std::size_t gateIndex = 0;
    double offsetMs = 0.0;   ///< press time minus gate onset (logical), negative = early
    bool gatePassed = false; ///< this press completed the gate
};

/** Deterministic gate state machine. Pure: no clock, no threads. Times are
    song seconds; the caller maps input timestamps to song time and tells
    the session when the transport reached the gate boundary.

    While the transport waits at a gate, song time is frozen at the
    boundary, so the caller passes the boundary plus the wall time since
    arrival; such
    presses are still accepted (the gate waits for them) and are classed
    late once beyond the late window. */
class PracticeSession
{
public:
    PracticeSession (std::vector<Gate> gates, PracticeProfile profile);

    /** Restart from a song position: the first gate at or after it is
        current; any partial match and retained presses are dropped. */
    void resetTo (double songSeconds);

    PressResult press (std::uint8_t pitch, double songSeconds);

    const std::vector<Gate>& gates() const noexcept { return gates_; }
    const Gate* currentGate() const noexcept;
    /** Where the transport holds if the current gate is not played: the end
        of its late window. Within the window the song keeps going. */
    std::optional<double> nextBoundarySeconds() const noexcept;

    /** Pitches of the current gate not matched yet, sorted. */
    std::vector<std::uint8_t> missingPitches() const;
    std::vector<std::uint8_t> matchedPitches() const;
    bool finished() const noexcept { return current_ >= gates_.size(); }

    std::size_t passedGateCount() const noexcept { return passed_; }

private:
    struct Retained
    {
        std::uint8_t pitch;
        double songSeconds;
    };

    void advance();
    void retain (std::uint8_t pitch, double songSeconds);
    PressResult judge (std::uint8_t pitch, double songSeconds);

    std::vector<Gate> gates_;
    PracticeProfile profile_;
    std::size_t current_ = 0;
    std::vector<std::uint8_t> matched_;
    std::vector<Retained> retained_; ///< presses that may belong to the next gate
    std::size_t passed_ = 0;
};

} // namespace pianoassist::lesson
