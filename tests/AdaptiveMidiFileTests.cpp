// Tests for the Adaptive Song Mode SMF reader, TempoMap and ReferencePerformance.

#include "AdaptiveTestSupport.h"

#include <cstdlib>

using namespace adaptive_test;

namespace
{
void roundTripMultitrack()
{
    MidiTrackData piano = trackFromSteps({ { 0, { 60, 64, 67 } }, { 480, { 62 } }, { 960, { 64 } } }, 0, "Piano");
    MidiTrackData drums;
    drums.name = "Drums";
    addNote(drums, 0, 60, 36, 100, 9);
    addNote(drums, 480, 60, 38, 100, 9);
    MidiChannelEvent pc;
    pc.tick = 0; pc.kind = MidiEventKind::ProgramChange; pc.channel = 1; pc.data1 = 33;
    MidiTrackData bass;
    bass.name = "Bass";
    bass.events.push_back(pc);
    addNote(bass, 0, 400, 36, 100, 1);
    MidiChannelEvent bend;
    bend.tick = 100; bend.kind = MidiEventKind::PitchBend; bend.channel = 1; bend.bend = 12000;
    bass.events.push_back(bend);
    sortTrackEvents(drums);
    sortTrackEvents(bass);

    auto file = fileWith({ piano, drums, bass }, 960, 100.0);
    file.tempos.push_back({ 1920, 400000 });   // 150 BPM from beat 3
    const auto bytes = writeMidiFile(file);
    const auto parsed = parseMidiFile(bytes);

    expect(parsed.ok, "round trip parses: " + parsed.error);
    expect(parsed.data.ppq == 960, "PPQ preserved");
    expect(parsed.data.tracks.size() == 3, "three tracks");
    expect(parsed.data.tracks[0].name == "Piano" && parsed.data.tracks[1].name == "Drums",
           "track names preserved");
    expect(parsed.data.tracks[0].notes.size() == 5, "piano notes paired");
    expect(parsed.data.tracks[1].usesChannel(9), "drums on channel 10 (index 9)");
    expect(parsed.data.tracks[2].firstProgram(1).value_or(-1) == 33, "program change preserved");
    expect(parsed.data.tempos.size() == 2, "tempo map preserved");
    expect(near(parsed.data.tempos[0].bpm(), 100.0, 0.01) && near(parsed.data.tempos[1].bpm(), 150.0, 0.01),
           "tempo values preserved");
    bool sawBend = false;
    for (const auto& e : parsed.data.tracks[2].events)
        if (e.kind == MidiEventKind::PitchBend) sawBend = e.bend == 12000;
    expect(sawBend, "pitch bend preserved (14-bit)");
    expect(parsed.data.warnings.empty(), "clean file has no warnings");
}

void runningStatusAndVelocityZeroNoteOff()
{
    // Hand-written format-0 file: running status + note-on velocity 0 as note-off.
    const std::vector<std::uint8_t> bytes = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xE0,      // format 0, 1 track, PPQ 480
        'M','T','r','k', 0,0,0,19,
        0x00, 0x90, 60, 100,     // note on C4
        0x00, 64, 100,           // running status: note on E4
        0x83, 0x60, 60, 0,       // +480, running: C4 vel 0 (off)
        0x00, 64, 0,             // E4 off
        0x00, 0xFF, 0x2F, 0x00,
    };
    const auto parsed = parseMidiFile(bytes);
    expect(parsed.ok, "running-status file parses: " + parsed.error);
    expect(parsed.data.format == 0, "format 0 detected");
    expect(parsed.data.tracks.size() == 1 && parsed.data.tracks[0].notes.size() == 2, "two notes");
    if (parsed.ok && parsed.data.tracks[0].notes.size() == 2) {
        const auto& n = parsed.data.tracks[0].notes[0];
        expect(n.pitch == 60 && n.onTick == 0 && n.offTick == 480, "vel-0 note on closes the note");
    }
}

