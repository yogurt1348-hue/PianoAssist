// Scenario tests for the Adaptive Song Mode ScoreFollower + TempoEstimator.
// Performances are synthesised from synthetic references (no copyrighted music):
// tempo curves, timing jitter, chord spread and deliberate mistakes.

#include "AdaptiveTestSupport.h"
#include "Adaptive/ScoreFollower.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

using namespace adaptive_test;

namespace
{
struct LiveNote
{
    int pitch = 60;
    double t = 0.0;
    int trueGroup = -1;   // -1 = not in the score (extra/wrong)
};

struct PerformanceSpec
{
    std::function<double(double)> rateAt = [](double) { return 1.0; };   // rate as a function of nominal sec
    double startLive = 1.0;
    double jitterSec = 0.0;
    double chordSpreadSec = 0.0;
    std::uint32_t seed = 7;
};

// Render every reference group as live notes following a (possibly varying) rate.
std::vector<LiveNote> perform(const ReferencePerformance& ref, const PerformanceSpec& spec)
{
    std::vector<LiveNote> out;
    Lcg rng;
    rng.state = spec.seed;
    double live = spec.startLive;
    double lastNominal = ref.empty() ? 0.0 : ref.group(0).nominalSec;
    for (int g = 0; g < ref.groupCount(); ++g) {
        const auto& grp = ref.group(g);
        const double dn = grp.nominalSec - lastNominal;
        // integrate live time with the rate at the midpoint
        live += dn / std::max(0.05, spec.rateAt(lastNominal + dn * 0.5));
        lastNominal = grp.nominalSec;
        const double onset = live + rng.symmetric() * spec.jitterSec;
        std::vector<int> pitches;
        for (int p = 0; p < 128; ++p) if (grp.pitches.test(p)) pitches.push_back(p);
        // arbitrary (but deterministic) finger order
        if (pitches.size() > 1 && (rng.next() & 1u)) std::reverse(pitches.begin(), pitches.end());
        for (std::size_t i = 0; i < pitches.size(); ++i) {
            const double spread = pitches.size() > 1 ? spec.chordSpreadSec * (static_cast<double>(i) / static_cast<double>(pitches.size() - 1)) : 0.0;
            out.push_back({ pitches[i], onset + spread, g });
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const LiveNote& a, const LiveNote& b) { return a.t < b.t; });
    return out;
}

struct RunStats
{
    int notes = 0;
    int scored = 0;          // notes with a true group
    int correct = 0;         // follower group == true group right after the note
    int finalGroup = -1;
    TrackingState finalState = TrackingState::Idle;
    double finalRate = 1.0;
    std::vector<double> rates;   // rate after each note
    std::vector<TrackingState> states;
};

RunStats run(ScoreFollower& f, const std::vector<LiveNote>& notes, double updateHz = 50.0)
{
    RunStats s;
    double nextUpdate = notes.empty() ? 0.0 : notes.front().t;
    for (const auto& n : notes) {
        while (nextUpdate < n.t) { f.update(nextUpdate); nextUpdate += 1.0 / updateHz; }
        const auto r = f.onNoteOn(n.pitch, 90, n.t);
        ++s.notes;
        if (n.trueGroup >= 0) {
            ++s.scored;
            if (r.groupIndex == n.trueGroup) ++s.correct;
        }
        s.rates.push_back(f.rate());
        s.states.push_back(f.state());
    }
    s.finalGroup = f.currentGroup();
    s.finalState = f.state();
    s.finalRate = f.rate();
    return s;
}

std::shared_ptr<const ReferencePerformance> makeRef(int steps, int ppq = 480, double bpm = 120.0, bool chords = true)
{
    return std::make_shared<const ReferencePerformance>(referenceFromSteps(syntheticPiece(steps, ppq, chords), ppq, bpm));
}

std::shared_ptr<const ReferencePerformance> makeRandomRef(int steps, std::uint32_t seed = 2024)
{
    return std::make_shared<const ReferencePerformance>(referenceFromSteps(randomPiece(steps, seed)));
}

double accuracy(const RunStats& s) { return s.scored > 0 ? static_cast<double>(s.correct) / s.scored : 0.0; }

ScoreFollower makeFollower(std::shared_ptr<const ReferencePerformance> ref, ScoreFollowerConfig cfg = {})
{
    ScoreFollower f(cfg);
    f.setReference(std::move(ref));
    return f;
}

// ---------------------------------------------------------------- scenarios

void exactPerformance()
{
    auto ref = makeRef(64);
    auto f = makeFollower(ref);
    const auto s = run(f, perform(*ref, {}));
    expect(accuracy(s) == 1.0, "1 exact: every note at its group (" + num(accuracy(s)) + ")");
    expect(s.finalGroup == ref->groupCount() - 1, "1 exact: ends on the last group");
    expect(near(s.finalRate, 1.0, 0.02), "1 exact: rate 1.0 (" + num(s.finalRate) + ")");
    expect(s.finalState == TrackingState::Tracking, std::string("1 exact: TRACKING, got ") + toString(s.finalState));
    expect(near(f.estimatedBpm(), 120.0, 2.5), "1 exact: ~120 BPM (" + num(f.estimatedBpm()) + ")");
}

void constantTempo(double rate, const std::string& name)
{
    auto ref = makeRef(64);
    auto f = makeFollower(ref);
    PerformanceSpec spec;
    spec.rateAt = [rate](double) { return rate; };
    spec.jitterSec = 0.015;
    spec.chordSpreadSec = 0.02;
    const auto s = run(f, perform(*ref, spec));
    expect(accuracy(s) >= 0.98, name + ": position accuracy " + num(accuracy(s)));
    expect(near(s.finalRate, rate, 0.04 * rate), name + ": rate " + num(s.finalRate) + " ~ " + num(rate));
    expect(near(f.estimatedBpm(), 120.0 * rate, 5.0), name + ": BPM " + num(f.estimatedBpm()));
}

void gradualTempo(double from, double to, const std::string& name)
{
    auto ref = makeRef(96);
    auto f = makeFollower(ref);
    const double total = ref->group(ref->groupCount() - 1).nominalSec;
    PerformanceSpec spec;
    spec.rateAt = [=](double n) { return from + (to - from) * std::clamp(n / total, 0.0, 1.0); };
    spec.jitterSec = 0.01;
    const auto notes = perform(*ref, spec);
    const auto s = run(f, notes);
    expect(accuracy(s) >= 0.98, name + ": accuracy " + num(accuracy(s)));
    expect(near(s.finalRate, to, 0.07 * to), name + ": final rate " + num(s.finalRate) + " ~ " + num(to));
    // The estimate must move monotonically in the right direction overall (mid-way check).
    const double mid = s.rates[s.rates.size() / 2];
    const double expectedMid = (from + to) * 0.5;
    expect(near(mid, expectedMid, 0.1 * expectedMid), name + ": mid-way rate " + num(mid) + " ~ " + num(expectedMid));
}

// Remove / replace / insert notes in an exact performance.
void mistakes()
{
    auto ref = makeRef(48);
    const auto clean = perform(*ref, {});

    // 6. One wrong note (a single-note group replaced by a semitone neighbour).
    {
        auto notes = clean;
        int idx = -1;
        for (int i = 10; i < static_cast<int>(notes.size()); ++i)
            if (ref->group(notes[static_cast<std::size_t>(i)].trueGroup).size() == 1) { idx = i; break; }
        notes[static_cast<std::size_t>(idx)].pitch += 1;
        notes[static_cast<std::size_t>(idx)].trueGroup = -1;
        auto f = makeFollower(ref);
        const auto s = run(f, notes);
        expect(accuracy(s) >= 0.97, "6 wrong note: accuracy " + num(accuracy(s)));
        expect(s.finalGroup == ref->groupCount() - 1, "6 wrong note: reaches the end");
        bool everLost = false;
        for (auto st : s.states) everLost |= st == TrackingState::Lost;
        expect(!everLost, "6 wrong note: never LOST");
    }
    // 7. Extra note inserted between two groups.
    {
        auto notes = clean;
        LiveNote extra = notes[20];
        extra.pitch = 100;
        extra.t += 0.12;
        extra.trueGroup = -1;
        notes.insert(notes.begin() + 21, extra);
        auto f = makeFollower(ref);
        const auto s = run(f, notes);
        expect(accuracy(s) >= 0.97, "7 extra note: accuracy " + num(accuracy(s)));
        expect(s.finalGroup == ref->groupCount() - 1, "7 extra note: reaches the end");
    }
    // 8. Missing note (a whole single-note group skipped).
    {
        auto notes = clean;
        int idx = -1;
        for (int i = 15; i < static_cast<int>(notes.size()); ++i)
            if (ref->group(notes[static_cast<std::size_t>(i)].trueGroup).size() == 1) { idx = i; break; }
        notes.erase(notes.begin() + idx);
        auto f = makeFollower(ref);
        const auto s = run(f, notes);
        expect(accuracy(s) >= 0.97, "8 missing note: accuracy " + num(accuracy(s)));
        expect(s.finalGroup == ref->groupCount() - 1, "8 missing note: reaches the end");
        expect(near(s.finalRate, 1.0, 0.05), "8 missing note: tempo unaffected (" + num(s.finalRate) + ")");
    }
    // Missing chord tone: chords played without their middle note.
    {
        std::vector<LiveNote> notes;
        for (const auto& n : clean) {
            const auto& g = ref->group(n.trueGroup);
            if (g.size() >= 3 && n.pitch != n.pitch / 1000) {
                int lowest = 0;
                for (int p = 0; p < 128; ++p) if (g.pitches.test(p)) { lowest = p; break; }
                if (n.pitch == lowest + 4) continue;   // drop the third
            }
            notes.push_back(n);
        }
        auto f = makeFollower(ref);
        const auto s = run(f, notes);
        expect(accuracy(s) >= 0.97, "missing chord tones: accuracy " + num(accuracy(s)));
    }
}

void brokenChord()
{
    // 4-note chords on every beat, arpeggiated over 120 ms in random finger order.
    std::vector<Step> steps;
    for (int i = 0; i < 32; ++i) {
        const int root = 48 + (i * 5) % 12;
        steps.push_back({ i * 480LL, { root, root + 4, root + 7, root + 12 } });
    }
    auto ref = std::make_shared<const ReferencePerformance>(referenceFromSteps(steps));
    expect(ref->groupCount() == 32, "9 broken chord: 32 groups");
    auto f = makeFollower(ref);
    PerformanceSpec spec;
    spec.chordSpreadSec = 0.12;
    spec.jitterSec = 0.01;
    const auto s = run(f, perform(*ref, spec));
    expect(accuracy(s) >= 0.97, "9 broken chord: accuracy " + num(accuracy(s)));
    expect(near(s.finalRate, 1.0, 0.06), "9 broken chord: rate stable " + num(s.finalRate));
    expect(s.finalState == TrackingState::Tracking, std::string("9 broken chord: TRACKING, got ") + toString(s.finalState));
}

void repeatedNotes()
{
    // Long runs of the same pitch (only timing tells them apart) and alternating pairs.
    std::vector<Step> steps;
    std::int64_t tick = 0;
    for (int bar = 0; bar < 8; ++bar) {
        for (int i = 0; i < 8; ++i) { steps.push_back({ tick, { 60 } }); tick += 240; }   // 8 repeated C eighths
        for (int i = 0; i < 4; ++i) { steps.push_back({ tick, { i % 2 ? 64 : 62 } }); tick += 240; }
    }
    auto ref = std::make_shared<const ReferencePerformance>(referenceFromSteps(steps));
    {
        auto f = makeFollower(ref);
        PerformanceSpec spec;
        spec.jitterSec = 0.02;
        const auto s = run(f, perform(*ref, spec));
        expect(accuracy(s) >= 0.95, "10 repeated notes: accuracy " + num(accuracy(s)));
        expect(s.finalGroup == ref->groupCount() - 1, "10 repeated notes: reaches the end ("
                                                        + std::to_string(s.finalGroup) + ")");
    }
    {
        // Slower performer on repeated notes: timing must scale with the estimated tempo.
        auto f = makeFollower(ref);
        PerformanceSpec spec;
        spec.rateAt = [](double) { return 0.8; };
        spec.jitterSec = 0.015;
        const auto s = run(f, perform(*ref, spec));
        expect(accuracy(s) >= 0.93, "10 repeated notes (slow): accuracy " + num(accuracy(s)));
    }
}

void pauseAndResume()
{
    auto ref = makeRef(48);
    auto notes = perform(*ref, {});
    // Performer stops for 6 s after group 20, then resumes at the same tempo.
    for (auto& n : notes) if (n.trueGroup > 20) n.t += 6.0;

    auto f = makeFollower(ref);
    bool sawWaiting = false;
    bool waitingBeforeResume = false;
    double positionDuringPause = -1.0;
    int groupAtPause = -1;
    double next = notes.front().t;
    for (const auto& n : notes) {
        while (next < n.t) {
            f.update(next);
            if (f.state() == TrackingState::Waiting) {
                sawWaiting = true;
                if (n.trueGroup == 21) waitingBeforeResume = true;
                positionDuringPause = std::max(positionDuringPause, f.scorePositionTicks(next));
            }
            next += 0.02;
        }
        if (n.trueGroup == 21 && groupAtPause < 0) groupAtPause = f.currentGroup();
        f.onNoteOn(n.pitch, 90, n.t);
    }
    expect(sawWaiting && waitingBeforeResume, "11 pause: WAITING while the performer stops");
    expect(groupAtPause == 20, "11 pause: position held at the last played group (" + std::to_string(groupAtPause) + ")");
    expect(positionDuringPause <= static_cast<double>(ref->group(21).tick) + 1e-6,
           "11 pause: estimated position never passes the next unplayed group");
    expect(f.currentGroup() == ref->groupCount() - 1, "12 resume: follower reaches the end");
    expect(f.state() == TrackingState::Tracking, std::string("12 resume: TRACKING again, got ") + toString(f.state()));
    expect(near(f.rate(), 1.0, 0.05), "12 resume: pause not read as a slow tempo (" + num(f.rate()) + ")");
}

void lossAndReacquisition()
{
    auto ref = makeRandomRef(128);
    const auto clean = perform(*ref, {});
    // Play groups 0..30, then 12 notes of garbage, then continue from group 80 onwards.
    std::vector<LiveNote> notes;
    double t = 0.0;
    for (const auto& n : clean) if (n.trueGroup <= 30) { notes.push_back(n); t = n.t; }
    Lcg rng;
    for (int i = 0; i < 12; ++i) {
        t += 0.2;
        notes.push_back({ 20 + static_cast<int>(rng.next() % 6), t, -1 });   // pitches not in the piece
    }
    const double shift = t + 0.5 - ref->group(80).nominalSec - clean.front().t;
    std::vector<LiveNote> tail;
    for (const auto& n : clean) if (n.trueGroup >= 80) { auto m = n; m.t += shift; tail.push_back(m); }
    notes.insert(notes.end(), tail.begin(), tail.end());

    auto f = makeFollower(ref);
    bool sawLost = false, sawReacq = false;
    int groupAtTrackingAgain = -1;
    std::uint32_t epochBefore = 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const auto r = f.onNoteOn(notes[i].pitch, 90, notes[i].t);
        if (f.state() == TrackingState::Lost) { sawLost = true; epochBefore = f.snapshot().epoch; }
        if (r.kind == MatchKind::Reacquired) sawReacq = true;
        if (sawReacq && groupAtTrackingAgain < 0 && f.state() == TrackingState::Tracking)
            groupAtTrackingAgain = f.currentGroup();
    }
    expect(sawLost, "13 confidence loss: garbage notes make the follower LOST");
    expect(sawReacq, "14 re-acquisition: global search finds the new place");
    expect(f.snapshot().epoch != epochBefore, "14 re-acquisition: epoch bumped (transport may re-anchor)");
    expect(groupAtTrackingAgain >= 80 && groupAtTrackingAgain < 92,
           "14 re-acquisition: back to TRACKING near group 80 (" + std::to_string(groupAtTrackingAgain) + ")");
    expect(f.currentGroup() == ref->groupCount() - 1, "14 re-acquisition: follows to the end");
    expect(f.state() == TrackingState::Tracking, std::string("14 re-acquisition: final TRACKING, got ") + toString(f.state()));
}

