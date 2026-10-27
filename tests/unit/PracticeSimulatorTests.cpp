// Musical scenarios run end to end: the real playback engine and gate
// session, a simulated learner and a clock driven by rendered frames, so
// every run is reproducible. Mirrors what the lesson thread does
// (LessonController): presses map to song time with songTimeOfPress, a
// passed gate moves the hold, loops use loopShouldJump.

#include "audio/PlaybackEngine.h"
#include "lesson_engine/Gate.h"
#include "lesson_engine/PracticeClock.h"
#include "lesson_engine/PracticeMode.h"
#include "lesson_engine/PracticeSession.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
const PracticeProfile strict = profileFor (Tolerance::strict); // 120 ms early, 180 ms late

Note note (timeline::NoteId id, int pitch, double start, NoteRole role, double length = 0.3)
{
    Note n;
    n.id = id;
    n.pitch = static_cast<std::uint8_t> (pitch);
    n.startSeconds = start;
    n.endSeconds = start + length;
    n.startTick = static_cast<std::int64_t> (start * 960);
    n.endTick = static_cast<std::int64_t> ((start + length) * 960);
    n.role = role;
    n.channel = role == NoteRole::backing ? 1 : 0;
    return n;
}

struct Sounded
{
    double wallSeconds;
    double songSeconds; ///< position when the block started
    std::uint8_t status, data1, data2;
};

struct Recorder final : audio::IInstrumentRenderer
{
    void prepare (double, int) override {}
    void render (std::span<const audio::ScheduledMidiEvent> events, audio::RenderBlock) noexcept override
    {
        for (const auto& e : events)
            log.push_back ({ wall, song, e.status, e.data1, e.data2 });
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override {}
    int reportedLatencyFrames() const noexcept override { return 0; }

    double wall = 0.0, song = 0.0;
    std::vector<Sounded> log;

    std::vector<Sounded> noteOns (std::uint8_t pitch) const
    {
        std::vector<Sounded> out;
        for (const auto& s : log)
            if ((s.status & 0xF0) == 0x90 && s.data1 == pitch && s.data2 > 0)
                out.push_back (s);
        return out;
    }
};

/** A practice run with a simulated learner. Time only moves when run() renders. */
class Simulator
{
public:
    Simulator (std::vector<Note> notes, PracticeMode mode, double rate = 1.0, std::vector<audio::PlaybackEvent> extra = {})
        : timeline_ (std::move (timeline::Timeline::create (std::move (notes))).value()),
          session_ (buildGates (timeline_, mode), strict)
    {
        list_.id = 1;
        for (const auto& n : timeline_.notes())
            if (isPlayedByApp (n.role, true, mode, {}))
            {
                list_.events.push_back ({ n.startSeconds, static_cast<std::uint8_t> (0x90 | n.channel), n.pitch, 80 });
                list_.events.push_back ({ n.endSeconds, static_cast<std::uint8_t> (0x80 | n.channel), n.pitch, 64 });
            }
        for (const auto& e : extra)
            list_.events.push_back (e);
        std::stable_sort (list_.events.begin(), list_.events.end(), [] (const auto& a, const auto& b) { return a.seconds < b.seconds; });
        list_.durationSeconds = 0.0;
        for (const auto& n : timeline_.notes())
            list_.durationSeconds = std::max (list_.durationSeconds, n.endSeconds);
        list_.durationSeconds += 0.5;

        engine_.prepare (sampleRate);
        engine_.handle ({ audio::PlaybackCommand::Type::setEvents, 0.0, &list_ }, recorder_);
        engine_.handle ({ audio::PlaybackCommand::Type::setRate, rate }, recorder_);
        session_.resetTo (0.0);
        sendGate();
    }

    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 256;

    void play() { engine_.handle ({ audio::PlaybackCommand::Type::play }, recorder_); }
    void pause() { engine_.handle ({ audio::PlaybackCommand::Type::pause }, recorder_); }
    void seek (double seconds)
    {
        engine_.handle ({ audio::PlaybackCommand::Type::seek, seconds }, recorder_);
        session_.resetTo (seconds);
        sendGate();
    }

    std::int64_t nowMicros() const { return static_cast<std::int64_t> (std::llround (frame_ * 1.0e6 / sampleRate)); }
    double wallSeconds() const { return static_cast<double> (frame_) / sampleRate; }

