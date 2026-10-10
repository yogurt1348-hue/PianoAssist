#include "TempoEstimator.h"

#include <algorithm>
#include <cmath>

namespace cadenza::adaptive
{
TempoEstimator::TempoEstimator(TempoEstimatorConfig config)
{
    setConfig(config);
}

void TempoEstimator::setConfig(const TempoEstimatorConfig& config) noexcept
{
    m_config = config;
    m_config.regressionWindow = std::clamp(m_config.regressionWindow, 2, static_cast<int>(kCapacity));
    m_config.smoothing = std::clamp(m_config.smoothing, 0.01, 1.0);
    if (m_config.minBpm <= 0.0) m_config.minBpm = 1.0;
    if (m_config.maxBpm < m_config.minBpm) m_config.maxBpm = m_config.minBpm;
    if (m_config.outlierRatio < 1.0) m_config.outlierRatio = 1.0;
}

void TempoEstimator::reset(double initialRate) noexcept
{
    m_count = 0;
    m_head = 0;
    m_rate = initialRate > 0.0 ? initialRate : 1.0;
    m_lastMeasured = m_rate;
    m_lastUpdateLive = 0.0;
    m_measurements = 0;
    m_outlierRun = 0;
}

void TempoEstimator::breakSegment() noexcept
{
    m_count = 0;
    m_head = 0;
    m_outlierRun = 0;
}

double TempoEstimator::clampRate(double rate, double writtenBpm) const noexcept
{
    if (writtenBpm <= 0.0) writtenBpm = 120.0;
    return std::clamp(rate, m_config.minBpm / writtenBpm, m_config.maxBpm / writtenBpm);
}

bool TempoEstimator::measure(double& slope) const noexcept
{
    const std::size_t n = std::min<std::size_t>(m_count, static_cast<std::size_t>(m_config.regressionWindow));
    if (n < 2) return false;

    // Weighted least squares of nominal (y) against live (x); newer anchors
    // weigh more so the estimate follows gradual tempo changes with less lag.
    double sw = 0, sx = 0, sy = 0;
    double minY = 1e300, maxY = -1e300;
    for (std::size_t k = 0; k < n; ++k) {
        const auto& a = m_anchors[(m_head + kCapacity - 1 - k) % kCapacity];
        const double w = static_cast<double>(n - k);
        sw += w; sx += w * a.live; sy += w * a.nominal;
        minY = std::min(minY, a.nominal); maxY = std::max(maxY, a.nominal);
    }
    if (maxY - minY < m_config.minNominalSpanSec) return false;
    const double mx = sx / sw, my = sy / sw;
    double sxx = 0, sxy = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const auto& a = m_anchors[(m_head + kCapacity - 1 - k) % kCapacity];
        const double w = static_cast<double>(n - k);
        sxx += w * (a.live - mx) * (a.live - mx);
        sxy += w * (a.live - mx) * (a.nominal - my);
    }
    if (sxx <= 1e-12) return false;
    slope = sxy / sxx;
    return slope > 0.0 && std::isfinite(slope);
}

void TempoEstimator::addAnchor(double nominalSec, double liveSec, double writtenBpm) noexcept
{
    if (m_count > 0) {
        const auto& last = m_anchors[(m_head + kCapacity - 1) % kCapacity];
        const double nominalGap = nominalSec - last.nominal;
        const double liveGap = liveSec - last.live;
        if (nominalGap <= 0.0 || liveGap <= 0.0) {
            // Backward / simultaneous anchor (re-played chord): restart the segment here.
            breakSegment();
        } else {
            // Until the tempo is known, only a gap too long even for the slowest
            // allowed tempo counts as a pause (a slow starter is not "pausing").
            const double rateForGap = m_measurements == 0 ? clampRate(0.0, writtenBpm) : m_rate;
            const double expectedLive = nominalGap / rateForGap;
            if (liveGap > m_config.pauseMinSec && liveGap > m_config.pauseFactor * expectedLive)
                breakSegment();   // a pause or fermata, not a tempo
        }
    }

    m_anchors[m_head] = { nominalSec, liveSec };
    m_head = (m_head + 1) % kCapacity;
    m_count = std::min(m_count + 1, kCapacity);

    double measured = 0.0;
    if (!measure(measured))
        return;
    measured = clampRate(measured, writtenBpm);
    m_lastMeasured = measured;

    const bool acquiring = m_measurements < 2;
    if (!acquiring) {
        const double k = m_config.outlierRatio;
        const int side = measured > m_rate * k ? 1 : (measured < m_rate / k ? -1 : 0);
        if (side != 0) {
            m_outlierRun = (m_outlierRun * side > 0) ? m_outlierRun + side : side;
            if (std::abs(m_outlierRun) < m_config.outliersToAccept)
                return;              // probably a mismatch: ignore for now
            // Persistent: a genuine big tempo change. Re-acquire from here.
            m_rate = measured;
            m_outlierRun = 0;
            m_lastUpdateLive = liveSec;
            ++m_measurements;
            return;
        }
        m_outlierRun = 0;
    }

    double target = acquiring ? measured : m_rate + m_config.smoothing * (measured - m_rate);
    if (!acquiring && m_config.maxChangeBpmPerSec > 0.0) {
        const double dt = std::max(0.0, liveSec - m_lastUpdateLive);
        const double maxDelta = m_config.maxChangeBpmPerSec / std::max(1.0, writtenBpm) * dt;
        target = std::clamp(target, m_rate - maxDelta, m_rate + maxDelta);
    }
    m_rate = clampRate(target, writtenBpm);
    m_lastUpdateLive = liveSec;
    ++m_measurements;
}
}
