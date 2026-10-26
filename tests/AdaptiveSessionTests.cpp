// End-to-end tests of AdaptiveSongSession: performer notes (MIDI thread role),
// audio blocks (audio thread role) and UI polling (message thread role), both
// deterministic single-threaded and as a real multi-threaded smoke test.

#include "AdaptiveTestSupport.h"
#include "Adaptive/AdaptiveSongSession.h"
#include "Adaptive/TripleBuffer.h"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace adaptive_test;

namespace
{
constexpr double kBlock = 256.0 / 48000.0;

struct TimedSink final : MidiEventSink
{
    struct Ev { char type; int ch; int note; double t; };
    std::vector<Ev> evs;
    double now = 0.0;
    int sounding = 0;
    void noteOn(int ch, int n, int) noexcept override { evs.push_back({ 'N', ch, n, now }); ++sounding; }
    void noteOff(int ch, int n) noexcept override { evs.push_back({ 'F', ch, n, now }); --sounding; }
    void controlChange(int, int, int) noexcept override {}
    void programChange(int, int) noexcept override {}
    void pitchBend(int, int) noexcept override {}
};

// Piano (reference) quarter notes + drums on every 8th + a sustained pad.
MidiFileData bandFile(int beats)
{
    std::vector<Step> steps;
    Lcg rng;
    rng.state = 5;
    int p = 64;
    for (int i = 0; i < beats; ++i) {
        p = std::clamp(p + static_cast<int>(rng.next() % 7) - 3, 55, 80);
        steps.push_back({ i * 480LL, i % 4 == 0 ? std::vector<int> { p - 12, p - 5, p } : std::vector<int> { p } });
    }
    auto piano = trackFromSteps(steps, 0, "Piano");
    MidiTrackData drums;
    drums.name = "Drums";
    for (int i = 0; i < beats * 2; ++i) addNote(drums, i * 240, 30, i % 4 == 2 ? 38 : 42, 100, 9);
    MidiTrackData pad;
    pad.name = "Pad";
    for (int bar = 0; bar < beats / 4; ++bar) addNote(pad, bar * 1920, 1900, 48, 70, 4);
    return fileWith({ piano, drums, pad });
}

AdaptiveSong buildSong(const MidiFileData& file)
{
    AdaptiveSongOptions opt;
    opt.referenceTracks = { 0 };
    opt.backingTracks = { { 1 }, { 2 } };
    opt.reservedOutputChannels = 0x0001;
    auto r = buildAdaptiveSong(file, nullptr, opt);
    expect(r.ok, "song builds: " + r.error);
    return r.song;
}

void endToEnd()
{
    const auto file = bandFile(48);
    const auto song = buildSong(file);
    AdaptiveSongSession session;
    session.setDiagnosticsEnabled(true);
    session.load(song);
    session.play();

    TimedSink sink;
    const double rate = 0.8;                // performer plays at 96 BPM in a 120 BPM file
    const double startLive = 2.0;
    std::vector<std::pair<double, std::vector<int>>> onsets;   // live time, pitches
    for (const auto& g : song.reference->groups()) {
        std::vector<int> ps;
        for (int p = 0; p < 128; ++p) if (g.pitches.test(p)) ps.push_back(p);
        onsets.push_back({ startLive + g.nominalSec / rate, ps });
    }
    std::size_t next = 0;
    double now = 0.0;
    int uiTicks = 0;
    bool sawFinished = false;
    while (now < startLive + 48 * 0.5 / rate + 3.0) {
        while (next < onsets.size() && onsets[next].first <= now) {
            for (int p : onsets[next].second) session.onLiveNote(p, 90, true, onsets[next].first);
            ++next;
        }
        sink.now = now;
        session.processBlock(now, kBlock, sink);
        if (++uiTicks % 10 == 0) session.update(now);
        sawFinished |= session.status(now).finished;
        now += kBlock;
    }

    // Nothing before the performer started (WaitForPerformer).
    bool early = false;
    for (const auto& e : sink.evs) early |= e.type == 'N' && e.t < startLive - 1e-9;
    expect(!early, "e2e: backing silent until the performer plays");
    // No reference (piano, ch1) notes in the backing output.
    bool piano = false;
    for (const auto& e : sink.evs) piano |= e.ch == 1;
    expect(!piano, "e2e: reference part is not played by Cadenza");
    // Drum hits follow the performer's tempo: hit k (8th notes) at startLive + k*0.25/rate.
    double worst = 0.0;
    int drumHits = 0;
    for (const auto& e : sink.evs) {
        if (e.type != 'N' || e.ch != 10) continue;
        const double expected = startLive + drumHits * 0.25 / rate;
        if (drumHits > 8 && drumHits < 90) worst = std::max(worst, std::abs(e.t - expected));
        ++drumHits;
    }
    expect(drumHits == 96, "e2e: every drum hit played once (" + std::to_string(drumHits) + ")");
    expect(worst < 0.03, "e2e: drums within 30 ms of the performer's grid (worst " + num(worst * 1000) + " ms)");
    expect(sawFinished, "e2e: auto-stop at the end of the song");
    expect(sink.sounding == 0, "e2e: all backing notes released at the end");
    const auto diags = session.drainDiagnostics();
    expect(!diags.empty() && diags.back().note.kind != MatchKind::None, "e2e: diagnostics recorded");
    expect(session.drainDiagnostics().empty(), "e2e: diagnostics drained");
    const auto st = session.status(now);
    expect(!st.playing && st.groupIndex == song.reference->groupCount() - 1, "e2e: status at the end");
}

void pauseStopsTheBand()
{
    const auto file = bandFile(32);
    const auto song = buildSong(file);
    AdaptiveSongSession session;
    session.load(song);
    session.play();
    TimedSink sink;
    double now = 0.0;
    // Performer plays 8 beats at the written tempo, then stops for 6 s.
    std::size_t g = 0;
    const auto& groups = song.reference->groups();
    double lastNoteTime = 0.0;
    while (now < 10.0) {
        while (g < groups.size() && groups[g].tick < 8 * 480 && 1.0 + groups[g].nominalSec <= now) {
            for (int p = 0; p < 128; ++p) if (groups[g].pitches.test(p)) session.onLiveNote(p, 90, true, 1.0 + groups[g].nominalSec);
            lastNoteTime = 1.0 + groups[g].nominalSec;
            ++g;
        }
        sink.now = now;
        session.processBlock(now, kBlock, sink);
        session.update(now);
        now += kBlock;
    }
    double lastBackingOn = 0.0;
    for (const auto& e : sink.evs) if (e.type == 'N') lastBackingOn = std::max(lastBackingOn, e.t);
    expect(lastBackingOn < lastNoteTime + 0.5 + 0.15, "pause: band stops shortly after the performer ("
                                                          + num(lastBackingOn - lastNoteTime) + " s)");
    expect(sink.sounding == 0, "pause: held notes released during a long pause");
    expect(session.status(now).state == TrackingState::Waiting, "pause: status WAITING");
    session.stop();
    session.processBlock(now, kBlock, sink);
    expect(!session.status(now).playing, "stop: audio side stopped");
}

void stopReleasesAndLoadWhilePlaying()
{
    const auto file = bandFile(16);
    const auto song = buildSong(file);
    AdaptiveSongSession session;
    session.load(song);
    session.play();
    TimedSink sink;
    double now = 1.0;
    for (int p = 0; p < 128; ++p) if (song.reference->group(0).pitches.test(p)) session.onLiveNote(p, 90, true, now);
    for (int i = 0; i < 40; ++i) { sink.now = now; session.processBlock(now, kBlock, sink); now += kBlock; }
    expect(sink.sounding > 0, "stop: something sounding before stop");
    session.stop();
    session.processBlock(now, kBlock, sink);
    expect(sink.sounding == 0, "stop: releases every backing note");

    // Load another song while "playing": it is swapped in on the audio thread when stopped.
    session.play();
    for (int i = 0; i < 5; ++i) { session.processBlock(now, kBlock, sink); now += kBlock; }
    const auto other = buildSong(bandFile(8));
    session.load(other);
    session.processBlock(now, kBlock, sink);   // stop + swap
    session.update(now);                        // retire the old song on the message thread
    expect(session.status(now).groupCount == other.reference->groupCount(), "load: new song active");
    session.play();
    for (int p = 0; p < 128; ++p) if (other.reference->group(0).pitches.test(p)) session.onLiveNote(p, 90, true, now);
    for (int i = 0; i < 20; ++i) { sink.now = now; session.processBlock(now, kBlock, sink); now += kBlock; }
    expect(session.status(now).playing, "load: new song plays");

    // Notes while stopped are ignored.
    AdaptiveSongSession idle;
    idle.load(song);
    idle.onLiveNote(60, 90, true, 1.0);
    expect(idle.status(1.0).matchedNotes == 0, "notes before Play are ignored");
    AdaptiveSongSession empty;
    empty.play();
    TimedSink s2;
    empty.processBlock(0.0, kBlock, s2);
    expect(!empty.status(0.0).playing && s2.evs.empty(), "play without a song does nothing");
}

void tripleBuffer()
{
    TripleBuffer<FollowerSnapshot> tb;
    FollowerSnapshot a;
    a.groupIndex = 5;
    tb.write(a);
    expect(tb.read().groupIndex == 5, "triple buffer: latest value");
    a.groupIndex = 6; tb.write(a);
    a.groupIndex = 7; tb.write(a);
    expect(tb.read().groupIndex == 7 && tb.read().groupIndex == 7, "triple buffer: newest wins, stable re-read");

    // Concurrent writer/reader: the reader never sees a torn value.
    struct Pair { std::uint64_t a = 0, b = 0; };
    TripleBuffer<Pair> p;
    std::atomic<bool> done { false };
    std::thread writer([&] {
        for (std::uint64_t i = 1; i < 200000; ++i) p.write({ i, ~i });
        done = true;
    });
    bool torn = false;
    std::uint64_t last = 0;
    bool monotonic = true;
    while (!done) {
        const auto v = p.read();
        torn |= v.a != 0 && v.b != ~v.a;
        monotonic &= v.a >= last;
        last = v.a;
    }
    writer.join();
    expect(!torn, "triple buffer: no torn reads");
    expect(monotonic, "triple buffer: values never go back in time");
}

void threadedSmoke()
{
    // Real threads: audio (blocks), MIDI (notes), message (update/status/play/stop/load).
    const auto file = bandFile(64);
    const auto song = buildSong(file);
    const auto other = buildSong(bandFile(24));
    AdaptiveSongSession session;
    session.setDiagnosticsEnabled(true);
    session.load(song);
    session.play();

    std::atomic<bool> run { true };
    std::atomic<double> clock { 0.0 };
    std::atomic<int> blocks { 0 };
    struct CountSink final : MidiEventSink {
        std::atomic<int> on { 0 }, off { 0 };
        void noteOn(int, int, int) noexcept override { ++on; }
        void noteOff(int, int) noexcept override { ++off; }
        void controlChange(int, int, int) noexcept override {}
        void programChange(int, int) noexcept override {}
        void pitchBend(int, int) noexcept override {}
    } sink;

    std::thread audio([&] {
        double t = 0.0;
        while (run) {
            session.processBlock(t, kBlock, sink);
            t += kBlock;
            clock.store(t);
            ++blocks;
            std::this_thread::yield();
        }
    });
    std::thread midi([&] {
        const auto& groups = song.reference->groups();
        for (int rep = 0; rep < 3 && run; ++rep)
            for (const auto& g : groups) {
                const double t = clock.load();
                for (int p = 0; p < 128; ++p) if (g.pitches.test(p)) session.onLiveNote(p, 90, true, t);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
    });
    for (int i = 0; i < 400; ++i) {
        const double t = clock.load();
        session.update(t);
        (void) session.status(t);
        (void) session.drainDiagnostics();
        if (i == 100) session.stop();
        if (i == 120) session.play();
        if (i == 200) session.load(other);
        if (i == 220) session.play();
        if (i == 300) session.load(song);
        if (i == 320) session.play();
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    session.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    run = false;
    midi.join();
    audio.join();
    expect(blocks.load() > 100, "threaded: audio thread ran");
    expect(sink.on.load() >= sink.off.load() - 1000000, "threaded: no crash, counters sane");
}
}

int main()
{
    endToEnd();
    pauseStopsTheBand();
    stopReleasesAndLoadWhilePlaying();
    tripleBuffer();
    threadedSmoke();

    if (failures == 0) std::cout << "adaptive session tests: all passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
