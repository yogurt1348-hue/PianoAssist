#pragma once

#include "lesson_engine/PracticeMode.h"
#include "timeline/Timeline.h"

#include <cstdint>
#include <vector>

namespace pianoassist::lesson
{

struct GateConfig
{
    /** Required notes starting within this span of the first one form one
        chord gate. */
    double chordGroupingSeconds = 0.030;

    /** The same pitch twice in one chord (e.g. doubled by both hands) is
        required once: a key cannot be struck twice at the same instant. */
    bool collapseUnisonDuplicates = true;
};

/** One point where the transport waits for the learner. */
struct Gate
{
    std::size_t index = 0;
    double onsetSeconds = 0.0;
    std::vector<std::uint8_t> requiredPitches; ///< sorted; repeats mean multiplicity
    std::vector<timeline::NoteId> noteIds;
};

/** Gates for the notes the mode requires, in time order. Spans without
    required notes produce no gate, so the transport plays through them. */
std::vector<Gate> buildGates (const timeline::Timeline& timeline, PracticeMode mode, const GateConfig& config = {});

} // namespace pianoassist::lesson
