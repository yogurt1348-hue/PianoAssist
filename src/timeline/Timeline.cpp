#include "timeline/Timeline.h"

#include <algorithm>
#include <string>
#include <tuple>

namespace pianoassist::timeline
{

Result<Timeline> Timeline::create (std::vector<Note> notes)
{
    using R = Result<Timeline>;
    for (const auto& note : notes)
    {
        if (note.pitch > 127 || note.velocity > 127 || note.channel > 15)
            return R::fail ("Note " + std::to_string (note.id) + " has an out-of-range MIDI value.");
        if (note.endTick < note.startTick || note.endSeconds < note.startSeconds)
            return R::fail ("Note " + std::to_string (note.id) + " ends before it starts.");
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) {
        return std::tie (a.startSeconds, a.pitch, a.id) < std::tie (b.startSeconds, b.pitch, b.id);
    });

    Timeline timeline;
    for (const auto& note : notes)
    {
        timeline.durationSeconds_ = std::max (timeline.durationSeconds_, note.endSeconds);
        timeline.longestNoteSeconds_ = std::max (timeline.longestNoteSeconds_, note.endSeconds - note.startSeconds);
    }
    timeline.notes_ = std::move (notes);
    return R::ok (std::move (timeline));
}

std::vector<Note> Timeline::notesOverlapping (double fromSeconds, double toSeconds) const
{
    std::vector<Note> result;
    if (toSeconds <= fromSeconds)
        return result;

    // A note starting before fromSeconds can only overlap if it started within
    // the longest note duration, which bounds the backwards scan.
    const auto earliestStart = fromSeconds - longestNoteSeconds_;
    auto it = std::lower_bound (notes_.begin(), notes_.end(), earliestStart,
                                [] (const Note& n, double t) { return n.startSeconds < t; });
    for (; it != notes_.end() && it->startSeconds < toSeconds; ++it)
        if (it->endSeconds > fromSeconds || it->startSeconds >= fromSeconds)
            result.push_back (*it);
    return result;
}

} // namespace pianoassist::timeline
