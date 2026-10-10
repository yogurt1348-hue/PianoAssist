#include "AdaptiveTransport.h"

#include <algorithm>
#include <cmath>

namespace cadenza::adaptive
{
AdaptiveTransport::AdaptiveTransport(AdaptiveTransportConfig config) noexcept
    : m_config(config)
{
}

void AdaptiveTransport::reset(double startNominal) noexcept
{
    m_running = false;
    m_started = false;
    m_inclusiveNext = true;
    m_position = startNominal;
    m_rate = 1.0;
    m_effectiveRate = 0.0;
    m_haveEpoch = false;
    m_epochPending = false;
    m_desyncSince = -1.0;
    m_holdSince = -1.0;
    m_released = false;
}

void AdaptiveTransport::start() noexcept
{
    m_running = true;
    m_inclusiveNext = true;
}

void AdaptiveTransport::stop() noexcept
{
    m_running = false;
    m_effectiveRate = 0.0;
}

double AdaptiveTransport::toTicks(double nominal) const noexcept
{
    return m_map != nullptr ? m_map->secondsToTicks(nominal) : nominal * 960.0;
}

double AdaptiveTransport::positionTicks() const noexcept
{
    return toTicks(m_position);
}

TransportStep AdaptiveTransport::process(const FollowerSnapshot& s, double now, double dt) noexcept
{
    TransportStep step;
    if (!m_running || !(dt > 0.0)) return step;
    dt = std::min(dt, 0.25);   // a stalled callback must not cause a giant jump

    if (!m_haveEpoch) { m_epoch = s.epoch; m_haveEpoch = true; }
    if (s.epoch != m_epoch) { m_epoch = s.epoch; m_epochPending = true; }

    const double before = m_position;
    const bool performerStarted = s.state != TrackingState::Idle;
    const double hold = s.holdNominalSec;   // +inf after the last reference event
    double rateCmd = 0.0;
    double target = m_position;
    double ceiling = hold + m_config.holdToleranceSec;
    bool late = false;

    if (!performerStarted) {
        m_started = false;
        if (m_config.startMode == AdaptiveStartMode::FreeRunIntro && m_position < hold) {
            rateCmd = m_config.freeRunRate;
            ceiling = hold;   // wait exactly at the performer's entry
            late = true;      // brake into it
            target = m_position;
        } else {
            // Wait for the performer at their entry point.
            if (m_position < s.anchorNominalSec && m_config.startMode == AdaptiveStartMode::WaitForPerformer) {
                m_position = s.anchorNominalSec;
                m_inclusiveNext = true;
            }
            m_effectiveRate = 0.0;
            m_rate = 1.0;
            step.holding = true;
            step.targetNominal = m_position;
            return step;
        }
    } else {
        const double rate = std::max(0.01, s.rate);
        if (!m_started) {
            m_started = true;
            m_rate = rate;    // first performer note: no slew from the intro/idle rate
            m_epochPending = false;
            if (m_config.startMode == AdaptiveStartMode::WaitForPerformer && m_position < s.anchorNominalSec) {
                m_position = s.anchorNominalSec;
                m_inclusiveNext = true;
            }
        }
        target = std::min(s.anchorNominalSec + std::max(0.0, now - s.anchorLiveSec) * rate, ceiling);
        double e = target - m_position;

        // --- hard reposition policy ---
        bool seek = false;
        if (m_epochPending) {
            m_epochPending = false;
            seek = std::abs(e) > m_config.seekThresholdSec;
        }
        if (s.state == TrackingState::Tracking && std::abs(e) > m_config.largeDesyncSec) {
            if (m_desyncSince < 0.0) m_desyncSince = now;
            if (now - m_desyncSince >= m_config.largeDesyncHoldSec) seek = true;
        } else {
            m_desyncSince = -1.0;
        }
        if (seek) {
            m_position = target;
            m_desyncSince = -1.0;
            m_inclusiveNext = true;
            step.seeked = true;
            step.releaseNotes = true;
            e = 0.0;
        }
        step.phaseErrorSec = e;

        // --- PLL: performer rate + bounded phase correction ---
        double gain = 0.0;
        switch (s.state) {
            case TrackingState::Tracking:    gain = 1.0; break;
            case TrackingState::Uncertain:   gain = m_config.uncertainGain; break;
            case TrackingState::Reacquiring: gain = m_config.uncertainGain; break;
            default:                         gain = 0.0; break;   // WAITING / LOST: no corrections
        }
        const double maxCorr = m_config.maxPhaseCorrection * rate;
        const double correction = std::clamp(gain * e / std::max(0.05, m_config.phaseCorrectionTimeSec), -maxCorr, maxCorr);
        rateCmd = std::max(rate + correction, m_config.minRateFraction * rate);

        // Late for the next reference event (or already past it): brake into the hold point.
        const double expectedAtHold = s.anchorLiveSec + (hold - s.anchorNominalSec) / rate;
        late = std::isfinite(hold) && (now > expectedAtHold || m_position >= hold);
    }

    // Slew-limit the command (smooth tempo changes for the band).
    const double maxStep = m_config.maxRateSlewPerSec * dt;
    m_rate = std::clamp(rateCmd, m_rate - maxStep, m_rate + maxStep);
    double rate = std::max(0.0, m_rate);

    if (late) {
        const double room = std::max(0.0, ceiling - m_position);
        rate = std::min(rate, room / std::max(1e-3, m_config.brakeTimeSec));
    }
    double next = std::min(m_position + rate * dt, std::max(m_position, ceiling));
    if (!std::isfinite(next)) next = m_position;
    m_effectiveRate = (next - m_position) / dt;

    const bool holdingNow = m_effectiveRate < 0.02 * std::max(0.05, m_rate);
    if (holdingNow) {
        if (m_holdSince < 0.0) m_holdSince = now;
        if (!m_released && now - m_holdSince >= m_config.releaseAfterHoldSec) {
            step.releaseNotes = true;
            m_released = true;
        }
    } else {
        m_holdSince = -1.0;
        m_released = false;
    }

    step.active = next > before || step.seeked || m_inclusiveNext;
    step.fromNominal = step.seeked ? m_position : before;
    if (step.seeked) step.fromNominal = m_position;
    step.toNominal = next;
    step.fromTick = toTicks(step.fromNominal);
    step.toTick = toTicks(next);
    step.fromInclusive = m_inclusiveNext;
    step.holding = holdingNow;
    step.playbackRate = m_effectiveRate;
    step.targetNominal = target;
    m_inclusiveNext = false;
    m_position = next;
    return step;
}
}
