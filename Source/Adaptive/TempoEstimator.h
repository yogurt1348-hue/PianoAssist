// TempoEstimator — the performer's tempo as a rate relative to the written
// tempo map (1.0 = as written), from (nominal time, live time) anchor pairs.
//
//   * least-squares slope over the last few anchors (robust to jitter),
//   * a pause (live gap far longer than expected) starts a new segment instead
//     of being read as a very slow tempo,
//   * outliers are rejected unless they persist (a real tempo change),
//   * EMA smoothing, absolute BPM limits and a slew limit (BPM per second).
//
// Pure C++, no allocation after construction (fixed ring of anchors).

#pragma once

#include <array>
#include <cstddef>

namespace cadenza::adaptive
{
struct TempoEstimatorConfig
{
    double minBpm = 30.0;                 // absolute performer limits
    double maxBpm = 300.0;
    double smoothing = 0.5;               // EMA weight of a new measurement (1 = none)
    double maxChangeBpmPerSec = 25.0;     // slew limit of the smoothed estimate
    int    regressionWindow = 4;          // anchors per slope measurement (2..16)
    double minNominalSpanSec = 0.2;       // shortest nominal span worth measuring
    double pauseFactor = 2.5;             // live gap > factor x expected gap => pause
    double pauseMinSec = 1.0;             //   ...and longer than this
    double outlierRatio = 1.6;            // measurement outside [r/k, r*k] => outlier
    int    outliersToAccept = 3;          // consecutive same-side outliers = real change
};

class TempoEstimator
{
public:
    explicit TempoEstimator(TempoEstimatorConfig config = {});

    void setConfig(const TempoEstimatorConfig& config) noexcept;
    const TempoEstimatorConfig& config() const noexcept { return m_config; }

    // Forget all anchors. The next measurements re-acquire the tempo quickly.
    void reset(double initialRate = 1.0) noexcept;
    // Keep the current rate but start a new segment (pause, seek, re-acquisition).
    void breakSegment() noexcept;

    // A matched reference onset: nominal time of the reference event, live time
    // it was played, and the written BPM there (for the absolute BPM limits).
    void addAnchor(double nominalSec, double liveSec, double writtenBpm) noexcept;

    double rate() const noexcept { return m_rate; }
    double lastMeasuredRate() const noexcept { return m_lastMeasured; }
    double bpm(double writtenBpm) const noexcept { return m_rate * writtenBpm; }
    int measurements() const noexcept { return m_measurements; }

    // Clamp a rate to the configured BPM limits at a written tempo.
    double clampRate(double rate, double writtenBpm) const noexcept;

private:
    struct Anchor { double nominal = 0.0; double live = 0.0; };
    static constexpr std::size_t kCapacity = 16;

    bool measure(double& slope) const noexcept;

    TempoEstimatorConfig m_config;
    std::array<Anchor, kCapacity> m_anchors {};
    std::size_t m_count = 0;          // anchors in the current segment (<= capacity)
    std::size_t m_head = 0;           // next write index
    double m_rate = 1.0;
    double m_lastMeasured = 1.0;
    double m_lastUpdateLive = 0.0;
    int m_measurements = 0;           // accepted measurements since reset()
    int m_outlierRun = 0;             // signed: + faster, - slower
};
}
