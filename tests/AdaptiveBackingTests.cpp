// Tests for BackingSequence (RT renderer) and AdaptiveSong (track selection,
// two-file timeline checks, channel assignment).

#include "AdaptiveTestSupport.h"
#include "Adaptive/AdaptiveSong.h"
#include "Adaptive/BackingSequence.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace adaptive_test;

namespace
{
struct Recorder final : MidiEventSink
{
    struct Ev { char type; int ch; int a; int b; };
    std::vector<Ev> evs;
    void noteOn(int ch, int n, int v) noexcept override { evs.push_back({ 'N', ch, n, v }); }
    void noteOff(int ch, int n) noexcept override { evs.push_back({ 'F', ch, n, 0 }); }
    void controlChange(int ch, int c, int v) noexcept override { evs.push_back({ 'C', ch, c, v }); }
    void programChange(int ch, int p) noexcept override { evs.push_back({ 'P', ch, p, 0 }); }
    void pitchBend(int ch, int v) noexcept override { evs.push_back({ 'B', ch, v, 0 }); }
    int count(char t) const { int n = 0; for (const auto& e : evs) n += e.type == t; return n; }
};

MidiFileData band()
{
    // Track 0 piano (reference), 1 drums (ch10), 2 bass (ch2, program 33, CC7), 3 pad (ch1 - collides with live voice)
    auto piano = trackFromSteps(syntheticPiece(16), 0, "Grand Piano");
    MidiTrackData drums;
    drums.name = "Drums";
    for (int i = 0; i < 32; ++i) addNote(drums, i * 240, 60, i % 2 ? 42 : 36, 100, 9);
    MidiTrackData bass;
    bass.name = "Bass";
    MidiChannelEvent pc; pc.kind = MidiEventKind::ProgramChange; pc.channel = 1; pc.data1 = 33; pc.tick = 0;
    MidiChannelEvent vol; vol.kind = MidiEventKind::ControlChange; vol.channel = 1; vol.data1 = 7; vol.data2 = 90; vol.tick = 0;
    MidiChannelEvent vol2 = vol; vol2.tick = 3000; vol2.data2 = 60;
    bass.events = { pc, vol, vol2 };
    for (int i = 0; i < 8; ++i) addNote(bass, i * 960, 900, 36 + i, 100, 1);
    MidiTrackData pad;
    pad.name = "Strings";
    addNote(pad, 0, 7680, 60, 70, 0);
    return fileWith({ piano, drums, bass, pad });
}

void rendererBasics()
{
    const auto file = band();
    auto seq = BackingSequence::build(file, { { 1 }, { 2 } }, BackingSequence::identityChannels());
    expect(!seq.empty(), "backing built");
    expect((seq.outputChannelMask() & (1u << 9)) && (seq.outputChannelMask() & (1u << 1)), "drums ch10 + bass ch2");
    Recorder r;
    seq.seek(0.0, r);
    seq.render(0.0, 479.0, true, r);
    expect(r.count('N') == 2 + 1, "inclusive first range: 2 drum hits + 1 bass note at/after tick 0 (got "
                                      + std::to_string(r.count('N')) + ")");
    expect(r.count('P') == 1 && r.count('C') == 1, "program + volume sent before notes");
    // Contiguous blocks: every event exactly once.
    Recorder all;
    auto seq2 = BackingSequence::build(file, { { 1 }, { 2 } }, BackingSequence::identityChannels());
    seq2.seek(0.0, all);
    double from = 0.0;
    bool first = true;
    for (double to = 7.3; to < 9000.0; to += 7.3) { seq2.render(from, to, first, all); from = to; first = false; }
    expect(all.count('N') == 32 + 8, "blockwise render plays every note once (" + std::to_string(all.count('N')) + ")");
    expect(all.count('F') == 32 + 8, "every note released");
    expect(seq2.soundingNotes() == 0, "nothing left sounding at the end");
}

void seekReleasesAndChases()
{
    const auto file = band();
    auto seq = BackingSequence::build(file, { { 2 }, { 3 } }, BackingSequence::identityChannels());
    Recorder r;
    seq.seek(0.0, r);
    seq.render(0.0, 100.0, true, r);
    expect(seq.soundingNotes() == 2, "bass + pad sounding");
    Recorder s;
    seq.seek(4000.0, s);   // past the CC7=60 change
    expect(s.count('F') == 2, "seek releases sounding notes");
    bool vol60 = false, prog33 = false;
    for (const auto& e : s.evs) {
        if (e.type == 'C' && e.ch == 2 && e.a == 7) vol60 = e.b == 60;
        if (e.type == 'P' && e.ch == 2) prog33 = e.a == 33;
    }
    expect(vol60 && prog33, "seek chases program + latest controller");
    Recorder after;
    seq.render(4000.0, 4800.0, true, after);
    expect(after.count('N') == 1 && after.count('F') == 0, "after seek: next bass note, orphan offs suppressed");
    Recorder rel;
    seq.releaseAll(rel);
    expect(rel.count('F') == 1 && seq.soundingNotes() == 0, "releaseAll");
}

void jumpSkipsInsteadOfFlooding()
{
    const auto file = band();
    auto seq = BackingSequence::build(file, { { 1 } }, BackingSequence::identityChannels());
    Recorder r;
    seq.seek(0.0, r);
    seq.render(0.0, 10.0, true, r);
    Recorder big;
    seq.render(5000.0, 5100.0, false, big);   // range jumped ahead without a seek
    expect(big.count('N') <= 1, "events before a jumped range are not played late");
}

void songSingleFile()
{
    const auto file = band();
    expect(suggestReferenceTrack(file) == 0, "heuristic picks the piano track");
    const auto backingSuggest = suggestBackingTracks(file, { 0 });
    expect(backingSuggest.size() == 3, "other note tracks suggested as backing");
    const auto infos = describeTracks(file);
    expect(infos[1].drums && !infos[2].drums && infos[2].program.value_or(-1) == 33, "track descriptions");
    expect(describeTrack(infos[2]).find("Bass") != std::string::npos, "track label");

    AdaptiveSongOptions opt;
    opt.referenceTracks = { 0 };
    opt.backingTracks = { { 0 }, { 1 }, { 2 }, { 3 } };   // includes the reference by mistake
    opt.reservedOutputChannels = 0x0007;                   // live voices on 1..3
    const auto res = buildAdaptiveSong(file, nullptr, opt);
    expect(res.ok, "single-file song builds: " + res.error);
    bool warnedRef = false, warnedRemap = false;
    for (const auto& w : res.song.warnings) {
        warnedRef |= w.find("reference part") != std::string::npos;
        warnedRemap |= w.find("used by the live keyboard") != std::string::npos;
    }
    expect(warnedRef, "reference track is never played as backing");
    expect(warnedRemap, "remap of backing off the live channels reported");
    expect(res.song.channelMap[9] == 10, "drums stay on 10");
    expect(res.song.channelMap[0] != 1 && res.song.channelMap[0] != 10 && res.song.channelMap[0] != 0,
           "pad (ch1) moved to a free melodic channel");
    expect(res.song.channelMap[1] != 2, "bass (ch2) moved off reserved channel 2");
    bool pianoInBacking = false;
    for (const auto& e : res.song.backing->events()) pianoInBacking |= e.sourceTrack == 0;
    expect(!pianoInBacking, "no reference events in the backing");
    expect(!res.song.twoFiles && !res.song.timelineMismatch, "single-file flags");
    expect(near(res.song.startNominalSec, 0.0, 1e-9), "song starts at the first reference note");

    AdaptiveSongOptions none;
    expect(!buildAdaptiveSong(file, nullptr, none).ok, "no reference track -> error");
    AdaptiveSongOptions emptyRef;
    emptyRef.referenceTracks = { 1 };   // drums only -> excluded -> empty reference
    expect(!buildAdaptiveSong(file, nullptr, emptyRef).ok, "drum-only reference -> error");
}

void songFormatZeroChannelSplit()
{
    // Format-0 style: one track, piano on channel 1, bass on channel 2.
    MidiTrackData all;
    for (int i = 0; i < 8; ++i) { addNote(all, i * 480, 400, 60 + i, 90, 0); addNote(all, i * 480, 400, 36, 90, 1); }
    auto file = fileWith({ all });
    AdaptiveSongOptions opt;
    opt.referenceTracks = { 0 };
    opt.referenceChannel = 0;
    opt.backingTracks = { { 0 } };
    const auto res = buildAdaptiveSong(file, nullptr, opt);
    expect(res.ok && res.song.reference->notes().size() == 8, "format 0: reference = channel 1 only");
    int bassNotes = 0, pianoNotes = 0;
    for (const auto& e : res.song.backing->events())
        if (e.kind == MidiEventKind::NoteOn) { if (e.data1 == 36) ++bassNotes; else ++pianoNotes; }
    expect(bassNotes == 8 && pianoNotes == 0, "format 0: backing = the other channels of the same track");
}

void songTwoFiles()
{
    // reference.mid at PPQ 480, backing.mid at PPQ 96 with the same tempo: rescaled, no mismatch.
    auto refFile = fileWith({ trackFromSteps(syntheticPiece(16, 480)) }, 480, 100.0);
    MidiTrackData drums;
    for (int i = 0; i < 16; ++i) addNote(drums, i * 96, 20, 36, 100, 9);
    auto backFile = fileWith({ drums }, 96, 100.0);
    AdaptiveSongOptions opt;
    opt.referenceTracks = { 0 };
    opt.backingTracks = { { 0 } };
    auto res = buildAdaptiveSong(refFile, &backFile, opt);
    expect(res.ok && res.song.twoFiles, "two-file song builds");
    expect(!res.song.timelineMismatch, "same tempo, different PPQ: compatible");
    bool ppqWarn = false;
    for (const auto& w : res.song.warnings) ppqWarn |= w.find("PPQ differs") != std::string::npos;
    expect(ppqWarn, "PPQ rescale reported");
    expect(near(res.song.backing->events()[2].tick, 480.0, 1e-9), "backing beat 2 lands on reference tick 480");

    // Different tempo: reported as a mismatch (not silently assumed).
    auto fastBack = fileWith({ drums }, 96, 140.0);
    res = buildAdaptiveSong(refFile, &fastBack, opt);
    expect(res.ok && res.song.timelineMismatch, "different tempo maps -> timelineMismatch");
    bool tempoWarn = false;
    for (const auto& w : res.song.warnings) tempoWarn |= w.find("tempo maps") != std::string::npos;
    expect(tempoWarn, "tempo mismatch warning text");
    // Aligned by seconds: beat 2 of the 140 BPM file (0.4286 s) maps to 0.4286 s on the 100 BPM reference.
    opt.alignment = TimelineAlignment::BySeconds;
    res = buildAdaptiveSong(refFile, &fastBack, opt);
    const double expected = res.song.reference->tempoMap().secondsToTicks(60.0 / 140.0);
    expect(near(res.song.backing->events()[2].tick, expected, 0.01), "BySeconds mapping (" + num(res.song.backing->events()[2].tick) + " vs " + num(expected) + ")");

    // Length mismatch reported.
    MidiTrackData longDrums;
    for (int i = 0; i < 64; ++i) addNote(longDrums, i * 96, 20, 36, 100, 9);
    auto longBack = fileWith({ longDrums }, 96, 100.0);
    opt.alignment = TimelineAlignment::ByBeats;
    res = buildAdaptiveSong(refFile, &longBack, opt);
    bool lenWarn = false;
    for (const auto& w : res.song.warnings) lenWarn |= w.find("lengths differ") != std::string::npos;
    expect(lenWarn, "length mismatch reported");
    expect(res.song.endNominalSec > res.song.reference->tempoMap().ticksToSeconds(16 * 480.0),
           "song end covers the longer backing (outro after the last reference note)");
}

void channelAssignment()
{
    const auto m = assignOutputChannels(0xFFFF, 0x0001);   // all 16 used, 1 reserved
    expect(m[0] == 0, "no free channel -> dropped");
    const auto m2 = assignOutputChannels(0x0003 | (1u << 9), 0x0003);
    expect(m2[0] != 0 && m2[1] != 0 && m2[0] != m2[1] && m2[0] != 10 && m2[1] != 10, "two remapped, distinct, not 10");
    expect(m2[9] == 10, "drums untouched");
    const auto m3 = assignOutputChannels(1u << 9, 1u << 9);
    expect(m3[9] != 10 && m3[9] != 0, "reserved drum channel: drums move elsewhere");
}
}

int main()
{
    rendererBasics();
    seekReleasesAndChases();
    jumpSkipsInsteadOfFlooding();
    songSingleFile();
    songFormatZeroChannelSplit();
    songTwoFiles();
    channelAssignment();

    if (failures == 0) std::cout << "adaptive backing tests: all passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
