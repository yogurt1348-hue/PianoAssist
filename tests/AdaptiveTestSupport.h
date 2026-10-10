// Shared helpers for the Adaptive Song Mode tests: the project's expect()
// convention plus synthetic (copyright-free) fixtures built in code.

#pragma once

#include "Adaptive/MidiFile.h"
#include "Adaptive/ReferencePerformance.h"
#include "Adaptive/TempoMap.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace adaptive_test
{
inline int failures = 0;

inline void expect(bool cond, const std::string& msg)
{
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

inline bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

inline std::string num(double v)
{
    return std::to_string(v);
}

using namespace cadenza::adaptive;

// A melody/chord "score" description: each step is a set of pitches at a tick.
struct Step
{
    std::int64_t tick = 0;
    std::vector<int> pitches;
    std::int64_t length = 0;   // 0 = until next step
};

inline MidiTrackData trackFromSteps(const std::vector<Step>& steps, int channel = 0, const std::string& name = "Piano")
{
    MidiTrackData t;
    t.name = name;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        std::int64_t len = steps[i].length;
        if (len <= 0) len = (i + 1 < steps.size()) ? steps[i + 1].tick - steps[i].tick : 480;
        for (int p : steps[i].pitches) addNote(t, steps[i].tick, std::max<std::int64_t>(1, len - 10), p, 90, channel);
    }
    t.endTick = steps.empty() ? 0 : steps.back().tick + 480;
    finalizeTrack(t);
    return t;
}

// Deterministic pseudo-random generator (LCG) so tests never depend on <random> impls.
struct Lcg
{
    std::uint32_t state = 12345u;
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
    double unit() { return (next() >> 8) * (1.0 / 16777216.0); }       // [0,1)
    double symmetric() { return unit() * 2.0 - 1.0; }                  // [-1,1)
};

// A copyright-free synthetic piece: `count` steps of quarter notes at PPQ `ppq`,
// cycling a fixed pattern of single notes and triads (with deliberate repeated notes).
inline std::vector<Step> syntheticPiece(int count, int ppq = 480, bool withChords = true)
{
    static const int melody[] = { 60, 62, 64, 64, 65, 67, 69, 67, 65, 64, 62, 62, 60, 59, 57, 59 };
    std::vector<Step> steps;
    for (int i = 0; i < count; ++i) {
        Step s;
        s.tick = static_cast<std::int64_t>(i) * ppq;
        const int m = melody[i % 16] + 12 * ((i / 16) % 2);
        if (withChords && i % 4 == 0) s.pitches = { m - 12, m - 8, m - 5, m };   // chord on the downbeat
        else s.pitches = { m };
        steps.push_back(s);
    }
    return steps;
}

inline MidiFileData fileWith(std::vector<MidiTrackData> tracks, int ppq = 480, double bpm = 120.0)
{
    MidiFileData f;
    f.format = 1;
    f.ppq = ppq;
    f.tempos.push_back({ 0, static_cast<std::uint32_t>(60'000'000.0 / bpm + 0.5) });
    f.timeSignatures.push_back({ 0, 4, 4 });
    f.tracks = std::move(tracks);
    for (auto& t : f.tracks) finalizeTrack(t);
    return f;
}

inline ReferencePerformance referenceFromSteps(const std::vector<Step>& steps, int ppq = 480, double bpm = 120.0)
{
    auto file = fileWith({ trackFromSteps(steps) }, ppq, bpm);
    return ReferencePerformance::build(file, {});
}
}