    void step()
    {
        blockStartPosition_ = engine_.positionSeconds();
        blockStartMicros_ = nowMicros();
        recorder_.wall = wallSeconds();
        recorder_.song = blockStartPosition_;
        engine_.process (recorder_, { nullptr, 2, blockSize });
        frame_ += blockSize;
        if (engine_.gateArrivals() != arrivals_)
        {
            arrivals_ = engine_.gateArrivals();
            arrivalMicros_ = nowMicros();
        }
        if (loop_.valid())
        {
            const bool running = engine_.state() == audio::PlaybackState::playing || engine_.state() == audio::PlaybackState::waitingForGate;
            std::optional<double> pending;
            if (const auto* gate = session_.currentGate())
                pending = gate->onsetSeconds;
            if (loopShouldJump (loop_, running, false, engine_.positionSeconds(), list_.durationSeconds, pending))
            {
                ++loopJumps;
                engine_.handle ({ audio::PlaybackCommand::Type::stop }, recorder_);
                engine_.handle ({ audio::PlaybackCommand::Type::seek, loop_.start }, recorder_);
                session_.resetTo (loop_.start);
                sendGate();
                play();
            }
        }
    }

    void run (double seconds)
    {
        const auto until = frame_ + static_cast<std::int64_t> (seconds * sampleRate);
        while (frame_ < until)
            step();
    }

    /** Renders until the transport holds at a gate; false if it never does. */
    bool runUntilWaiting (double maxSeconds = 30.0)
    {
        const auto until = frame_ + static_cast<std::int64_t> (maxSeconds * sampleRate);
        while (frame_ < until && engine_.state() != audio::PlaybackState::waitingForGate)
            step();
        return engine_.state() == audio::PlaybackState::waitingForGate;
    }

    void runUntilPosition (double seconds)
    {
        while (engine_.positionSeconds() < seconds && engine_.state() == audio::PlaybackState::playing)
            step();
    }

    /** A key press now. Ignored unless the song runs or waits, like the lesson thread. */
    PressResult press (int pitch)
    {
        const auto state = engine_.state();
        if (state != audio::PlaybackState::playing && state != audio::PlaybackState::waitingForGate)
            return {};
        TransportClock clock;
        clock.waiting = state == audio::PlaybackState::waitingForGate;
        clock.positionSeconds = engine_.positionSeconds();
        clock.arrivalMicros = arrivalMicros_;
        clock.blockStartPosition = blockStartPosition_;
        clock.blockStartMicros = blockStartMicros_;
        clock.rate = engine_.rate();
        const auto result = session_.press (static_cast<std::uint8_t> (pitch), songTimeOfPress (clock, nowMicros(), 0.0));
        if (result.gatePassed)
            sendGate();
        return result;
    }

    void setLoop (double start, double end) { loop_ = { true, start, end }; }

    audio::PlaybackEngine& engine() { return engine_; }
    PracticeSession& session() { return session_; }
    Recorder& recorder() { return recorder_; }
    int loopJumps = 0;

private:
    void sendGate()
    {
        const auto next = session_.nextBoundarySeconds();
        engine_.handle ({ audio::PlaybackCommand::Type::setGate, next ? *next : -1.0 }, recorder_);
    }

    timeline::Timeline timeline_;
    PracticeSession session_;
    audio::PlaybackEventList list_;
    audio::PlaybackEngine engine_;
    Recorder recorder_;
    std::int64_t frame_ = 0;
    std::uint64_t arrivals_ = 0;
    std::int64_t arrivalMicros_ = 0;
    double blockStartPosition_ = 0.0;
    std::int64_t blockStartMicros_ = 0;
    LoopRange loop_;
};

std::vector<std::uint8_t> pitches (std::initializer_list<int> list)
{
    std::vector<std::uint8_t> out;
    for (const int p : list)
        out.push_back (static_cast<std::uint8_t> (p));
    return out;
}

using State = audio::PlaybackState;
} // namespace