void restartFromTop()
{
    // Performer plays 40 groups, then starts over from the beginning.
    auto ref = makeRandomRef(96);
    const auto clean = perform(*ref, {});
    std::vector<LiveNote> notes;
    double last = 0.0;
    for (const auto& n : clean) if (n.trueGroup < 40) { notes.push_back(n); last = n.t; }
    for (const auto& n : clean) if (n.trueGroup < 30) { auto m = n; m.t += last + 1.0 - clean.front().t; notes.push_back(m); }
    auto f = makeFollower(ref);
    run(f, notes);
    expect(f.currentGroup() == 29, "restart from top: follower relocates to the beginning ("
                                       + std::to_string(f.currentGroup()) + ")");
}

void repeatedSectionTieBreak()
{
    // The periodic piece repeats every 32 groups (like identical choruses). After a
    // loss inside repeat #1, re-acquisition must pick the nearest repetition at or
    // after the last known position — never jump back to an earlier one.
    auto ref = makeRef(128);
    const auto clean = perform(*ref, {});
    std::vector<LiveNote> notes;
    double t = 0.0;
    for (const auto& n : clean) if (n.trueGroup <= 40) { notes.push_back(n); t = n.t; }
    for (int i = 0; i < 8; ++i) { t += 0.2; notes.push_back({ 21, t, -1 }); }
    // The performer continues at the music of group 50 (identical to 18, 82, 114).
    const double shift = t + 0.5 - ref->group(50).nominalSec - clean.front().t;
    for (const auto& n : clean) if (n.trueGroup >= 50 && n.trueGroup < 60) { auto m = n; m.t += shift; notes.push_back(m); }
    auto f = makeFollower(ref);
    run(f, notes);
    expect(f.currentGroup() >= 55 && f.currentGroup() < 64,
           "identical repeats: nearest following repetition chosen (" + std::to_string(f.currentGroup()) + ")");
}

