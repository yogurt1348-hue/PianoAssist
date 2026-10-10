// AdaptiveTransport — the backing playhead that follows the performer.
//
// A phase-locked loop in *nominal* time (seconds of the written tempo map):
//
//   target   P*  = anchor + (now - anchorLive) * performerRate, capped at the
//                  next reference event the performer has not played yet
//   phase    e   = P* - playhead
//   command  r   = performerRate + clamp(e / phaseCorrectionTime, ±maxCorrection·rate)
//
// so a tempo error is fixed by feed-forward (performer rate) and a phase error
// by a bounded, smooth correction — "same BPM but half a bar ahead" makes the
// band temporarily slower instead of being ignored. The playhead never moves
// backwards (a played snare cannot be undone); a hard seek happens only on an
// explicit re-acquisition / user seek (follower epoch change) with a large
// error, or on a large desynchronisation that persists while TRACKING.
//
// When the performer is late for the next reference event the band brakes
// smoothly and waits just past that event; after a long hold it asks for held
// notes to be released.
//
// Pure math: no allocation, no locks — safe on the audio thread.

#pragma once

#include "ScoreFollower.h"
#include "TempoMap.h"

#include <cstdint>

namespace cadenza::adaptive
{
enum class AdaptiveStartMode : std::uint8_t
{
    WaitForPerformer,   // backing starts at the first reference event when the performer plays it
    FreeRunIntro,       // backing plays from `startNominal` at freeRunRate and waits at the performer's entry
};

struct AdaptiveTransportConfig
{
    double phaseCorrectionTimeSec = 1.0;   // close a phase error over roughly this long
    double maxPhaseCorrection = 0.15;      // |correction| <= this x performer rate
    double uncertainGain = 0.3;            // phase gain while UNCERTAIN
    double maxRateSlewPerSec = 0.8;        // playback rate change per second (rate units)
    double holdToleranceSec = 0.10;        // may run this far (nominal) past an unplayed event
    double brakeTimeSec = 0.10;            // time constant of the approach to the hold point
    double minRateFraction = 0.4;          // never slower than this x performer rate while running
    double seekThresholdSec = 0.6;         // epoch change + |e| above this => hard seek
    double largeDesyncSec = 2.0;           // |e| above this while TRACKING ...
    double largeDesyncHoldSec = 1.5;       // ... for this long => hard seek
    double releaseAfterHoldSec = 1.5;      // held this long => release sounding notes
    AdaptiveStartMode startMode = AdaptiveStartMode::WaitForPerformer;
    double freeRunRate = 1.0;              // intro rate in FreeRunIntro mode
};

// What the playhead did during one block.
struct TransportStep
{
    bool   active = false;          // render the range below
    double fromNominal = 0.0;       // range [from, to] (from exclusive unless fromInclusive)
    double toNominal = 0.0;
    double fromTick = 0.0;
    double toTick = 0.0;
    bool   fromInclusive = false;   // first block after start / seek: include events exactly at `from`
    bool   seeked = false;          // hard reposition happened before this range
    bool   releaseNotes = false;    // release every sounding backing note (seek, long hold)
    bool   holding = false;         // stopped at a hold point
    double playbackRate = 0.0;      // effective rate this block
    double phaseErrorSec = 0.0;     // target - playhead (nominal sec), before this block
    double targetNominal = 0.0;
};

class AdaptiveTransport
{
public:
    explicit AdaptiveTransport(AdaptiveTransportConfig config = {}) noexcept;

    void setConfig(const AdaptiveTransportConfig& config) noexcept { m_config = config; }
    const AdaptiveTransportConfig& config() const noexcept { return m_config; }

    // Tempo map used for nominal <-> tick conversion (must outlive use; immutable).
    void setTempoMap(const TempoMap* map) noexcept { m_map = map; }

    // Stopped, positioned at `startNominal` (nominal sec of the reference timeline).
    void reset(double startNominal) noexcept;
    void start() noexcept;
    void stop() noexcept;
    bool running() const noexcept { return m_running; }

    // Advance by one block. `nowSec` = same clock as the follower's live times.
    TransportStep process(const FollowerSnapshot& follower, double nowSec, double dtSec) noexcept;

    double positionNominal() const noexcept { return m_position; }
    double positionTicks() const noexcept;
    double playbackRate() const noexcept { return m_effectiveRate; }

private:
    double toTicks(double nominal) const noexcept;

    AdaptiveTransportConfig m_config;
    const TempoMap* m_map = nullptr;
    bool m_running = false;
    bool m_started = false;        // follower has started (left IDLE) since start()
    bool m_inclusiveNext = true;
    double m_position = 0.0;
    double m_rate = 1.0;           // smoothed command (before braking)
    double m_effectiveRate = 0.0;
    std::uint32_t m_epoch = 0;
    bool m_haveEpoch = false;
    bool m_epochPending = false;
    double m_desyncSince = -1.0;
    double m_holdSince = -1.0;
    bool m_released = false;
};
}
