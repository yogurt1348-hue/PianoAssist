#include "audio/PlaybackEngine.h"
#include "lesson_engine/Gate.h"
#include "lesson_engine/PracticeSession.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace pianoassist;
using namespace pianoassist::lesson;
using timeline::Note;
using timeline::NoteRole;
using Catch::Approx;

namespace
{
// The table-driven cases below are written for the 120/180 ms windows.
const lesson::PracticeProfile strict = lesson::profileFor (lesson::Tolerance::strict);
} // namespace

namespace
{
Note note (timeline::NoteId id, std::uint8_t pitch, double start, NoteRole role, double length = 0.4)
{
    Note n;
    n.id = id;
    n.pitch = pitch;
    n.startSeconds = start;
    n.endSeconds = start + length;
    n.startTick = static_cast<std::int64_t> (start * 960);
    n.endTick = static_cast<std::int64_t> ((start + length) * 960);
    n.role = role;
    return n;
}

timeline::Timeline makeTimeline (std::vector<Note> notes)
{
    auto t = timeline::Timeline::create (std::move (notes));
    REQUIRE (t.isOk());
    return std::move (t).value();
}

// Right-hand C-E-G chord at 1.0 s (E 10 ms late in the file), left C3 at 1.0 s,
// right D4 at 2.0 s, backing only at 3.0 s, right E4 at 4.0 s.
timeline::Timeline song()
{
    return makeTimeline ({
        note (0, 60, 1.000, NoteRole::rightHand),
        note (1, 64, 1.010, NoteRole::rightHand),
        note (2, 67, 1.000, NoteRole::rightHand),
        note (3, 48, 1.000, NoteRole::leftHand),
        note (4, 62, 2.000, NoteRole::rightHand),
        note (5, 36, 3.000, NoteRole::backing),
        note (6, 64, 4.000, NoteRole::rightHand),
    });
}

std::vector<std::uint8_t> pitches (std::initializer_list<int> list)
{
    std::vector<std::uint8_t> out;
    for (const int p : list)
        out.push_back (static_cast<std::uint8_t> (p));
    return out;
}
} // namespace

TEST_CASE ("Gates group chords by onset and follow the practice mode")
{
    const auto timeline = song();

    const auto both = buildGates (timeline, PracticeMode::bothHands);
    REQUIRE (both.size() == 3);
    CHECK (both[0].onsetSeconds == 1.0);
    CHECK (both[0].requiredPitches == pitches ({ 48, 60, 64, 67 }));
    CHECK (both[1].requiredPitches == pitches ({ 62 }));
    CHECK (both[2].onsetSeconds == 4.0); // backing-only span at 3 s has no gate

    const auto right = buildGates (timeline, PracticeMode::rightHand);
    REQUIRE (right.size() == 3);
    CHECK (right[0].requiredPitches == pitches ({ 60, 64, 67 }));

    const auto left = buildGates (timeline, PracticeMode::leftHand);
    REQUIRE (left.size() == 1);
    CHECK (left[0].requiredPitches == pitches ({ 48 }));
}

TEST_CASE ("Unison duplicates collapse unless configured otherwise")
{
    const auto timeline = makeTimeline ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 60, 1.0, NoteRole::leftHand) });
    CHECK (buildGates (timeline, PracticeMode::bothHands)[0].requiredPitches == pitches ({ 60 }));
    GateConfig keep;
    keep.collapseUnisonDuplicates = false;
    CHECK (buildGates (timeline, PracticeMode::bothHands, keep)[0].requiredPitches == pitches ({ 60, 60 }));
}

TEST_CASE ("Single-note gate: timing classification")
{
    const auto gates = buildGates (makeTimeline ({ note (0, 60, 2.0, NoteRole::rightHand) }), PracticeMode::rightHand);

    struct Case
    {
        double pressAt;
        PressVerdict verdict;
        bool passes;
    };
    const auto c = GENERATE (Case { 1.800, PressVerdict::tooEarly, false },       // 200 ms early
                             Case { 1.879, PressVerdict::tooEarly, false },       // just outside 120 ms
                             Case { 1.880, PressVerdict::acceptedEarly, true },   // exactly on the lower bound
                             Case { 1.950, PressVerdict::acceptedEarly, true },
                             Case { 2.000, PressVerdict::acceptedOnTime, true },
                             Case { 2.180, PressVerdict::acceptedOnTime, true },  // exactly on the upper bound
                             Case { 2.181, PressVerdict::acceptedLate, true },    // just outside: still waits, late
                             Case { 9.000, PressVerdict::acceptedLate, true });

    PracticeSession session (gates, strict);
    session.resetTo (0.0);
    const auto result = session.press (60, c.pressAt);
    CHECK (result.verdict == c.verdict);
    CHECK (result.gatePassed == c.passes);
    CHECK (session.finished() == c.passes);
}

