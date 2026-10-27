#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace pianoassist::lesson
{

/** Median and spread of a set of timing offsets (milliseconds). */
struct TimingStats
{
    std::size_t count = 0;
    double median = 0.0;
    double p10 = 0.0;
    double p25 = 0.0;
    double p75 = 0.0;
    double p90 = 0.0;

    double spread() const noexcept { return p75 - p25; } ///< interquartile range
};

inline double percentileOfSorted (const std::vector<double>& sorted, double fraction) noexcept
{
    if (sorted.empty())
        return 0.0;
    const double index = std::clamp (fraction, 0.0, 1.0) * static_cast<double> (sorted.size() - 1);
    const auto low = static_cast<std::size_t> (std::floor (index));
    const auto high = std::min (low + 1, sorted.size() - 1);
    return sorted[low] + (sorted[high] - sorted[low]) * (index - static_cast<double> (low));
}

inline TimingStats timingStats (std::vector<double> values)
{
    TimingStats s;
    s.count = values.size();
    if (values.empty())
        return s;
    std::sort (values.begin(), values.end());
    s.median = percentileOfSorted (values, 0.5);
    s.p10 = percentileOfSorted (values, 0.1);
    s.p25 = percentileOfSorted (values, 0.25);
    s.p75 = percentileOfSorted (values, 0.75);
    s.p90 = percentileOfSorted (values, 0.9);
    return s;
}

/** Latency test: the app clicks at known times and the player presses a key
    on each click. Every press is matched to its nearest click; presses more
    than half an interval away, and the first warm-up clicks, are left out.
    Returns press minus click in milliseconds, one value per matched click. */
inline std::vector<double> matchTapsToClicks (const std::vector<std::int64_t>& clickMicros,
                                              const std::vector<std::int64_t>& tapMicros, std::size_t warmUpClicks,
                                              std::int64_t intervalMicros)
{
    std::vector<double> diffs;
    std::vector<bool> used (clickMicros.size(), false);
    for (const auto tap : tapMicros)
    {
        std::optional<std::size_t> best;
        std::int64_t bestDistance = intervalMicros / 2;
        for (std::size_t i = 0; i < clickMicros.size(); ++i)
        {
            const auto distance = std::abs (tap - clickMicros[i]);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        if (! best || *best < warmUpClicks || used[*best])
            continue; // a double press counts once
        used[*best] = true;
        diffs.push_back (static_cast<double> (tap - clickMicros[*best]) / 1000.0);
    }
    return diffs;
}

/** A player who is late by about the same amount on every note is almost
    always following sound that reaches them late (wireless headphones,
    virtual audio devices, large buffers), not playing badly. Returns that
    amount once enough recent presses agree; otherwise nothing. */
inline std::optional<double> steadyLateness (const std::vector<double>& recentOffsetsMs, std::size_t minCount = 16,
                                             double minMedianMs = 120.0, double maxSpreadMs = 120.0)
{
    if (recentOffsetsMs.size() < minCount)
        return std::nullopt;
    const auto s = timingStats (recentOffsetsMs);
    if (s.median < minMedianMs || s.spread() > maxSpreadMs)
        return std::nullopt;
    return s.median;
}

} // namespace pianoassist::lesson
