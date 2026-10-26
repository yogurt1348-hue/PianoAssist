#include "ReferencePerformance.h"

#include <algorithm>

namespace cadenza::adaptive
{
namespace
{
constexpr int kGmDrumChannel = 9;
}

ReferencePerformance ReferencePerformance::build(const MidiFileData& file, const ReferenceBuildOptions& options,
                                                 std::string* error)
{
    const TempoMap tempoMap = TempoMap::fromMidiFile(file);
    std::vector<ReferenceNote> notes;

    std::vector<int> tracks = options.tracks;
    if (tracks.empty())
        for (int t = 0; t < static_cast<int>(file.tracks.size()); ++t) tracks.push_back(t);

    for (int t : tracks) {
        if (t < 0 || t >= static_cast<int>(file.tracks.size())) {
            if (error != nullptr) *error = "reference track " + std::to_string(t) + " does not exist";
            return ReferencePerformance { };
        }
        for (const auto& n : file.tracks[static_cast<std::size_t>(t)].notes) {
            ReferenceNote r;
            r.pitch = n.pitch;
            r.onTick = n.onTick;
            r.offTick = n.offTick;
            r.velocity = n.velocity;
            r.channel = n.channel;
            r.track = t;
            notes.push_back(r);
        }
    }

    auto perf = fromNotes(tempoMap, std::move(notes), options);
    if (perf.empty() && error != nullptr)
        *error = "the selected reference track(s) contain no playable notes";
    return perf;
}

ReferencePerformance ReferencePerformance::fromNotes(const TempoMap& tempoMap, std::vector<ReferenceNote> notes,
                                                     const ReferenceBuildOptions& options)
{
    ReferencePerformance perf;
    perf.m_tempoMap = tempoMap;

    for (auto& n : notes) {
        if (n.pitch < 0 || n.pitch > 127 || n.onTick < 0) continue;
        if (options.channelFilter >= 0 && n.channel != options.channelFilter) continue;
        if (options.excludeDrumChannel && n.channel == kGmDrumChannel && options.channelFilter != kGmDrumChannel)
            continue;
        n.offTick = std::max(n.offTick, n.onTick + 1);
        n.onSec = tempoMap.ticksToSeconds(static_cast<double>(n.onTick));
        perf.m_notes.push_back(n);
        perf.m_endTick = std::max(perf.m_endTick, n.offTick);
    }
    std::stable_sort(perf.m_notes.begin(), perf.m_notes.end(), [](const ReferenceNote& a, const ReferenceNote& b) {
        if (a.onTick != b.onTick) return a.onTick < b.onTick;
        return a.pitch < b.pitch;
    });
    perf.buildGroups(options);
    return perf;
}

void ReferencePerformance::buildGroups(const ReferenceBuildOptions& options)
{
    m_groups.clear();
    const bool useTicks = options.chordGroupingWindowTicks >= 0;
    const double windowSec = std::max(0.0, options.chordGroupingWindowMs) / 1000.0;

    for (int i = 0; i < static_cast<int>(m_notes.size()); ++i) {
        const auto& n = m_notes[static_cast<std::size_t>(i)];
        bool joins = false;
        if (!m_groups.empty()) {
            const auto& g = m_groups.back();
            joins = useTicks ? (n.onTick - g.tick) <= options.chordGroupingWindowTicks
                             : (n.onSec - g.nominalSec) <= windowSec + 1e-12;
        }
        if (!joins) {
            NoteGroup g;
            g.tick = n.onTick;
            g.lastOnsetTick = n.onTick;
            g.nominalSec = n.onSec;
            g.firstNote = i;
            m_groups.push_back(g);
        }
        auto& g = m_groups.back();
        g.pitches.set(n.pitch);
        g.lastOnsetTick = n.onTick;
        g.noteCount += 1;
        g.maxVelocity = std::max(g.maxVelocity, n.velocity);
    }
}

int ReferencePerformance::groupAtOrAfter(double tick) const noexcept
{
    auto it = std::lower_bound(m_groups.begin(), m_groups.end(), tick,
                               [](const NoteGroup& g, double t) { return static_cast<double>(g.tick) < t; });
    return static_cast<int>(std::distance(m_groups.begin(), it));
}

int ReferencePerformance::groupAtOrBefore(double tick) const noexcept
{
    auto it = std::upper_bound(m_groups.begin(), m_groups.end(), tick,
                               [](double t, const NoteGroup& g) { return t < static_cast<double>(g.tick); });
    return static_cast<int>(std::distance(m_groups.begin(), it)) - 1;
}
}