void tempoSmoothing()
{
    // Heavy timing jitter (+-40 ms) at a constant tempo: the estimate must stay calm.
    auto ref = makeRef(96, 480, 120.0, false);
    auto f = makeFollower(ref);
    PerformanceSpec spec;
    spec.jitterSec = 0.04;
    spec.seed = 99;
    const auto s = run(f, perform(*ref, spec));
    double lo = 10, hi = 0;
    for (std::size_t i = 16; i < s.rates.size(); ++i) { lo = std::min(lo, s.rates[i]); hi = std::max(hi, s.rates[i]); }
    expect(hi - lo < 0.12, "15 tempo smoothing: rate band " + num(lo) + ".." + num(hi));
    expect(accuracy(s) >= 0.97, "15 tempo smoothing: accuracy " + num(accuracy(s)));

    // Estimator unit checks: limits + slew.
    TempoEstimatorConfig cfg;
    cfg.minBpm = 60; cfg.maxBpm = 180; cfg.maxChangeBpmPerSec = 10;
    TempoEstimator est(cfg);
    for (int i = 0; i < 6; ++i) est.addAnchor(i * 0.5, i * 0.5, 120);   // rate 1.0
    expect(near(est.rate(), 1.0, 1e-6), "estimator: exact 1.0");
    for (int i = 6; i < 8; ++i) est.addAnchor(i * 0.5, 3.0 + (i - 6) * 0.42, 120);   // sudden ~1.2x
    expect(est.rate() <= 1.0 + 10.0 / 120.0 * 1.0 + 1e-6, "estimator: slew-limited change (" + num(est.rate()) + ")");
    TempoEstimator fast(cfg);
    for (int i = 0; i < 6; ++i) fast.addAnchor(i * 0.5, i * 0.1, 120);   // 5x = 600 BPM
    expect(fast.bpm(120) <= 180.0 + 1e-6, "estimator: clamped to maxBpm (" + num(fast.bpm(120)) + ")");
    TempoEstimator slowStart(cfg);
    for (int i = 0; i < 4; ++i) slowStart.addAnchor(i * 0.5, i * 1.0, 120);   // 60 BPM from the first notes
    expect(near(slowStart.bpm(120), 60.0, 1.0), "estimator: slow starter acquired quickly (" + num(slowStart.bpm(120)) + ")");
    TempoEstimator paused(cfg);
    for (int i = 0; i < 6; ++i) paused.addAnchor(i * 0.5, i * 0.5, 120);
    paused.addAnchor(3.0, 2.5 + 8.0, 120);   // 8 s pause
    paused.addAnchor(3.5, 11.0, 120);
    expect(near(paused.rate(), 1.0, 0.02), "estimator: a pause starts a new segment (" + num(paused.rate()) + ")");
}

