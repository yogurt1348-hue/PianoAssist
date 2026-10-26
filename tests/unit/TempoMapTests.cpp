#include "smf/TempoMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using pianoassist::smf::TempoChange;
using pianoassist::smf::TempoMap;
using Catch::Approx;

namespace
{
TempoMap makeMap (std::uint16_t ppq, std::vector<TempoChange> changes)
{
    auto map = TempoMap::create (ppq, std::move (changes));
    INFO (map.error());
    REQUIRE (map.isOk());
    return std::move (map).value();
}
} // namespace

TEST_CASE ("Without tempo events the SMF default of 120 BPM applies")
{
    const auto map = makeMap (480, {});
    CHECK (map.tickToSeconds (0) == 0.0);
    CHECK (map.tickToSeconds (480) == Approx (0.5));
    CHECK (map.tickToSeconds (4 * 480) == Approx (2.0));
    CHECK (map.tempoBpmAt (0) == Approx (120.0));
}

TEST_CASE ("Tempo changes accumulate piecewise")
{
    // 120 BPM for one bar, 60 BPM for one bar, then 90 BPM.
    const auto map = makeMap (480, { { 0, 500000 }, { 1920, 1000000 }, { 3840, 666667 } });
    CHECK (map.tickToSeconds (1920) == Approx (2.0));
    CHECK (map.tickToSeconds (1920 + 480) == Approx (3.0));
    CHECK (map.tickToSeconds (3840) == Approx (6.0));
    CHECK (map.tickToSeconds (3840 + 480) == Approx (6.0 + 0.666667));
    CHECK (map.tempoBpmAt (2000) == Approx (60.0));
}

TEST_CASE ("Seconds to ticks inverts ticks to seconds across segments")
{
    const auto map = makeMap (96, { { 0, 400000 }, { 100, 750000 }, { 333, 300000 }, { 1000, 1200000 } });
    for (std::int64_t tick = -50; tick <= 2000; tick += 7)
        CHECK (map.secondsToTick (map.tickToSeconds (tick)) == Approx (static_cast<double> (tick)).margin (1e-6));
}

TEST_CASE ("Unsorted and stacked tempo events are normalized")
{
    const auto map = makeMap (480, { { 960, 1000000 }, { 0, 600000 }, { 960, 250000 } });
    REQUIRE (map.changes().size() == 2);
    CHECK (map.changes()[0].microsecondsPerQuarter == 600000);
    CHECK (map.changes()[1].microsecondsPerQuarter == 250000); // last stacked event wins
}

TEST_CASE ("A first tempo event after tick 0 keeps the default before it")
{
    const auto map = makeMap (480, { { 480, 1000000 } });
    CHECK (map.tickToSeconds (480) == Approx (0.5));
    CHECK (map.tickToSeconds (960) == Approx (1.5));
}

TEST_CASE ("Negative ticks extrapolate with the first tempo for pre-roll")
{
    const auto map = makeMap (480, { { 0, 1000000 } });
    CHECK (map.tickToSeconds (-480) == Approx (-1.0));
    CHECK (map.secondsToTick (-1.0) == Approx (-480.0));
}

TEST_CASE ("Thirty minutes of tempo changes do not drift")
{
    // Alternate 100/140 BPM every bar for 30+ minutes; compare against exact
    // integer microsecond arithmetic.
    std::vector<TempoChange> changes;
    std::int64_t exactMicros = 0;
    const std::int64_t barTicks = 4 * 480;
    for (int bar = 0; bar < 1000; ++bar)
    {
        const std::uint32_t tempo = bar % 2 == 0 ? 600000 : 428571;
        changes.push_back ({ bar * barTicks, tempo });
        exactMicros += static_cast<std::int64_t> (tempo) * 4;
    }
    const auto map = makeMap (480, changes);
    const double seconds = map.tickToSeconds (1000 * barTicks);
    CHECK (seconds > 30 * 60);
    CHECK (seconds == Approx (static_cast<double> (exactMicros) / 1e6).margin (1e-6));
}

TEST_CASE ("Invalid tempo maps are rejected")
{
    CHECK_FALSE (TempoMap::create (0, {}).isOk());
    CHECK_FALSE (TempoMap::create (480, { { 0, 0 } }).isOk());
    CHECK_FALSE (TempoMap::create (480, { { -1, 500000 } }).isOk());
    CHECK_FALSE (TempoMap::create (480, { { 0, 0x1000000 } }).isOk());
}
