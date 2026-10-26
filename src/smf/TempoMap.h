#pragma once

#include "core/Result.h"

#include <cstdint>
#include <vector>

namespace pianoassist::smf
{

/** SMF default when a file has no tempo event before the first note: 120 BPM. */
inline constexpr std::uint32_t defaultMicrosecondsPerQuarter = 500000;

struct TempoChange
{
    std::int64_t tick = 0;
    std::uint32_t microsecondsPerQuarter = defaultMicrosecondsPerQuarter;
};

/** Converts between ticks and seconds across tempo changes. Immutable once
    built; the timeline never assumes a fixed tempo. */
class TempoMap
{
public:
    /** changes may be unsorted and may omit tick 0 (the default tempo is used
        until the first change). Equal ticks keep the last change, matching
        how sequencers resolve stacked tempo events. */
    static Result<TempoMap> create (std::uint16_t ticksPerQuarter, std::vector<TempoChange> changes);

    std::uint16_t ticksPerQuarter() const noexcept { return ticksPerQuarter_; }
    const std::vector<TempoChange>& changes() const noexcept { return changes_; }

    /** Negative ticks extrapolate with the first tempo (used for pre-roll). */
    double tickToSeconds (std::int64_t tick) const noexcept;

    /** Inverse of tickToSeconds; returns fractional ticks. */
    double secondsToTick (double seconds) const noexcept;

    double tempoBpmAt (std::int64_t tick) const noexcept;

private:
    TempoMap() = default;
    std::size_t segmentForTick (std::int64_t tick) const noexcept;
    double secondsPerTick (std::size_t segment) const noexcept;

    std::uint16_t ticksPerQuarter_ = 480;
    std::vector<TempoChange> changes_;     // sorted, first entry at tick 0
    std::vector<double> segmentStartSeconds_;
};

} // namespace pianoassist::smf