TEST_CASE ("Simulator: after any wait the right note passes, and old presses do not pass the next gate")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 62, 2.0, NoteRole::rightHand) }, PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.engine().positionSeconds() == Approx (1.18));

    sim.run (60.0); // a long wait: the gate stays open
    CHECK (sim.engine().state() == State::waitingForGate);
    CHECK (sim.press (62).verdict == PressVerdict::wrongPitch); // the next gate's note, too soon
    CHECK (sim.press (64).verdict == PressVerdict::wrongPitch);
    sim.run (1.0);
    const auto passed = sim.press (60);
    CHECK (passed.gatePassed);
    CHECK (passed.verdict == PressVerdict::acceptedLate);

    // The D pressed during the wait does not count for the D gate.
    REQUIRE (sim.session().currentGate() != nullptr);
    CHECK (sim.session().currentGate()->requiredPitches == pitches ({ 62 }));
    CHECK (sim.session().missingPitches() == pitches ({ 62 }));

    // The song resumes from the hold; a D on its beat is on time, not "late by a minute".
    sim.run (0.02);
    CHECK (sim.engine().state() == State::playing);
    sim.runUntilPosition (2.0);
    const auto onBeat = sim.press (62);
    CHECK (onBeat.gatePassed);
    CHECK (std::abs (onBeat.offsetMs) < 10.0);
}

TEST_CASE ("Simulator: a chord passes in any order, wrong notes in between do not matter")
{
    const std::vector<std::vector<int>> orders { { 60, 64, 67 }, { 67, 60, 64 }, { 64, 67, 60 } };
    for (const auto& order : orders)
    {
        Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 64, 1.0, NoteRole::rightHand), note (2, 67, 1.0, NoteRole::rightHand) },
                       PracticeMode::rightHand);
        sim.play();
        REQUIRE (sim.runUntilWaiting());
        sim.run (3.0);
        CHECK_FALSE (sim.press (order[0]).gatePassed);
        CHECK (sim.press (61).verdict == PressVerdict::wrongPitch);
        sim.run (0.2);
        CHECK_FALSE (sim.press (order[1]).gatePassed);
        sim.run (0.2);
        CHECK (sim.press (order[2]).gatePassed);
        CHECK (sim.session().finished());
    }
}

TEST_CASE ("Simulator: an incomplete chord attempt expires and must be played again")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 64, 1.0, NoteRole::rightHand), note (2, 67, 1.0, NoteRole::rightHand) },
                   PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    sim.press (60);
    sim.press (64);
    sim.run (PracticeSession::chordWindowSeconds + 1.0); // e.g. the USB cable was out for a moment
    CHECK_FALSE (sim.press (67).gatePassed);
    CHECK (sim.session().missingPitches() == pitches ({ 60, 64 }));
    sim.run (0.1);
    sim.press (60);
    CHECK (sim.press (64).gatePassed);
}

TEST_CASE ("Simulator: a held key is not a new press; a repeated note needs a release and a new strike")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand, 0.9), note (1, 60, 2.0, NoteRole::rightHand) }, PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.press (60).gatePassed);
    // Keeping C down sends nothing more: the song waits at the second C.
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.engine().positionSeconds() == Approx (2.18));
    CHECK (sim.session().missingPitches() == pitches ({ 60 }));
    CHECK (sim.press (60).gatePassed); // released and struck again
    CHECK (sim.session().finished());
}

TEST_CASE ("Simulator: fast repeats of one key, each with its own strike")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand, 0.1), note (1, 60, 1.2, NoteRole::rightHand, 0.1),
                     note (2, 60, 1.4, NoteRole::rightHand, 0.1) },
                   PracticeMode::rightHand);
    sim.play();
    for (const double at : { 1.0, 1.2, 1.4 })
    {
        sim.runUntilPosition (at);
        CHECK (sim.press (60).gatePassed);
    }
    sim.run (1.0);
    CHECK (sim.engine().gateArrivals() == 0); // played in time: never held
    CHECK (sim.session().passedGateCount() == 3);
}

TEST_CASE ("Simulator: early presses inside the window count, earlier ones are ignored")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand) }, PracticeMode::rightHand);
    sim.play();
    sim.runUntilPosition (0.7);
    CHECK (sim.press (60).verdict == PressVerdict::tooEarly); // 300 ms early
    sim.runUntilPosition (0.92);
    const auto early = sim.press (60);                         // about 80 ms early
    CHECK (early.verdict == PressVerdict::acceptedEarly);
    CHECK (early.gatePassed);
    sim.run (1.0);
    CHECK (sim.engine().gateArrivals() == 0);
}

