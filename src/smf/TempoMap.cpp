#include "smf/TempoMap.h"

#include <algorithm>
#include <string>

namespace pianoassist::smf
{

Result<TempoMap> TempoMap::create (std::uint16_t ticksPerQuarter, std::vector<TempoChange> changes)
{
    using R = Result<TempoMap>;
    if (ticksPerQuarter == 0)
        return R::fail ("Ticks per quarter note must be positive.");

    for (const auto& change : changes)
    {
        if (change.tick < 0)
            return R::fail ("Tempo change at negative tick " + std::to_string (change.tick) + ".");
        // SMF stores tempo in 24 bits; zero would make time stand still.
        if (change.microsecondsPerQuarter == 0 || change.microsecondsPerQuarter > 0xFFFFFFu)
            return R::fail ("Tempo value " + std::to_string (change.microsecondsPerQuarter)
                            + " is outside the valid MIDI range.");
    }

    std::stable_sort (changes.begin(), changes.end(),
                      [] (const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });

    TempoMap map;
    map.ticksPerQuarter_ = ticksPerQuarter;
    map.changes_.push_back ({ 0, defaultMicrosecondsPerQuarter });
    for (const auto& change : changes)
    {
        if (change.tick == map.changes_.back().tick)
            map.changes_.back() = change;
        else
            map.changes_.push_back (change);
    }

    map.segmentStartSeconds_.resize (map.changes_.size());
    map.segmentStartSeconds_[0] = 0.0;
    for (std::size_t i = 1; i < map.changes_.size(); ++i)
    {
        const auto ticks = static_cast<double> (map.changes_[i].tick - map.changes_[i - 1].tick);
        map.segmentStartSeconds_[i] = map.segmentStartSeconds_[i - 1] + ticks * map.secondsPerTick (i - 1);
    }
    return R::ok (std::move (map));
}

double TempoMap::secondsPerTick (std::size_t segment) const noexcept
{
    return static_cast<double> (changes_[segment].microsecondsPerQuarter)
           / (1.0e6 * static_cast<double> (ticksPerQuarter_));
}

std::size_t TempoMap::segmentForTick (std::int64_t tick) const noexcept
{
    const auto it = std::upper_bound (changes_.begin(), changes_.end(), tick,
                                      [] (std::int64_t t, const TempoChange& c) { return t < c.tick; });
    return it == changes_.begin() ? 0 : static_cast<std::size_t> (it - changes_.begin() - 1);
}

double TempoMap::tickToSeconds (std::int64_t tick) const noexcept
{
    const auto segment = segmentForTick (tick);
    const auto offset = static_cast<double> (tick - changes_[segment].tick);
    return segmentStartSeconds_[segment] + offset * secondsPerTick (segment);
}

double TempoMap::secondsToTick (double seconds) const noexcept
{
    const auto it = std::upper_bound (segmentStartSeconds_.begin(), segmentStartSeconds_.end(), seconds);
    const auto segment = it == segmentStartSeconds_.begin()
                             ? std::size_t { 0 }
                             : static_cast<std::size_t> (it - segmentStartSeconds_.begin() - 1);
    return static_cast<double> (changes_[segment].tick)
           + (seconds - segmentStartSeconds_[segment]) / secondsPerTick (segment);
}

double TempoMap::tempoBpmAt (std::int64_t tick) const noexcept
{
    return 60.0e6 / static_cast<double> (changes_[segmentForTick (tick)].microsecondsPerQuarter);
}

} // namespace pianoassist::smf