void degenerateInputs()
{
    // 17. Empty reference.
    {
        ScoreFollower f;
        f.setReference(std::make_shared<const ReferencePerformance>());
        const auto r = f.onNoteOn(60, 90, 1.0);
        f.update(2.0);
        expect(r.kind == MatchKind::Extra && f.state() == TrackingState::Idle, "17 empty reference: inert, no crash");
        expect(f.scorePositionTicks(5.0) == 0.0, "17 empty reference: position 0");
    }
    {
        ScoreFollower f;   // no reference at all
        f.onNoteOn(60, 90, 1.0);
        expect(f.state() == TrackingState::Idle, "17 no reference: inert");
    }
    // 18. Invalid input.
    {
        auto ref = makeRef(16);
        auto f = makeFollower(ref);
        f.onNoteOn(-5, 90, 1.0);
        f.onNoteOn(200, 90, 1.1);
        expect(f.state() == TrackingState::Idle && f.currentGroup() == -1, "18 invalid pitches ignored");
        // Non-monotonic timestamps must not break anything.
        const auto notes = perform(*ref, {});
        for (auto it = notes.rbegin(); it != notes.rend(); ++it) f.onNoteOn(it->pitch, 90, it->t);
        f.update(-100.0);
        expect(f.rate() > 0.0 && std::isfinite(f.rate()), "18 reversed timestamps: finite rate");
        // Notes before the performer starts that are not in the first group keep IDLE.
        auto g = makeFollower(ref);
        g.onNoteOn(30, 90, 0.5);
        g.onNoteOn(31, 90, 0.6);
        expect(g.state() == TrackingState::Idle, "18 stray notes before the start keep IDLE");
    }
}