TEST_CASE ("Simulator: the scheduler sends nothing past a gate before it is passed")
{
    // Backing notes just before and just after the right-hand gate's boundary (1.18 s).
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 40, 1.10, NoteRole::backing, 0.05),
                     note (2, 41, 1.25, NoteRole::backing, 0.05), note (3, 43, 3.0, NoteRole::backing) },
                   PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    sim.run (5.0);
    CHECK (sim.recorder().noteOns (40).size() == 1); // before the boundary: played
    CHECK (sim.recorder().noteOns (41).empty());     // after it: held back
    const double pressedAt = sim.wallSeconds();
    CHECK (sim.press (60).gatePassed);
    sim.run (3.0);
    REQUIRE (sim.recorder().noteOns (41).size() == 1);
    CHECK (sim.recorder().noteOns (41)[0].wallSeconds >= pressedAt);
    // A backing-only stretch (3 s) plays through without waiting.
    CHECK (sim.recorder().noteOns (43).size() == 1);
    CHECK (sim.engine().gateArrivals() == 1);
}

TEST_CASE ("Simulator: half tempo holds at the same song positions and judges in song time")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 62, 2.0, NoteRole::rightHand) }, PracticeMode::rightHand, 0.5);
    sim.play();
    sim.runUntilPosition (1.0);
    CHECK (sim.wallSeconds() == Approx (2.0).margin (0.01)); // 1 song second takes 2 wall seconds
    sim.run (0.2);                                           // 0.1 song s after the onset
    const auto press = sim.press (60);
    CHECK (press.verdict == PressVerdict::acceptedOnTime);
    CHECK (press.offsetMs == Approx (100.0).margin (8.0));
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.engine().positionSeconds() == Approx (2.18));
}

TEST_CASE ("Simulator: hand modes decide what the learner plays and what the app plays")
{
    const std::vector<Note> notes { note (0, 72, 1.0, NoteRole::rightHand), note (1, 48, 1.0, NoteRole::leftHand),
                                    note (2, 74, 2.0, NoteRole::rightHand) };
    SECTION ("left hand: the right hand is played by the app and never waits")
    {
        Simulator sim (notes, PracticeMode::leftHand);
        sim.play();
        REQUIRE (sim.runUntilWaiting());
        CHECK (sim.session().missingPitches() == pitches ({ 48 }));
        CHECK (sim.press (48).gatePassed);
        sim.run (3.0);
        CHECK (sim.recorder().noteOns (72).size() == 1);
        CHECK (sim.recorder().noteOns (74).size() == 1);
        CHECK (sim.engine().gateArrivals() == 1);
    }
    SECTION ("both hands: one gate per chord across the hands")
    {
        Simulator sim (notes, PracticeMode::bothHands);
        sim.play();
        REQUIRE (sim.runUntilWaiting());
        CHECK (sim.session().missingPitches() == pitches ({ 48, 72 }));
        CHECK (sim.recorder().noteOns (72).empty());
    }
}

TEST_CASE ("Simulator: the end of the song, pause and seek")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 64, 1.0, NoteRole::rightHand), note (2, 62, 2.0, NoteRole::rightHand) },
                   PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    sim.press (60);
    sim.pause();
    CHECK (sim.press (64).verdict == PressVerdict::noGate); // ignored while paused
    sim.seek (0.5);                                          // drops the half-played chord
    CHECK (sim.session().matchedPitches().empty());
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    sim.press (60);
    CHECK (sim.press (64).gatePassed);
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.press (62).gatePassed);
    sim.run (2.0);
    CHECK (sim.engine().state() == State::completed);
    CHECK (sim.press (62).verdict == PressVerdict::noGate);
}

TEST_CASE ("Simulator: a loop repeats from A, resets the matcher and waits for a note near B")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand), note (1, 62, 2.4, NoteRole::rightHand), note (2, 64, 4.0, NoteRole::rightHand) },
                   PracticeMode::rightHand);
    sim.setLoop (0.5, 2.5);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.press (60).gatePassed);
    // D at 2.4 is still pending when the position reaches B (2.5): no jump yet,
    // the song holds at 2.58 for it.
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.engine().positionSeconds() == Approx (2.58));
    CHECK (sim.loopJumps == 0);
    CHECK (sim.press (62).gatePassed);
    sim.run (0.1);
    CHECK (sim.loopJumps == 1);
    REQUIRE (sim.session().currentGate() != nullptr);
    CHECK (sim.session().currentGate()->requiredPitches == pitches ({ 60 })); // back to A's first gate
    CHECK (sim.engine().positionSeconds() < 1.0);
    // Second time round works the same.
    REQUIRE (sim.runUntilWaiting());
    CHECK (sim.engine().positionSeconds() == Approx (1.18));
}