TEST_CASE ("Chords pass in any order; wrong notes never pass or reset")
{
    const auto gates = buildGates (song(), PracticeMode::rightHand);
    std::vector<std::uint8_t> order { 60, 64, 67 };
    do
    {
        PracticeSession session (gates, strict);
        session.resetTo (0.0);
        CHECK (session.press (61, 1.0).verdict == PressVerdict::wrongPitch);
        CHECK_FALSE (session.press (order[0], 1.0).gatePassed);
        CHECK (session.press (63, 1.0).verdict == PressVerdict::wrongPitch);
        CHECK_FALSE (session.press (order[1], 1.05).gatePassed);
        CHECK (session.missingPitches() == std::vector<std::uint8_t> { order[2] });
        CHECK (session.press (order[2], 1.1).gatePassed);
        REQUIRE (session.currentGate() != nullptr);
        CHECK (session.currentGate()->requiredPitches == pitches ({ 62 }));
    } while (std::next_permutation (order.begin(), order.end()));
}

TEST_CASE ("A required pitch matches once per multiplicity")
{
    GateConfig keep;
    keep.collapseUnisonDuplicates = false;
    const auto timeline = makeTimeline ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 60, 1.0, NoteRole::leftHand),
                                          note (2, 64, 1.0, NoteRole::rightHand) });
    PracticeSession session (buildGates (timeline, PracticeMode::bothHands, keep), strict);
    session.resetTo (0.0);
    CHECK (session.press (60, 1.0).verdict == PressVerdict::acceptedOnTime);
    CHECK (session.press (64, 1.0).verdict == PressVerdict::acceptedOnTime);
    CHECK_FALSE (session.finished());
    CHECK (session.press (60, 1.1).gatePassed); // second strike satisfies the duplicate
}

TEST_CASE ("Rapid repeated pitch: each gate needs its own press")
{
    const auto timeline = makeTimeline ({ note (0, 60, 1.0, NoteRole::rightHand, 0.1), note (1, 60, 1.125, NoteRole::rightHand, 0.1),
                                          note (2, 60, 1.25, NoteRole::rightHand, 0.1) });
    PracticeSession session (buildGates (timeline, PracticeMode::rightHand), strict);
    session.resetTo (0.0);
    CHECK (session.press (60, 1.0).gatePassed);
    CHECK (session.press (60, 1.0).verdict == PressVerdict::tooEarly);        // 125 ms before the second
    CHECK (session.press (60, 1.01).verdict == PressVerdict::acceptedEarly);  // 115 ms before it
    CHECK (session.currentGate()->index == 2);
    CHECK (session.press (60, 1.25).verdict == PressVerdict::acceptedOnTime);
    CHECK (session.finished());
}

TEST_CASE ("Already-matched presses carry into a close next gate")
{
    // Gate A at 1.0 (C4), gate B at 1.08 (C4 again): one press while waiting at A
    // passes A; a second press 50 ms later is early for B and is accepted.
    const auto timeline = makeTimeline ({ note (0, 60, 1.0, NoteRole::rightHand, 0.05), note (1, 62, 1.08, NoteRole::rightHand, 0.05) });
    PracticeSession session (buildGates (timeline, PracticeMode::rightHand), strict);
    session.resetTo (0.0);
    CHECK (session.press (62, 1.0).verdict == PressVerdict::wrongPitch); // D early, before C
    CHECK (session.press (60, 1.01).gatePassed);                         // C passes A, D carries into B
    CHECK (session.finished());
    CHECK (session.passedGateCount() == 2);
}

TEST_CASE ("Seeking resets the current gate and drops partial matches")
{
    const auto gates = buildGates (song(), PracticeMode::bothHands);
    PracticeSession session (gates, strict);
    session.resetTo (0.0);
    session.press (60, 1.0);
    session.resetTo (1.0); // seek exactly onto the chord
    CHECK (session.currentGate()->index == 0);
    CHECK (session.matchedPitches().empty());
    session.resetTo (1.5);
    CHECK (session.currentGate()->index == 1);
    session.resetTo (10.0);
    CHECK (session.finished());
    CHECK (session.press (60, 10.0).verdict == PressVerdict::noGate);
}

