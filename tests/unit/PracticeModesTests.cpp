// Phase 4: the three practice modes on fixtures with split hands, crossed
// hands, a backing-only intro and no backing; accompaniment choices; manual
// part mapping.
#include "Fixtures.h"
#include "lesson_engine/Gate.h"
#include "lesson_engine/PracticeSession.h"
#include "smf/SmfReader.h"
#include "timeline/Song.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace pianoassist;
using namespace pianoassist::lesson;
using timeline::NoteRole;
using timeline::Part;
using Catch::Approx;

namespace
{
timeline::Song songOf (std::string_view fixture)
{
    auto file = smf::readSmf (test::readFixture (fixture));
    REQUIRE (file);
    auto song = timeline::buildSong (file.value());
    REQUIRE (song);
    return std::move (song).value();
}

std::vector<double> onsets (const std::vector<Gate>& gates)
{
    std::vector<double> result;
    for (const auto& g : gates)
        result.push_back (g.onsetSeconds);
    return result;
}

std::size_t countRole (const timeline::Song& song, NoteRole role)
{
    const auto notes = song.timeline.notes();
    return static_cast<std::size_t> (std::count_if (notes.begin(), notes.end(), [role] (const auto& n) { return n.role == role; }));
}
} // namespace

TEST_CASE ("Split hands: two unnamed piano parts are told apart by pitch and flagged for a check")
{
    const auto song = songOf ("practice_split_hands.mid");
    CHECK (timeline::needsPartCheck (song));
    CHECK (countRole (song, NoteRole::rightHand) == 4);
    CHECK (countRole (song, NoteRole::leftHand) == 4);

    CHECK (buildGates (song.timeline, PracticeMode::rightHand).size() == 4);
    CHECK (buildGates (song.timeline, PracticeMode::leftHand).size() == 2);   // two chords
    CHECK (buildGates (song.timeline, PracticeMode::bothHands).size() == 4);  // chords coincide with melody
    const auto both = buildGates (song.timeline, PracticeMode::bothHands);
    CHECK (both[0].requiredPitches == std::vector<std::uint8_t> { 48, 55, 72 });
}

TEST_CASE ("Crossed hands follow the named tracks, not pitch")
{
    const auto song = songOf ("practice_crossed_hands.mid");
    CHECK_FALSE (timeline::needsPartCheck (song));

    const auto right = buildGates (song.timeline, PracticeMode::rightHand);
    REQUIRE (right.size() == 4);
    CHECK (right[2].requiredPitches == std::vector<std::uint8_t> { 50 }); // D3, below the left hand
    const auto left = buildGates (song.timeline, PracticeMode::leftHand);
    REQUIRE (left.size() == 4);
    CHECK (left[2].requiredPitches == std::vector<std::uint8_t> { 62 });
}

TEST_CASE ("Backing-only intro produces no gates until the piano enters")
{
    const auto song = songOf ("practice_backing_intro.mid");
    for (const auto mode : { PracticeMode::bothHands, PracticeMode::rightHand, PracticeMode::leftHand })
    {
        const auto gates = buildGates (song.timeline, mode);
        REQUIRE (gates.size() == 2);
        CHECK (gates.front().onsetSeconds == Approx (4.0)); // two bars of bass first
        CHECK (onsets (gates) == std::vector<double> { 4.0, 5.0 });
    }

    // The session starting from 0 waits for the first piano note, not earlier.
    PracticeSession session (buildGates (song.timeline, PracticeMode::bothHands), {});
    session.resetTo (0.0);
    REQUIRE (session.currentGate() != nullptr);
    CHECK (session.currentGate()->onsetSeconds == Approx (4.0));
    CHECK (*session.nextBoundarySeconds() == Approx (4.0 + PracticeProfile {}.onsetLateMs / 1000.0));
}

TEST_CASE ("No backing: one piano part, all three modes")
{
    const auto song = songOf ("practice_no_backing.mid");
    CHECK (countRole (song, NoteRole::backing) == 0);
    for (const auto mode : { PracticeMode::bothHands, PracticeMode::rightHand, PracticeMode::leftHand })
        CHECK (buildGates (song.timeline, mode).size() == 3);
    CHECK (buildGates (song.timeline, PracticeMode::rightHand)[2].requiredPitches == std::vector<std::uint8_t> { 72 });
    CHECK (buildGates (song.timeline, PracticeMode::leftHand)[2].requiredPitches == std::vector<std::uint8_t> { 55 });
}

