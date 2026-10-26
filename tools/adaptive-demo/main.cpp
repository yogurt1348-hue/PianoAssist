// adaptive-demo — writes a copyright-free multitrack demo song for trying and
// validating Adaptive Song Mode with a real keyboard.
//
//   adaptive-demo <out.mid> [--bpm N] [--repeats N] [--tempo-change] [--right-hand-only]
//
// Track 1 "Piano - YOUR PART" : the part YOU play (easy: a C-major melody in
//                     the right hand over block chords on beat 1 of every bar).
//                     The name marks it; Adaptive Song Mode also picks it.
// Track 2 "Drums"   : kick / snare / hi-hat on channel 10.
// Track 3 "Bass"    : root notes, channel 2 (GM Fingered Bass).
// Track 4 "Strings" : sustained pad, channel 3 (GM Strings).
// --tempo-change writes a written tempo change (+20 BPM) half way through.
// --right-hand-only keeps only the melody in your part and moves the left-hand
//                     chords to an extra track "Left hand (band plays)",
//                     channel 4, so a beginner plays one hand.

#include "Adaptive/MidiFile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace cadenza::adaptive;

namespace
{
MidiChannelEvent program(int channel, int prog)
{
    MidiChannelEvent e;
    e.kind = MidiEventKind::ProgramChange;
    e.channel = static_cast<std::uint8_t>(channel);
    e.data1 = static_cast<std::uint8_t>(prog);
    return e;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: adaptive-demo <out.mid> [--bpm N] [--repeats N] [--tempo-change] [--right-hand-only]\n");
        return 2;
    }
    const std::string out = argv[1];
    int bpm = 90;
    int repeats = 2;
    bool tempoChange = false;
    bool rightHandOnly = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--bpm" && i + 1 < argc) bpm = std::atoi(argv[++i]);
        else if (a == "--repeats" && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (a == "--tempo-change") tempoChange = true;
        else if (a == "--right-hand-only") rightHandOnly = true;
    }
    if (bpm < 30 || bpm > 240 || repeats < 1 || repeats > 16) {
        std::fprintf(stderr, "bpm must be 30..240, repeats 1..16\n");
        return 2;
    }

    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;
    // 8-bar progression C F G C | Am F G C with a stepwise melody (quarter notes).
    static const int chordRoot[8] = { 48, 53, 55, 48, 57, 53, 55, 48 };
    static const int chordThird[8] = { 4, 4, 4, 4, 3, 4, 4, 4 };
    static const int melody[8][4] = {
        { 64, 65, 67, 64 }, { 65, 67, 69, 65 }, { 67, 65, 64, 62 }, { 60, 62, 64, 60 },
        { 64, 65, 67, 69 }, { 69, 67, 65, 64 }, { 62, 64, 65, 62 }, { 60, 64, 67, 72 },
    };

    MidiFileData file;
    file.format = 1;
    file.ppq = ppq;
    file.tempos.push_back({ 0, static_cast<std::uint32_t>(60'000'000 / bpm) });
    file.timeSignatures.push_back({ 0, 4, 4 });

    MidiTrackData piano, leftHand, drums, bass, pad;
    piano.name = rightHandOnly ? "Piano right hand - YOUR PART" : "Piano - YOUR PART";
    leftHand.name = "Left hand (band plays)";
    drums.name = "Drums";
    bass.name = "Bass";
    pad.name = "Strings";
    piano.events.push_back(program(0, 0));
    bass.events.push_back(program(1, 33));
    pad.events.push_back(program(2, 48));
    leftHand.events.push_back(program(3, 0));

    const int bars = 8 * repeats;
    for (int b = 0; b < bars; ++b) {
        const int s = b % 8;
        const std::int64_t t0 = static_cast<std::int64_t>(b) * bar;
        const int root = chordRoot[s];
        // Left hand: block chord on beat 1 (root, third, fifth), held for the bar.
        for (int p : { root, root + chordThird[s], root + 7 }) {
            if (rightHandOnly) addNote(leftHand, t0, bar - 40, p, 80, 3);
            else addNote(piano, t0, bar - 40, p, 80, 0);
        }
        // Right hand: four quarter notes.
        for (int q = 0; q < 4; ++q) addNote(piano, t0 + q * ppq, ppq - 40, melody[s][q], 92, 0);

        for (int e = 0; e < 8; ++e) {
            const std::int64_t t = t0 + e * (ppq / 2);
            addNote(drums, t, 60, 42, e % 2 ? 70 : 90, 9);          // closed hi-hat 8ths
            if (e == 0 || e == 4 || e == 5) addNote(drums, t, 60, 36, 110, 9);   // kick
            if (e == 2 || e == 6) addNote(drums, t, 60, 38, 105, 9);             // snare
        }
        addNote(bass, t0, ppq * 2 - 30, root - 12, 100, 1);
        addNote(bass, t0 + 2 * ppq, ppq - 30, root - 12, 90, 1);
        addNote(bass, t0 + 3 * ppq, ppq - 30, root - 5, 90, 1);
        addNote(pad, t0, bar - 20, root + 12, 60, 2);
        addNote(pad, t0, bar - 20, root + 12 + chordThird[s], 60, 2);
    }
    // Ending: a final chord + crash.
    const std::int64_t end = static_cast<std::int64_t>(bars) * bar;
    for (int p : { 48, 52, 55 }) {
        if (rightHandOnly) addNote(leftHand, end, bar, p, 90, 3);
        else addNote(piano, end, bar, p, 90, 0);
    }
    for (int p : { 60, 64, 72 }) addNote(piano, end, bar, p, 90, 0);
    addNote(drums, end, ppq, 49, 110, 9);
    addNote(drums, end, ppq, 36, 110, 9);
    addNote(bass, end, bar, 36, 100, 1);

    if (tempoChange)
        file.tempos.push_back({ static_cast<std::int64_t>(bars / 2) * bar, static_cast<std::uint32_t>(60'000'000 / (bpm + 20)) });

    std::vector<MidiTrackData*> tracks { &piano, &drums, &bass, &pad };
    if (rightHandOnly) tracks.insert(tracks.begin() + 1, &leftHand);
    for (auto* t : tracks) {
        t->endTick = end + bar;
        finalizeTrack(*t);
        file.tracks.push_back(*t);
    }
    const auto bytes = writeMidiFile(file);
    std::ofstream f(out, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("wrote %s: %d bars at %d BPM%s, %d tracks (%s = reference)\n", out.c_str(), bars + 1, bpm,
                tempoChange ? " with a written tempo change" : "", static_cast<int>(tracks.size()), piano.name.c_str());
    return f.good() ? 0 : 1;
}