void tempoMapChanges()
{
    // Reference written as 100 BPM for 16 beats, then 140 BPM. A performer who follows
    // the written change should have rate ~1 throughout.
    auto steps = syntheticPiece(48);
    auto file = fileWith({ trackFromSteps(steps) }, 480, 100.0);
    file.tempos.push_back({ 16 * 480, static_cast<std::uint32_t>(60'000'000.0 / 140.0) });
    auto ref = std::make_shared<const ReferencePerformance>(ReferencePerformance::build(file, {}));
    auto f = makeFollower(ref);
    const auto s = run(f, perform(*ref, {}));
    expect(accuracy(s) == 1.0, "19 tempo map: accuracy " + num(accuracy(s)));
    double worst = 0;
    for (double r : s.rates) worst = std::max(worst, std::abs(r - 1.0));
    expect(worst < 0.03, "19 tempo map: written change is not a performer change (worst " + num(worst) + ")");
    expect(near(f.estimatedBpm(), 140.0, 3.0), "19 tempo map: BPM shows the written 140 (" + num(f.estimatedBpm()) + ")");
}

void differentPpq()
{
    const auto a = std::make_shared<const ReferencePerformance>(referenceFromSteps(syntheticPiece(32, 96), 96));
    const auto b = std::make_shared<const ReferencePerformance>(referenceFromSteps(syntheticPiece(32, 960), 960));
    auto fa = makeFollower(a);
    auto fb = makeFollower(b);
    PerformanceSpec spec;
    spec.rateAt = [](double) { return 0.9; };
    spec.jitterSec = 0.01;
    const auto na = perform(*a, spec);
    const auto nb = perform(*b, spec);
    const auto sa = run(fa, na);
    const auto sb = run(fb, nb);
    expect(sa.correct == sb.correct && near(sa.finalRate, sb.finalRate, 1e-9), "20 PPQ 96 vs 960: identical behaviour");
    expect(near(fa.scorePositionTicks(na.back().t + 0.1) * 10.0, fb.scorePositionTicks(nb.back().t + 0.1), 1e-3),
           "20 PPQ: positions scale with PPQ");
}

