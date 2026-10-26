#include "lesson_engine/Gate.h"

#include <algorithm>

namespace pianoassist::lesson
{

std::vector<Gate> buildGates (const timeline::Timeline& timeline, PracticeMode mode, const GateConfig& config)
{
    std::vector<Gate> gates;
    for (const auto& note : timeline.notes()) // sorted by start time
    {
        if (! isRequired (note.role, mode))
            continue;

        if (gates.empty() || note.startSeconds - gates.back().onsetSeconds > config.chordGroupingSeconds)
            gates.push_back ({ gates.size(), note.startSeconds, {}, {} });

        auto& gate = gates.back();
        gate.noteIds.push_back (note.id);
        const bool already = std::find (gate.requiredPitches.begin(), gate.requiredPitches.end(), note.pitch)
                             != gate.requiredPitches.end();
        if (! (already && config.collapseUnisonDuplicates))
            gate.requiredPitches.push_back (note.pitch);
    }
    for (auto& gate : gates)
        std::sort (gate.requiredPitches.begin(), gate.requiredPitches.end());
    return gates;
}

} // namespace pianoassist::lesson
