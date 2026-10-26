#include "timeline/Timeline.h"

#include <catch2/catch_test_macros.hpp>

using namespace pianoassist::timeline;

namespace
{
Note note (NoteId id, std::uint8_t pitch, double start, double end)
{
    Note n;
    n.id = id;
    n.pitch = pitch;
    n.startSeconds = start;
    n.endSeconds = end;
    n.startTick = static_cast<std::int64_t> (start * 960);
    n.endTick = static_cast<std::int64_t> (end * 960);
    return n;
}

std::vector<NoteId> ids (const std::vector<Note>& notes)
{
    std::vector<NoteId> result;
    for (const auto& n : notes)
        result.push_back (n.id);
    return result;
}
} // namespace

TEST_CASE ("Timeline orders notes deterministically")
{
    const auto timeline = Timeline::create ({ note (3, 64, 1.0, 2.0), note (1, 67, 0.0, 1.0), note (2, 60, 0.0, 1.0) });
    REQUIRE (timeline.isOk());
    const auto notes = timeline.value().notes();
    REQUIRE (notes.size() == 3);
    CHECK (notes[0].id == 2);
    CHECK (notes[1].id == 1);
    CHECK (notes[2].id == 3);
    CHECK (timeline.value().durationSeconds() == 2.0);
}

TEST_CASE ("Overlap query includes notes still sounding from earlier")
{
    const auto timeline = Timeline::create ({ note (1, 48, 0.0, 10.0), note (2, 60, 1.0, 1.5), note (3, 62, 2.0, 2.5),
                                              note (4, 64, 5.0, 5.5) })
                              .value();
    CHECK (ids (timeline.notesOverlapping (2.0, 3.0)) == std::vector<NoteId> { 1, 3 });
    CHECK (ids (timeline.notesOverlapping (1.5, 2.0)).empty() == false);
    CHECK (ids (timeline.notesOverlapping (1.5, 2.0)) == std::vector<NoteId> { 1 });
    CHECK (timeline.notesOverlapping (3.0, 3.0).empty());
}

TEST_CASE ("Timeline rejects invalid notes")
{
    CHECK_FALSE (Timeline::create ({ note (1, 128, 0.0, 1.0) }).isOk());
    CHECK_FALSE (Timeline::create ({ note (1, 60, 2.0, 1.0) }).isOk());
}

TEST_CASE ("Role names are stable for diagnostics")
{
    CHECK (toString (NoteRole::rightHand) == "rightHand");
    CHECK (toString (NoteRole::backing) == "backing");
}