void invalidAndEmptyInput()
{
    expect(!parseMidiFile({}).ok, "empty buffer rejected");
    expect(!parseMidiFile({ 'R','I','F','F', 1, 2, 3 }).ok, "garbage rejected");
    const std::vector<std::uint8_t> smpte = { 'M','T','h','d', 0,0,0,6, 0,1, 0,1, 0xE7,0x28 };
    const auto r = parseMidiFile(smpte);
    expect(!r.ok && r.error.find("SMPTE") != std::string::npos, "SMPTE division reported");
    const std::vector<std::uint8_t> zeroPpq = { 'M','T','h','d', 0,0,0,6, 0,1, 0,1, 0,0 };
    expect(!parseMidiFile(zeroPpq).ok, "zero PPQ rejected");
    const std::vector<std::uint8_t> noTracks = { 'M','T','h','d', 0,0,0,6, 0,1, 0,1, 1,0xE0 };
    expect(!parseMidiFile(noTracks).ok, "header without tracks rejected");

    // Truncated track body: parses what is there, warns, does not crash.
    std::vector<std::uint8_t> truncated = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xE0,
        'M','T','r','k', 0,0,0,40,
        0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60,
    };
    const auto t = parseMidiFile(truncated);
    expect(t.ok, "truncated file still yields a result");
    expect(!t.data.warnings.empty(), "truncation produces a warning");
    expect(t.ok && t.data.tracks[0].notes.size() == 1 && t.data.tracks[0].notes[0].offTick > 0,
           "unterminated note closed at track end");

    expect(!parseMidiFileFromPath("/definitely/not/here.mid").ok, "missing path reported");

    // A structurally valid file with no notes is fine for the reader...
    MidiFileData empty;
    empty.ppq = 480;
    MidiTrackData t0;
    empty.tracks.push_back(t0);
    const auto e = parseMidiFile(writeMidiFile(empty));
    expect(e.ok && e.data.noteCount() == 0, "empty MIDI parses with zero notes");
    // ...but gives an empty reference with an explanatory error.
    std::string err;
    const auto ref = ReferencePerformance::build(e.data, {}, &err);
    expect(ref.empty() && !err.empty(), "empty MIDI -> empty reference + error");
    std::string err2;
    const auto bad = ReferencePerformance::build(e.data, { { 5 } }, &err2);
    expect(bad.empty() && err2.find("does not exist") != std::string::npos, "invalid track index reported");
}

void tempoMapConversions()
{
    // 120 BPM for 2 beats, then 60 BPM. PPQ 480.
    TempoMap map(480, { { 0, 500000 }, { 960, 1000000 } }, {});
    expect(near(map.ticksToSeconds(480), 0.5, 1e-9), "beat 1 at 0.5 s");
    expect(near(map.ticksToSeconds(960), 1.0, 1e-9), "tempo change at 1.0 s");
    expect(near(map.ticksToSeconds(1440), 2.0, 1e-9), "60 BPM beat takes 1 s");
    expect(near(map.secondsToTicks(2.0), 1440, 1e-6), "inverse conversion");
    expect(near(map.bpmAt(100), 120.0, 1e-9) && near(map.bpmAt(1000), 60.0, 1e-9), "BPM lookup");
    expect(!map.isConstant(), "map with a change is not constant");

    TempoMap def(96);
    expect(near(def.bpmAt(0), 120.0, 1e-9) && def.isConstant(), "default is constant 120 BPM");
    expect(near(def.ticksToSeconds(96), 0.5, 1e-9), "PPQ 96 default beat 0.5 s");

    // Same musical tempo curve at a different PPQ is equivalent.
    TempoMap other(960, { { 0, 500000 }, { 1920, 1000000 } }, {});
    double diff = 0;
    expect(map.equivalentTo(other, 4800, 0.001, &diff), "same curve at different PPQ is equivalent");
    TempoMap faster(480, { { 0, 400000 } }, {});
    expect(!map.equivalentTo(faster, 4800, 0.05, &diff) && diff > 0.5, "different curves detected");
}

void barBeatAndTimeSignatures()
{
    TempoMap map(480, {}, { { 0, 4, 4 }, { 1920 * 2, 3, 4 } });
    auto bb = map.barBeatAt(1920 + 480 * 2);
    expect(bb.bar == 1 && bb.beat == 2, "4/4 bar 2 beat 3");
    bb = map.barBeatAt(1920 * 2 + 1440 + 480);
    expect(bb.bar == 3 && bb.beat == 1, "after change to 3/4 bars are 1440 ticks");
    expect(map.timeSignatureAt(5000).numerator == 3, "time signature lookup");
}