TEST_CASE ("What the app plays: required hands never, other hand and backing by choice")
{
    const Accompaniment all;
    const Accompaniment soloHand { false, true };
    const Accompaniment noBacking { true, false };

    // Not practising: everything, except backing when it is muted.
    CHECK (isPlayedByApp (NoteRole::rightHand, false, PracticeMode::rightHand, soloHand));
    CHECK (isPlayedByApp (NoteRole::backing, false, PracticeMode::bothHands, all));
    CHECK_FALSE (isPlayedByApp (NoteRole::backing, false, PracticeMode::bothHands, noBacking));

    // Practising: required hands are the learner's.
    CHECK_FALSE (isPlayedByApp (NoteRole::rightHand, true, PracticeMode::bothHands, all));
    CHECK_FALSE (isPlayedByApp (NoteRole::leftHand, true, PracticeMode::bothHands, all));
    CHECK_FALSE (isPlayedByApp (NoteRole::rightHand, true, PracticeMode::rightHand, all));

    // "Play other piano part" is independent from backing.
    CHECK (isPlayedByApp (NoteRole::leftHand, true, PracticeMode::rightHand, all));
    CHECK_FALSE (isPlayedByApp (NoteRole::leftHand, true, PracticeMode::rightHand, soloHand));
    CHECK (isPlayedByApp (NoteRole::backing, true, PracticeMode::rightHand, soloHand));
    CHECK (isPlayedByApp (NoteRole::rightHand, true, PracticeMode::leftHand, noBacking));
    CHECK_FALSE (isPlayedByApp (NoteRole::backing, true, PracticeMode::leftHand, noBacking));

    // Unknown is never required and follows the backing switch.
    CHECK (isPlayedByApp (NoteRole::unknown, true, PracticeMode::bothHands, all));
}

TEST_CASE ("Manual mapping reassigns a part and its notes")
{
    const auto song = songOf ("practice_split_hands.mid");
    const auto high = static_cast<std::size_t> (std::find_if (song.parts.begin(), song.parts.end(),
                                                              [] (const Part& p) { return p.assignment == Part::Assignment::rightHand; })
                                                - song.parts.begin());
    REQUIRE (high < song.parts.size());

    auto mapped = timeline::reassignPart (song, high, Part::Assignment::backing);
    REQUIRE (mapped);
    const auto& s = mapped.value();
    CHECK (s.parts[high].setByUser);
    CHECK (s.parts[high].reason == "set by you");
    CHECK (countRole (s, NoteRole::rightHand) == 0);
    CHECK (countRole (s, NoteRole::backing) == 4);
    CHECK (buildGates (s.timeline, PracticeMode::rightHand).empty());
    CHECK (s.timeline.notes().size() == song.timeline.notes().size());

    // Split of a single part by pitch, then back to one hand.
    auto split = timeline::reassignPart (s, high, Part::Assignment::splitAtMiddleC);
    REQUIRE (split);
    CHECK (countRole (split.value(), NoteRole::rightHand) == 4); // all above C4

    CHECK_FALSE (timeline::reassignPart (song, song.parts.size(), Part::Assignment::backing));
}

TEST_CASE ("Switching mode resets gate state predictably")
{
    const auto song = songOf ("practice_split_hands.mid");
    PracticeSession right (buildGates (song.timeline, PracticeMode::rightHand), {});
    right.resetTo (0.0);
    CHECK (right.press (72, 0.0).gatePassed);
    CHECK (right.passedGateCount() == 1);

    // A new session for the new mode, reset to the same position, starts at
    // the first gate at or after it with nothing matched.
    PracticeSession left (buildGates (song.timeline, PracticeMode::leftHand), {});
    left.resetTo (0.4);
    REQUIRE (left.currentGate() != nullptr);
    CHECK (left.currentGate()->onsetSeconds == Approx (1.0));
    CHECK (left.matchedPitches().empty());
    CHECK (left.passedGateCount() == 0);
}