namespace
{
struct NullRenderer final : audio::IInstrumentRenderer
{
    void prepare (double, int) override {}
    void render (std::span<const audio::ScheduledMidiEvent> events, audio::RenderBlock) noexcept override
    {
        for (const auto& e : events)
            if ((e.status & 0xF0) == 0x90)
                ++noteOns;
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override {}
    int reportedLatencyFrames() const noexcept override { return 0; }
    int noteOns = 0;
};

struct Run
{
    std::vector<double> holdPositions;
    std::vector<std::int64_t> holdFrames;
    int noteOns = 0;
    std::size_t passed = 0;
    audio::PlaybackState finalState {};
};

/** Plays the song in practice mode with a learner who presses each gate's
    pitches a fixed time after the transport holds there. Wall time is the
    rendered frame count, so the run is a pure function of its inputs. */
Run practise (int blockSize, double sampleRate, double reactionSeconds)
{
    const auto timeline = song();
    audio::PlaybackEventList list;
    list.id = 1;
    list.durationSeconds = 5.0;
    for (const auto& n : timeline.notes())
        if (n.role == NoteRole::backing)
        {
            list.events.push_back ({ n.startSeconds, 0x90, n.pitch, 80 });
            list.events.push_back ({ n.endSeconds, 0x80, n.pitch, 64 });
        }

    PracticeSession session (buildGates (timeline, PracticeMode::rightHand), strict);
    session.resetTo (0.0);

    audio::PlaybackEngine engine;
    NullRenderer renderer;
    engine.prepare (sampleRate);
    engine.handle ({ audio::PlaybackCommand::Type::setEvents, 0.0, &list }, renderer);
    engine.handle ({ audio::PlaybackCommand::Type::setGate, *session.nextBoundarySeconds() }, renderer);
    engine.handle ({ audio::PlaybackCommand::Type::play }, renderer);

    Run run;
    std::int64_t frame = 0;
    std::uint64_t arrivals = 0;
    std::int64_t heldSince = -1;
    while (frame < static_cast<std::int64_t> (20 * sampleRate) && engine.state() != audio::PlaybackState::completed)
    {
        engine.process (renderer, { nullptr, 2, blockSize });
        frame += blockSize;
        if (engine.gateArrivals() != arrivals)
        {
            arrivals = engine.gateArrivals();
            heldSince = frame;
            run.holdPositions.push_back (engine.positionSeconds());
        }
        if (heldSince >= 0 && engine.state() == audio::PlaybackState::waitingForGate
            && static_cast<double> (frame - heldSince) / sampleRate >= reactionSeconds)
        {
            const auto boundary = *session.nextBoundarySeconds();
            run.holdFrames.push_back (frame - heldSince);
            for (const auto pitch : session.missingPitches())
                session.press (pitch, boundary + reactionSeconds);
            const auto next = session.nextBoundarySeconds();
            engine.handle ({ audio::PlaybackCommand::Type::setGate, next ? *next : -1.0 }, renderer);
            heldSince = -1;
        }
    }
    run.noteOns = renderer.noteOns;
    run.passed = session.passedGateCount();
    run.finalState = engine.state();
    return run;
}
} // namespace

TEST_CASE ("Practice transport holds exactly at each gate for any buffer size")
{
    const int blockSize = GENERATE (128, 256, 512, 1024);
    const double sampleRate = GENERATE (44100.0, 48000.0);
    const auto run = practise (blockSize, sampleRate, 0.5);

    REQUIRE (run.holdPositions.size() == 3);
    // Holds at the end of each late window (onset + 180 ms), not at the onset.
    CHECK (run.holdPositions[0] == Approx (1.18));
    CHECK (run.holdPositions[1] == Approx (2.18));
    CHECK (run.holdPositions[2] == Approx (4.18)); // backing at 3 s played through
    CHECK (run.passed == 3);
    CHECK (run.noteOns == 1);                     // the backing note, once
    CHECK (run.finalState == audio::PlaybackState::completed);
}

TEST_CASE ("The same input yields identical decisions regardless of block size")
{
    const auto reference = practise (128, 48000.0, 0.3);
    for (const int blockSize : { 64, 256, 480, 1024 })
    {
        const auto run = practise (blockSize, 48000.0, 0.3);
        CHECK (run.holdPositions == reference.holdPositions);
        CHECK (run.passed == reference.passed);
        CHECK (run.noteOns == reference.noteOns);
    }
}

TEST_CASE ("A gate passed before the transport reaches it causes no hold")
{
    const auto timeline = makeTimeline ({ note (0, 60, 1.0, NoteRole::rightHand) });
    audio::PlaybackEventList list;
    list.id = 1;
    list.durationSeconds = 2.0;

    PracticeSession session (buildGates (timeline, PracticeMode::rightHand), strict);
    session.resetTo (0.0);
    audio::PlaybackEngine engine;
    NullRenderer renderer;
    engine.prepare (48000.0);
    engine.handle ({ audio::PlaybackCommand::Type::setEvents, 0.0, &list }, renderer);
    engine.handle ({ audio::PlaybackCommand::Type::setGate, 1.0 }, renderer);
    engine.handle ({ audio::PlaybackCommand::Type::play }, renderer);

    for (int i = 0; i < 90; ++i) // 0.96 s
        engine.process (renderer, { nullptr, 2, 512 });
    CHECK (session.press (60, engine.positionSeconds()).gatePassed); // ~40 ms early
    engine.handle ({ audio::PlaybackCommand::Type::setGate, -1.0 }, renderer);
    for (int i = 0; i < 200; ++i)
        engine.process (renderer, { nullptr, 2, 512 });
    CHECK (engine.gateArrivals() == 0);
    CHECK (engine.state() == audio::PlaybackState::completed);
}
