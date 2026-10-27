// Latency test matching and the "steadily late" hint, from real numbers:
// on 2026-10-27 the owner played about 300 ms after every note.
#include "lesson_engine/Latency.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pianoassist::lesson;

TEST_CASE ("Taps are matched to their nearest click, skipping warm-up clicks and double presses")
{
    const std::int64_t interval = 1'000'000;
    std::vector<std::int64_t> clicks;
    for (int i = 0; i < 8; ++i)
        clicks.push_back (5'000'000 + i * interval);

    std::vector<std::int64_t> taps;
    for (int i = 0; i < 8; ++i)
        taps.push_back (clicks[static_cast<std::size_t> (i)] + 290'000 + (i % 2 == 0 ? 10'000 : -10'000));
    taps.push_back (clicks[5] + 300'000);   // pressed twice on one click
    taps.push_back (clicks[7] + 2'000'000); // long after the last click: ignored

    const auto diffs = matchTapsToClicks (clicks, taps, 2, interval);
    REQUIRE (diffs.size() == 6);
    const auto s = timingStats (diffs);
    CHECK (s.median == Catch::Approx (290.0).margin (10.0));
    CHECK (s.spread() <= 20.0);

    // Pressing ahead of the click counts too (negative delay).
    const auto early = matchTapsToClicks (clicks, { clicks[4] - 40'000 }, 2, interval);
    REQUIRE (early.size() == 1);
    CHECK (early[0] == Catch::Approx (-40.0));
}

TEST_CASE ("Percentiles interpolate and an empty set gives zeros")
{
    const auto s = timingStats ({ 10.0, 20.0, 30.0, 40.0, 50.0 });
    CHECK (s.count == 5);
    CHECK (s.median == 30.0);
    CHECK (s.p25 == 20.0);
    CHECK (s.p75 == 40.0);
    CHECK (s.p10 == Catch::Approx (14.0));
    CHECK (timingStats ({}).count == 0);
}

TEST_CASE ("Steady lateness is reported only when many presses agree")
{
    std::vector<double> late;
    for (int i = 0; i < 20; ++i)
        late.push_back (300.0 + (i % 5) * 10.0 - 20.0);
    const auto found = steadyLateness (late);
    REQUIRE (found.has_value());
    CHECK (*found == Catch::Approx (300.0).margin (10.0));

    CHECK_FALSE (steadyLateness (std::vector<double> (10, 300.0)).has_value()); // too few
    CHECK_FALSE (steadyLateness (std::vector<double> (20, 40.0)).has_value());  // on time
    std::vector<double> scattered;
    for (int i = 0; i < 20; ++i)
        scattered.push_back (i % 2 == 0 ? 0.0 : 400.0);
    CHECK_FALSE (steadyLateness (scattered).has_value()); // not steady: no single delay explains it
}