void chordGrouping()
{
    // A triad with onsets spread over 20 ms, then a run of 16ths at 120 BPM (125 ms apart).
    MidiTrackData t;
    addNote(t, 0, 400, 60);
    addNote(t, 10, 400, 64);    // 10 ticks @480/120bpm = ~10.4 ms
    addNote(t, 19, 400, 67);    // ~19.8 ms
    for (int i = 0; i < 4; ++i) addNote(t, 480 + i * 120, 100, 72 + i);
    addNote(t, 960, 100, 72);
    addNote(t, 960, 100, 72);   // duplicated pitch in one group (layered track)
    sortTrackEvents(t);
    t.endTick = 1440;
    auto file = fileWith({ t });
    const auto ref = ReferencePerformance::build(file, {});
    expect(ref.groupCount() == 6, "spread triad = 1 group, 4 run notes = 4 groups, dup = 1 group (got "
                                       + std::to_string(ref.groupCount()) + ")");
    if (ref.groupCount() == 6) {
        expect(ref.group(0).size() == 3 && ref.group(0).tick == 0 && ref.group(0).lastOnsetTick == 19,
               "triad group keeps earliest/latest onset");
        expect(ref.group(5).size() == 1 && ref.group(5).noteCount == 2, "duplicate pitch collapses");
        expect(near(ref.group(1).nominalSec, 0.5, 1e-9), "group nominal time");
    }

    // Tick-based window override: 0 ticks means every distinct onset is its own group.
    ReferenceBuildOptions strict;
    strict.chordGroupingWindowTicks = 0;
    expect(ReferencePerformance::build(file, strict).groupCount() == 8, "0-tick window splits the spread chord");

    // A wide window must not chain the 16th-note run into one group (measured from first onset).
    ReferenceBuildOptions wide;
    wide.chordGroupingWindowMs = 130.0;
    const auto w = ReferencePerformance::build(file, wide);
    expect(w.groupCount() == 4, "wide window groups pairs but does not chain the whole run (got "
                                    + std::to_string(w.groupCount()) + ")");

    expect(ref.groupAtOrAfter(481) == 2 && ref.groupAtOrBefore(481) == 1, "group lookup by tick");
    expect(ref.groupAtOrAfter(100000) == ref.groupCount() && ref.groupAtOrBefore(-1) == -1, "lookup bounds");
}

void channelAndDrumFiltering()
{
    MidiTrackData t;
    addNote(t, 0, 100, 60, 90, 0);    // right hand ch1
    addNote(t, 0, 100, 48, 90, 1);    // left hand ch2
    addNote(t, 0, 100, 36, 90, 9);    // drum
    sortTrackEvents(t);
    auto file = fileWith({ t });
    const auto all = ReferencePerformance::build(file, {});
    expect(all.notes().size() == 2, "drum channel excluded by default");
    ReferenceBuildOptions rh;
    rh.channelFilter = 0;
    expect(ReferencePerformance::build(file, rh).notes().size() == 1, "channel filter");
}

void differentPpqSameMusic()
{
    const auto a = referenceFromSteps({ { 0, { 60 } }, { 480, { 62 } }, { 960, { 64 } } }, 480);
    const auto b = referenceFromSteps({ { 0, { 60 } }, { 96, { 62 } }, { 192, { 64 } } }, 96);
    expect(a.groupCount() == 3 && b.groupCount() == 3, "same piece at PPQ 480 and 96");
    if (a.groupCount() == 3 && b.groupCount() == 3)
        expect(near(a.group(2).nominalSec, b.group(2).nominalSec, 1e-9), "nominal time independent of PPQ");
}
}

int main()
{
    roundTripMultitrack();
    runningStatusAndVelocityZeroNoteOff();
    invalidAndEmptyInput();
    tempoMapConversions();
    barBeatAndTimeSignatures();
    chordGrouping();
    channelAndDrumFiltering();
    differentPpqSameMusic();

    if (failures == 0) std::cout << "adaptive midi file tests: all passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