TEST_CASE ("Simulator: a seek into a pedalled passage restores the pedal; a stop releases it")
{
    std::vector<audio::PlaybackEvent> pedal { { 0.5, 0xB1, 64, 127 }, { 3.5, 0xB1, 64, 0 } };
    Simulator sim ({ note (0, 40, 1.0, NoteRole::backing), note (1, 41, 2.0, NoteRole::backing) }, PracticeMode::rightHand, 1.0, pedal);
    sim.seek (1.5);
    sim.play();
    sim.run (0.05);
    const auto& log = sim.recorder().log;
    const bool restored = std::any_of (log.begin(), log.end(), [] (const Sounded& s) { return s.status == 0xB1 && s.data1 == 64 && s.data2 >= 64; });
    CHECK (restored);
    sim.engine().handle ({ audio::PlaybackCommand::Type::stop }, sim.recorder());
    sim.run (0.05);
    CHECK ((log.back().status == 0xB1 && log.back().data1 == 64 && log.back().data2 == 0));
}

TEST_CASE ("Simulator: losing the piano while waiting changes nothing until it plays again")
{
    Simulator sim ({ note (0, 60, 1.0, NoteRole::rightHand) }, PracticeMode::rightHand);
    sim.play();
    REQUIRE (sim.runUntilWaiting());
    sim.run (120.0); // no input at all: cable out, piano off
    CHECK (sim.engine().state() == State::waitingForGate);
    CHECK (sim.engine().positionSeconds() == Approx (1.18));
    CHECK (sim.press (60).gatePassed); // reconnected
}

TEST_CASE ("Press timing maps to song time with tempo and input delay")
{
    TransportClock waiting { true, 2.18, 1'000'000, 0.0, 0, 0.5 };
    CHECK (songTimeOfPress (waiting, 1'400'000, 0.0) == Approx (2.38));
    CHECK (songTimeOfPress (waiting, 1'400'000, 40.0) == Approx (2.36)); // 40 ms earlier, at half tempo 20 ms of song
    TransportClock playing { false, 3.0, 0, 1.0, 5'000'000, 1.0 };
    CHECK (songTimeOfPress (playing, 5'250'000, 0.0) == Approx (1.25));
}

TEST_CASE ("Count-in is one bar at the practice tempo")
{
    const auto plain = makeCountIn (120.0, 4, 4, 1.0);
    CHECK (plain.beats == 4);
    CHECK (plain.beatSeconds == Approx (0.5));
    CHECK (makeCountIn (120.0, 4, 4, 0.5).beatSeconds == Approx (1.0));
    const auto fast = makeCountIn (200.0, 6, 8, 1.0); // eighths at 200: 0.15 s, counted in pairs
    CHECK (fast.beats == 3);
    CHECK (fast.beatSeconds == Approx (0.3));
    CHECK (makeCountIn (0.0, 0, 0, 1.0).beats == 1);
}

TEST_CASE ("Loop decisions")
{
    const LoopRange loop { true, 1.0, 3.0 };
    CHECK_FALSE (loopShouldJump (loop, true, false, 2.0, 10.0, std::nullopt));
    CHECK (loopShouldJump (loop, true, false, 3.0, 10.0, std::nullopt));
    CHECK_FALSE (loopShouldJump (loop, true, false, 3.1, 10.0, 2.9)); // the learner's last note first
    CHECK (loopShouldJump (loop, true, false, 3.1, 10.0, 4.0));       // a gate after B does not hold the jump
    CHECK (loopShouldJump ({ true, 8.0, 10.0 }, false, true, 10.0, 10.0, std::nullopt)); // range up to the end
    CHECK_FALSE (loopShouldJump ({ false, 1.0, 3.0 }, true, false, 3.0, 10.0, std::nullopt));
    CHECK_FALSE (loopShouldJump ({ true, 1.0, 1.01 }, true, false, 3.0, 10.0, std::nullopt)); // too short
}