void longSequence()
{
    // ~3000 groups with jitter, drift and sporadic mistakes.
    auto ref = makeRef(3000);
    PerformanceSpec spec;
    spec.rateAt = [](double n) { return 1.0 + 0.15 * std::sin(n / 60.0); };
    spec.jitterSec = 0.015;
    spec.chordSpreadSec = 0.03;
    auto notes = perform(*ref, spec);
    Lcg rng;
    rng.state = 4242;
    std::vector<LiveNote> messy;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const auto r = rng.next() % 200;
        if (r == 0) continue;                                  // missing (0.5%)
        if (r == 1) { auto w = notes[i]; w.pitch += 1; w.trueGroup = -1; messy.push_back(w); continue; }   // wrong
        messy.push_back(notes[i]);
        if (r == 2) { auto x = notes[i]; x.pitch = 110; x.t += 0.05; x.trueGroup = -1; messy.push_back(x); }  // extra
    }
    auto f = makeFollower(ref);
    const auto s = run(f, messy, 20.0);
    expect(accuracy(s) >= 0.97, "21 long sequence: accuracy " + num(accuracy(s)));
    expect(s.finalGroup >= ref->groupCount() - 2, "21 long sequence: reaches the end (" + std::to_string(s.finalGroup) + ")");
}

void denseChords()
{
    // 6-8 note two-hand chords (adjacent chords share most pitches) on 8th notes.
    std::vector<Step> steps;
    static const int shapes[4][8] = {
        { 36, 43, 48, 52, 55, 60, 64, 67 },
        { 41, 48, 53, 57, 60, 65, 69, -1 },
        { 43, 50, 55, 59, 62, 67, -1, -1 },
        { 36, 43, 48, 52, 55, 60, 64, 72 },
    };
    for (int i = 0; i < 48; ++i) {
        Step s;
        s.tick = i * 240;
        for (int p : shapes[i % 4]) if (p >= 0) s.pitches.push_back(p);
        steps.push_back(s);
    }
    auto ref = std::make_shared<const ReferencePerformance>(referenceFromSteps(steps));
    auto f = makeFollower(ref);
    PerformanceSpec spec;
    spec.chordSpreadSec = 0.05;
    spec.jitterSec = 0.01;
    spec.rateAt = [](double) { return 0.85; };
    auto notes = perform(*ref, spec);
    // Drop one random tone from every third chord (sloppy dense playing).
    std::vector<LiveNote> sloppy;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!(notes[i].trueGroup % 3 == 0 && i % 7 == 3)) sloppy.push_back(notes[i]);
    const auto s = run(f, sloppy);
    expect(accuracy(s) >= 0.95, "22 dense chords: accuracy " + num(accuracy(s)));
    expect(s.finalGroup == ref->groupCount() - 1, "22 dense chords: reaches the end");
    expect(near(s.finalRate, 0.85, 0.06), "22 dense chords: rate " + num(s.finalRate));
}

