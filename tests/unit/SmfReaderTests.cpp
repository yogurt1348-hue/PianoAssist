#include "Fixtures.h"
#include "smf/SmfReader.h"
#include "timeline/Song.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace pianoassist;
using namespace pianoassist::smf;
using namespace pianoassist::timeline;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace
{
SmfFile readFixtureSmf (std::string_view name)
{
    auto file = readSmf (test::readFixture (name));
    INFO (file.error());
    REQUIRE (file.isOk());
    return std::move (file).value();
}

Song songFrom (std::string_view name)
{
    auto song = buildSong (readFixtureSmf (name));
    INFO (song.error());
    REQUIRE (song.isOk());
    return std::move (song).value();
}

const Part& partOn (const Song& song, std::uint16_t track, std::uint8_t channel)
{
    for (const auto& part : song.parts)
        if (part.track == track && part.channel == channel)
            return part;
    FAIL ("part not found");
    return song.parts.front();
}
} // namespace

TEST_CASE ("Format 0 scale normalizes to eight quarter notes at 120 BPM")
{
    const auto file = readFixtureSmf ("format0_c_major_scale_120bpm.mid");
    REQUIRE (file.notes.size() == 8);
    CHECK (file.repairs.empty());
    const std::uint8_t expected[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    for (std::size_t i = 0; i < 8; ++i)
    {
        CHECK (file.notes[i].pitch == expected[i]);
        CHECK (file.notes[i].startTick == static_cast<std::int64_t> (i * 480));
        CHECK (file.notes[i].endTick == static_cast<std::int64_t> ((i + 1) * 480));
        CHECK (file.notes[i].velocity == 90);
    }

    const auto song = songFrom ("format0_c_major_scale_120bpm.mid");
    CHECK (song.durationSeconds == Approx (4.0));
    const auto notes = song.timeline.notes();
    CHECK (notes[3].startSeconds == Approx (1.5));
    CHECK (notes[3].endSeconds == Approx (2.0));
    // The only piano part is split at C4: every scale note is right hand.
    CHECK (song.parts.size() == 1);
    CHECK (song.parts[0].assignment == Part::Assignment::splitAtMiddleC);
    for (const auto& note : notes)
        CHECK (note.role == NoteRole::rightHand);
}

TEST_CASE ("Tempo changes and named hands map to seconds and roles")
{
    const auto song = songFrom ("format1_tempo_changes.mid");
    REQUIRE (song.trackNames.size() == 3);
    CHECK (song.trackNames[1] == "Right Hand");
    REQUIRE (song.timeSignatures.size() == 1);
    CHECK (song.timeSignatures[0].numerator == 4);
    CHECK (song.timeSignatures[0].denominator == 4);

    CHECK (partOn (song, 1, 0).assignment == Part::Assignment::rightHand);
    CHECK (partOn (song, 2, 1).assignment == Part::Assignment::leftHand);

    // Right hand: 8 quarters; bar 1 at 120 BPM (0.5 s each), bar 2 at 60 BPM (1 s each).
    std::vector<double> rightStarts;
    for (const auto& note : song.timeline.notes())
        if (note.role == NoteRole::rightHand)
            rightStarts.push_back (note.startSeconds);
    REQUIRE (rightStarts.size() == 8);
    CHECK (rightStarts[3] == Approx (1.5));
    CHECK (rightStarts[4] == Approx (2.0));
    CHECK (rightStarts[5] == Approx (3.0));
    CHECK (rightStarts[7] == Approx (5.0));
}

TEST_CASE ("Band file: drums, named bass and non-piano programs are backing")
{
    const auto song = songFrom ("format1_band_roles.mid");
    CHECK (partOn (song, 1, 0).assignment == Part::Assignment::splitAtMiddleC);
    CHECK (partOn (song, 2, 9).assignment == Part::Assignment::backing);
    CHECK (partOn (song, 3, 1).assignment == Part::Assignment::backing);
    CHECK (partOn (song, 3, 1).reason == "track name");
    CHECK (partOn (song, 4, 2).assignment == Part::Assignment::backing);
    CHECK (partOn (song, 4, 2).program == 48);

    int left = 0, right = 0, backing = 0;
    for (const auto& note : song.timeline.notes())
    {
        if (note.role == NoteRole::leftHand) ++left;
        if (note.role == NoteRole::rightHand) ++right;
        if (note.role == NoteRole::backing) ++backing;
    }
    CHECK (left == 1);  // C3 below the split
    CHECK (right == 1); // E4
    CHECK (backing == 3);
}

TEST_CASE ("Two unnamed piano parts are split by average pitch")
{
    SmfFile file;
    file.header = { 1, 2, 480 };
    file.tracks = { { 0, "", 960 }, { 1, "", 960 } };
    file.notes = { { 0, 0, 40, 80, 0, 480 }, { 1, 0, 76, 80, 0, 480 } };
    const auto parts = inferParts (file);
    REQUIRE (parts.size() == 2);
    CHECK (parts[0].assignment == Part::Assignment::leftHand);
    CHECK (parts[1].assignment == Part::Assignment::rightHand);
}

TEST_CASE ("Parser edge cases: running status, overlapping repeats, missing note-off")
{
    const auto file = readFixtureSmf ("format0_parser_edge_cases.mid");

    std::vector<SmfNote> sixty;
    std::optional<SmfNote> sixtyFour;
    for (const auto& note : file.notes)
    {
        if (note.pitch == 60) sixty.push_back (note);
        if (note.pitch == 64) sixtyFour = note;
    }

    // Two overlapping C4s pair first-on-first-off; running status kept velocity 90.
    REQUIRE (sixty.size() == 2);
    CHECK (sixty[0].startTick == 0);
    CHECK (sixty[1].startTick == 0);
    const auto [first, second] = sixty[0].velocity == 100 ? std::pair { sixty[0], sixty[1] } : std::pair { sixty[1], sixty[0] };
    CHECK (first.velocity == 100);
    CHECK (first.endTick == 240);
    CHECK (second.velocity == 90);
    CHECK (second.endTick == 480);

    // Missing note-off closed at track end and reported.
    REQUIRE (sixtyFour.has_value());
    CHECK (sixtyFour->startTick == 480);
    CHECK (sixtyFour->endTick == 960);
    REQUIRE (file.repairs.size() == 1);
    CHECK_THAT (file.repairs.front(), ContainsSubstring ("without a note-off"));

    REQUIRE (file.sustainEvents.size() == 2);
    CHECK (file.sustainEvents[0].value == 127);
    CHECK (file.sustainEvents[1].tick == 960);
}

TEST_CASE ("Malformed track data fails safely")
{
    const auto make = [] (std::vector<std::uint8_t> trackData) {
        std::vector<std::uint8_t> bytes { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k' };
        const auto size = static_cast<std::uint32_t> (trackData.size());
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes.push_back (static_cast<std::uint8_t> (size >> shift));
        bytes.insert (bytes.end(), trackData.begin(), trackData.end());
        return readSmf (bytes);
    };

    CHECK_FALSE (make ({ 0x00, 0x3C, 0x40 }).isOk());                   // data without status
    CHECK_FALSE (make ({ 0x00, 0x90, 0x3C }).isOk());                   // truncated note-on
    CHECK_FALSE (make ({ 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x90 }).isOk()); // 5-byte delta
    CHECK_FALSE (make ({ 0x00, 0xFF, 0x51, 0x03, 0x07 }).isOk());       // truncated tempo
    CHECK_FALSE (make ({ 0x00, 0xFF, 0x51, 0x03, 0, 0, 0 }).isOk());    // zero tempo
    CHECK_FALSE (make ({ 0x00, 0xF0, 0x7F }).isOk());                   // sysex past end
    CHECK (make ({ 0x00, 0xFF, 0x2F, 0x00 }).isOk());                   // empty track is fine
    CHECK (make ({ 0x00, 0xFF, 0x2F, 0x00 }).value().warnings.size() == 1);
}

TEST_CASE ("Format 2 and truncated files are rejected by the full parser")
{
    CHECK_FALSE (readSmf (test::readFixture ("unsupported_format2.mid")).isOk());
    CHECK_FALSE (readSmf (test::readFixture ("malformed_truncated_track.mid")).isOk());
}
