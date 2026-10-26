#pragma once

#include "core/Result.h"
#include "timeline/Note.h"

#include <span>
#include <vector>

namespace pianoassist::timeline
{

/** Immutable, start-time-ordered note collection. Built once on the control
    thread and then shared read-only, so queries need no locking. */
class Timeline
{
public:
    Timeline() = default;

    /** Validates pitch/channel ranges and note ordering (end >= start), then
        sorts by start time, pitch and id so ordering is deterministic. */
    static Result<Timeline> create (std::vector<Note> notes);

    std::span<const Note> notes() const noexcept { return notes_; }
    bool empty() const noexcept { return notes_.empty(); }
    double durationSeconds() const noexcept { return durationSeconds_; }

    /** Notes whose sounding interval overlaps [fromSeconds, toSeconds). */
    std::vector<Note> notesOverlapping (double fromSeconds, double toSeconds) const;

private:
    std::vector<Note> notes_;
    double durationSeconds_ = 0.0;
    double longestNoteSeconds_ = 0.0;
};

} // namespace pianoassist::timeline