void startAnywhereAndSeek()
{
    auto ref = makeRandomRef(64);
    const auto clean = perform(*ref, {});
    ScoreFollowerConfig cfg;
    cfg.startAnywhere = true;
    auto f = makeFollower(ref, cfg);
    std::vector<LiveNote> notes;
    for (const auto& n : clean) if (n.trueGroup >= 33) notes.push_back(n);
    run(f, notes);
    expect(f.currentGroup() == ref->groupCount() - 1, "start anywhere: follows from the middle");

    auto g = makeFollower(ref);
    g.seekToGroup(33);
    const auto s = run(g, notes);
    expect(accuracy(s) >= 0.98, "seek: accuracy after a user seek " + num(accuracy(s)));
}

void snapshotContract()
{
    auto ref = makeRef(16);
    auto f = makeFollower(ref);
    auto s0 = f.snapshot();
    expect(s0.state == TrackingState::Idle && s0.groupIndex == -1, "snapshot: idle before start");
    expect(near(s0.holdNominalSec, ref->group(0).nominalSec, 1e-9), "snapshot: idle holds at the first group");
    const auto notes = perform(*ref, {});
    f.onNoteOn(notes[0].pitch, 90, notes[0].t);
    auto s1 = f.snapshot();
    expect(s1.groupIndex == 0 && near(s1.anchorLiveSec, notes[0].t, 1e-9), "snapshot: anchored at the first note");
    expect(near(s1.holdNominalSec, ref->group(1).nominalSec, 1e-9), "snapshot: hold = next unplayed group");
}
}

int main()
{
    exactPerformance();
    constantTempo(0.75, "2 slower");
    constantTempo(1.3, "3 faster");
    gradualTempo(1.0, 1.4, "4 acceleration");
    gradualTempo(1.0, 0.7, "5 deceleration");
    mistakes();
    brokenChord();
    repeatedNotes();
    pauseAndResume();
    lossAndReacquisition();
    restartFromTop();
    repeatedSectionTieBreak();
    tempoSmoothing();
    degenerateInputs();
    tempoMapChanges();
    differentPpq();
    longSequence();
    denseChords();
    startAnywhereAndSeek();
    snapshotContract();

    if (failures == 0) std::cout << "adaptive score follower tests: all passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
