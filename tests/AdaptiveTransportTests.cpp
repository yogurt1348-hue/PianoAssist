// Tests for AdaptiveTransport: phase vs tempo error, braking/hold, seeks, and a
// closed loop (synthetic performer -> ScoreFollower -> AdaptiveTransport).

#include "AdaptiveTestSupport.h"
#include "Adaptive/AdaptiveTransport.h"
#include "Adaptive/ScoreFollower.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>

using namespace adaptive_test;

namespace
{
constexpr double kBlock = 256.0 / 48000.0;   // 5.33 ms audio blocks

FollowerSnapshot tracking(double anchorNominal, double anchorLive, double rate, double hold,
                          std::uint32_t epoch = 1, TrackingState st = TrackingState::Tracking)
{
    FollowerSnapshot s;
    s.state = st;
    s.epoch = epoch;
    s.groupIndex = 0;
    s.anchorNominalSec = anchorNominal;
    s.anchorLiveSec = anchorLive;
    s.holdNominalSec = hold;
    s.rate = rate;
    s.confidence = 0.9;
    return s;
}

void phaseCorrection()
{
    // Performer at exactly rate 1.0; backing starts 0.5 s (nominal) AHEAD.
    // Matching the tempo alone would keep it 0.5 s ahead forever.
    AdaptiveTransport t;
    TempoMap map(480);
    t.setTempoMap(&map);
    t.reset(0.0);
    t.start();
    const auto s = tracking(0.0, 0.0, 1.0, std::numeric_limits<double>::infinity());
    t.process(s, 0.0, kBlock);          // first block: positions at the anchor
    // Force the playhead ahead by simulating an earlier start.
    AdaptiveTransport ahead;
    ahead.setTempoMap(&map);
    ahead.reset(0.5);
    ahead.start();
    double now = 0.0;
    double prevPos = 0.5;
    double minRate = 10.0, maxRate = 0.0;
    bool monotonic = true;
    double e0 = 0.0, e3 = 0.0, eEnd = 0.0;
    for (int i = 0; i < static_cast<int>(10.0 / kBlock); ++i) {
        now += kBlock;
        const auto st = ahead.process(s, now, kBlock);
        if (i == 0) e0 = st.phaseErrorSec;
        if (std::abs(now - 3.0) < kBlock / 2) e3 = st.phaseErrorSec;
        eEnd = st.phaseErrorSec;
        monotonic &= ahead.positionNominal() >= prevPos - 1e-12;
        prevPos = ahead.positionNominal();
        if (i > 5) { minRate = std::min(minRate, st.playbackRate); maxRate = std::max(maxRate, st.playbackRate); }
    }
    expect(near(e0, -0.5, 0.02), "16 phase: initial error -0.5 s (" + num(e0) + ")");
    expect(std::abs(e3) < 0.5 * 0.5, "16 phase: error shrinking after 3 s (" + num(e3) + ")");
    expect(std::abs(eEnd) < 0.02, "16 phase: converged after 10 s (" + num(eEnd) + ")");
    expect(monotonic, "16 phase: playhead never moves backwards");
    expect(minRate >= 1.0 - 0.15 - 1e-6 && maxRate <= 1.0 + 1e-6,
           "16 phase: bounded smooth slow-down only (" + num(minRate) + ".." + num(maxRate) + ")");

    // Behind: catches up with a bounded speed-up.
    AdaptiveTransport behind;
    behind.setTempoMap(&map);
    behind.reset(0.0);
    behind.start();
    now = 0.0;
    const auto s2 = tracking(0.4, 0.0, 1.0, std::numeric_limits<double>::infinity(), 1);
    double maxR = 0;
    for (int i = 0; i < static_cast<int>(8.0 / kBlock); ++i) {
        now += kBlock;
        const auto st = behind.process(s2, now, kBlock);
        if (i > 0) maxR = std::max(maxR, st.playbackRate);
        eEnd = st.phaseErrorSec;
    }
    expect(std::abs(eEnd) < 0.02 && maxR <= 1.15 + 1e-6, "16 phase: behind catches up within bounds ("
                                                             + num(eEnd) + ", max " + num(maxR) + ")");
}

void uncertainIsGentler()
{
    TempoMap map(480);
    const auto run = [&](TrackingState st) {
        AdaptiveTransport t;
        t.setTempoMap(&map);
        t.reset(0.5);
        t.start();
        const auto s = tracking(0.0, 0.0, 1.0, std::numeric_limits<double>::infinity(), 1, st);
        double now = 0, e = 0;
        for (int i = 0; i < static_cast<int>(3.0 / kBlock); ++i) { now += kBlock; e = t.process(s, now, kBlock).phaseErrorSec; }
        return std::abs(e);
    };
    const double tracked = run(TrackingState::Tracking);
    const double uncertain = run(TrackingState::Uncertain);
    expect(uncertain > tracked * 1.3, "UNCERTAIN corrects phase more gently (" + num(uncertain) + " vs " + num(tracked) + ")");
}

void holdWhenPerformerStops()
{
    // Performer anchored at 2.0 s nominal, next event at 2.5 s, then silence.
    TempoMap map(480);
    AdaptiveTransport t;
    t.setTempoMap(&map);
    t.reset(2.0);
    t.start();
    const auto s = tracking(2.0, 10.0, 1.0, 2.5);
    double now = 10.0;
    bool released = false;
    int releases = 0;
    double maxPos = 0;
    bool holding = false;
    for (int i = 0; i < static_cast<int>(4.0 / kBlock); ++i) {
        now += kBlock;
        const auto st = t.process(s, now, kBlock);
        maxPos = std::max(maxPos, t.positionNominal());
        if (st.releaseNotes) { released = true; ++releases; }
        holding = st.holding;
    }
    const double ceiling = 2.5 + t.config().holdToleranceSec;
    expect(maxPos <= ceiling + 1e-9, "10 pause: backing never runs past the next unplayed event (" + num(maxPos) + ")");
    expect(maxPos > 2.5, "10 pause: backing reaches the event itself (" + num(maxPos) + ")");
    expect(holding, "10 pause: holding");
    expect(released && releases == 1, "10 pause: held notes released once after a long hold");

    // 11. Resume: the performer plays the 2.5 s event 3 s late, then continues at rate 1.
    auto s2 = tracking(2.5, now, 1.0, 3.0);
    double eAfter = 1;
    bool seeked = false;
    for (int i = 0; i < static_cast<int>(0.4 / kBlock); ++i) {
        now += kBlock;
        const auto st = t.process(s2, now, kBlock);
        seeked |= st.seeked;
        eAfter = st.phaseErrorSec;
    }
    expect(!seeked, "11 resume: no hard seek for a pause");
    expect(std::abs(eAfter) < 0.1, "11 resume: back in phase quickly (" + num(eAfter) + ")");
    expect(t.playbackRate() > 0.8, "11 resume: band moving again (" + num(t.playbackRate()) + ")");
}

void seekPolicy()
{
    TempoMap map(480);
    AdaptiveTransport t;
    t.setTempoMap(&map);
    t.reset(0.0);
    t.start();
    double now = 0;
    auto s = tracking(0.0, 0.0, 1.0, std::numeric_limits<double>::infinity(), 5);
    for (int i = 0; i < 100; ++i) { now += kBlock; t.process(s, now, kBlock); }

    // Small re-anchor on a new epoch (re-acquired close by): no seek.
    auto near1 = tracking(now + 0.2, now, 1.0, std::numeric_limits<double>::infinity(), 6, TrackingState::Reacquiring);
    now += kBlock;
    auto st = t.process(near1, now, kBlock);
    expect(!st.seeked, "seek policy: small error after re-acquisition corrected softly");

    // Re-acquired 30 s later in the song: hard seek, release notes, inclusive render.
    auto far = tracking(40.0, now, 1.0, std::numeric_limits<double>::infinity(), 7, TrackingState::Reacquiring);
    now += kBlock;
    st = t.process(far, now, kBlock);
    expect(st.seeked && st.releaseNotes && st.fromInclusive, "seek policy: re-acquisition far away seeks");
    expect(near(t.positionNominal(), 40.0, 0.02), "seek policy: positioned at the performer");

    // Same epoch, large error while TRACKING: seeks only after it persists.
    auto drift = tracking(t.positionNominal() + 5.0, now, 1.0, std::numeric_limits<double>::infinity(), 7);
    bool early = false, later = false;
    for (int i = 0; i < static_cast<int>(3.0 / kBlock); ++i) {
        now += kBlock;
        st = t.process(drift, now, kBlock);
        const double since = i * kBlock;
        if (st.seeked && since < 1.4) early = true;
        if (st.seeked && since >= 1.4) later = true;
    }
    expect(!early && later, "seek policy: confirmed large desync seeks after the hold time");

    // WAITING/LOST never seek on their own.
    AdaptiveTransport w;
    w.setTempoMap(&map);
    w.reset(0.0);
    w.start();
    auto lost = tracking(30.0, 0.0, 1.0, 30.5, 1, TrackingState::Lost);
    bool any = false;
    double t2 = 0;
    for (int i = 0; i < 1000; ++i) { t2 += kBlock; any |= w.process(lost, t2, kBlock).seeked; }
    expect(!any, "seek policy: LOST does not jump");
}

void startModes()
{
    TempoMap map(480);
    // WaitForPerformer: idle -> the band waits at the performer's entry (2 s), renders nothing.
    AdaptiveTransport t;
    t.setTempoMap(&map);
    t.reset(0.0);
    t.start();
    FollowerSnapshot idle;
    idle.state = TrackingState::Idle;
    idle.anchorNominalSec = 2.0;
    idle.holdNominalSec = 2.0;
    double now = 0;
    bool anyRange = false;
    for (int i = 0; i < 200; ++i) { now += kBlock; const auto st = t.process(idle, now, kBlock); anyRange |= st.toNominal > st.fromNominal; }
    expect(!anyRange && near(t.positionNominal(), 2.0, 1e-9), "start: waits at the first reference event");
    // First note -> moves from there, first range inclusive.
    auto go = tracking(2.0, now, 1.0, 2.5);
    now += kBlock;
    const auto first = t.process(go, now, kBlock);
    expect(first.active && first.fromInclusive && near(first.fromNominal, 2.0, 1e-9),
           "start: first block includes events exactly at the entry");

    // FreeRunIntro: plays the 2 s intro on its own, then waits at the entry.
    AdaptiveTransportConfig cfg;
    cfg.startMode = AdaptiveStartMode::FreeRunIntro;
    AdaptiveTransport intro(cfg);
    intro.setTempoMap(&map);
    intro.reset(0.0);
    intro.start();
    now = 0;
    for (int i = 0; i < static_cast<int>(1.0 / kBlock); ++i) { now += kBlock; intro.process(idle, now, kBlock); }
    expect(near(intro.positionNominal(), 1.0, 0.03), "free-run intro: plays at the written tempo (" + num(intro.positionNominal()) + ")");
    for (int i = 0; i < static_cast<int>(3.0 / kBlock); ++i) { now += kBlock; intro.process(idle, now, kBlock); }
    expect(intro.positionNominal() <= 2.0 + 1e-9 && intro.positionNominal() > 1.95,
           "free-run intro: waits at the performer's entry (" + num(intro.positionNominal()) + ")");

    // Stopped transport does nothing.
    AdaptiveTransport off;
    expect(!off.process(go, 1.0, kBlock).active, "stopped transport renders nothing");
}

void slewLimit()
{
    TempoMap map(480);
    AdaptiveTransport t;
    t.setTempoMap(&map);
    t.reset(0.0);
    t.start();
    double now = 0;
    auto s = tracking(0.0, 0.0, 1.0, std::numeric_limits<double>::infinity());
    for (int i = 0; i < 50; ++i) { now += kBlock; t.process(s, now, kBlock); }
    // Performer rate jumps to 1.5 (new estimate): the band ramps, it does not jump.
    auto fast = tracking(t.positionNominal(), now, 1.5, std::numeric_limits<double>::infinity());
    double prev = t.playbackRate();
    double worstStep = 0;
    for (int i = 0; i < 400; ++i) {
        now += kBlock;
        const auto st = t.process(fast, now, kBlock);
        worstStep = std::max(worstStep, std::abs(st.playbackRate - prev));
        prev = st.playbackRate;
    }
    expect(worstStep <= t.config().maxRateSlewPerSec * kBlock * 1.01 + 1e-9,
           "slew: rate change per block bounded (" + num(worstStep) + ")");
}

// ------------------------------------------------------------ closed loop

struct LoopResult
{
    double maxErrAfterSettle = 0.0;   // |transport - true performer position| in nominal sec
    double meanErr = 0.0;
    bool monotonic = true;
    bool overranPause = false;
    int seeks = 0;
    double rateStdDev = 0.0;          // band playback-rate wobble (steady part)
};

// The performer plays the reference with `rateAt(nominal)`; between notes the
// "true" performer position is interpolated. Optional pause after group `pauseAfter`.
LoopResult closedLoop(const std::function<double(double)>& rateAt, int pauseAfter = -1, double pauseSec = 0.0,
                      double jitterSec = 0.0)
{
    Lcg jitter;
    auto ref = std::make_shared<const ReferencePerformance>(referenceFromSteps(randomPiece(160, 31)));
    ScoreFollower f;
    f.setReference(ref);
    AdaptiveTransport t;
    t.setTempoMap(&ref->tempoMap());
    t.reset(ref->group(0).nominalSec);
    t.start();

    // performer schedule: live time of each group onset
    std::vector<double> onset(static_cast<std::size_t>(ref->groupCount()));
    double live = 1.0, lastNom = ref->group(0).nominalSec;
    for (int g = 0; g < ref->groupCount(); ++g) {
        const double dn = ref->group(g).nominalSec - lastNom;
        live += dn / rateAt(lastNom + dn / 2);
        if (g == pauseAfter + 1 && pauseAfter >= 0) live += pauseSec;
        lastNom = ref->group(g).nominalSec;
        onset[static_cast<std::size_t>(g)] = live + jitter.symmetric() * jitterSec;
    }
    LoopResult r;
    double now = 0.5;
    int next = 0;
    double prevPos = t.positionNominal();
    double sumErr = 0;
    int samples = 0;
    double sumRate = 0, sumRate2 = 0;
    int rateSamples = 0;
    const double endLive = onset.back();
    while (now < endLive) {
        while (next < ref->groupCount() && onset[static_cast<std::size_t>(next)] <= now) {
            const auto& g = ref->group(next);
            for (int p = 0; p < 128; ++p) if (g.pitches.test(p)) f.onNoteOn(p, 90, onset[static_cast<std::size_t>(next)]);
            ++next;
        }
        f.update(now);
        const auto st = t.process(f.snapshot(), now, kBlock);
        if (st.seeked) ++r.seeks;
        if (next > 16 && next < ref->groupCount() - 1) {
            sumRate += st.playbackRate; sumRate2 += st.playbackRate * st.playbackRate; ++rateSamples;
        }
        r.monotonic &= t.positionNominal() >= prevPos - 1e-12 || st.seeked;
        prevPos = t.positionNominal();

        // true performer position (nominal) at `now`
        if (next > 0 && next < ref->groupCount()) {
            const double t0 = onset[static_cast<std::size_t>(next - 1)], t1 = onset[static_cast<std::size_t>(next)];
            const double n0 = ref->group(next - 1).nominalSec, n1 = ref->group(next).nominalSec;
            const bool paused = pauseAfter >= 0 && next == pauseAfter + 1;
            if (paused) {
                if (t.positionNominal() > n1 + t.config().holdToleranceSec + 1e-9) r.overranPause = true;
            } else if (next > 8) {
                const double truePos = n0 + (n1 - n0) * std::clamp((now - t0) / std::max(1e-6, t1 - t0), 0.0, 1.0);
                const double err = std::abs(t.positionNominal() - truePos);
                // skip the second right after a pause while it re-locks
                if (!(pauseAfter >= 0 && next > pauseAfter && next < pauseAfter + 4)) {
                    r.maxErrAfterSettle = std::max(r.maxErrAfterSettle, err);
                    sumErr += err;
                    ++samples;
                }
            }
        }
        now += kBlock;
    }
    r.meanErr = samples > 0 ? sumErr / samples : 0.0;
    if (rateSamples > 1) {
        const double m = sumRate / rateSamples;
        r.rateStdDev = std::sqrt(std::max(0.0, sumRate2 / rateSamples - m * m));
    }
    return r;
}

void closedLoopScenarios()
{
    const auto steady = closedLoop([](double) { return 1.0; });
    expect(steady.maxErrAfterSettle < 0.08 && steady.meanErr < 0.03,
           "loop exact: max " + num(steady.maxErrAfterSettle) + " mean " + num(steady.meanErr));
    expect(steady.monotonic && steady.seeks == 0, "loop exact: monotonic, no seeks");

    const auto slow = closedLoop([](double) { return 0.75; });
    expect(slow.maxErrAfterSettle < 0.12 && slow.meanErr < 0.05,
           "loop slower: max " + num(slow.maxErrAfterSettle) + " mean " + num(slow.meanErr));

    const auto accel = closedLoop([](double n) { return 0.9 + 0.5 * std::min(1.0, n / 70.0); });
    expect(accel.maxErrAfterSettle < 0.15 && accel.meanErr < 0.06,
           "loop accelerating: max " + num(accel.maxErrAfterSettle) + " mean " + num(accel.meanErr));
    expect(accel.seeks == 0, "loop accelerating: no seeks");

    const auto decel = closedLoop([](double n) { return 1.2 - 0.5 * std::min(1.0, n / 70.0); });
    expect(decel.maxErrAfterSettle < 0.15 && decel.meanErr < 0.06,
           "loop decelerating: max " + num(decel.maxErrAfterSettle) + " mean " + num(decel.meanErr));

    const auto rubato = closedLoop([](double n) { return 1.0 + 0.25 * std::sin(n / 3.0); });
    expect(rubato.meanErr < 0.075 && rubato.seeks == 0, "loop rubato (+-25%): mean " + num(rubato.meanErr));

    // Sloppy timing (+-30 ms) at a steady tempo: the band stays steady too.
    const auto sloppy = closedLoop([](double) { return 1.0; }, -1, 0.0, 0.03);
    expect(sloppy.meanErr < 0.03, "loop jitter: mean " + num(sloppy.meanErr));
    expect(sloppy.rateStdDev < 0.04, "loop jitter: band rate wobble " + num(sloppy.rateStdDev));

    const auto pause = closedLoop([](double) { return 1.0; }, 60, 5.0);
    expect(!pause.overranPause, "loop pause: band waits at the performer's next event");
    expect(pause.seeks == 0 && pause.monotonic, "loop pause: no seek, monotonic");
    expect(pause.maxErrAfterSettle < 0.12, "loop pause: re-locks after resuming (max " + num(pause.maxErrAfterSettle) + ")");
}
}

int main()
{
    phaseCorrection();
    uncertainIsGentler();
    holdWhenPerformerStops();
    seekPolicy();
    startModes();
    slewLimit();
    closedLoopScenarios();

    if (failures == 0) std::cout << "adaptive transport tests: all passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
