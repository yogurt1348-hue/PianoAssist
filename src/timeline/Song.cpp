#include "timeline/Song.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>

namespace pianoassist::timeline
{
namespace
{
constexpr std::uint8_t percussionChannel = 9; // MIDI channel 10

std::string lowercase (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(),
                    [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return text;
}

bool containsAny (const std::string& haystack, std::initializer_list<const char*> needles)
{
    return std::any_of (needles.begin(), needles.end(),
                        [&] (const char* needle) { return haystack.find (needle) != std::string::npos; });
}

bool isWord (const std::string& name, const char* word)
{
    // Matches short tokens like "rh" only as whole words.
    const std::string w (word);
    for (std::size_t pos = name.find (w); pos != std::string::npos; pos = name.find (w, pos + 1))
    {
        const bool startOk = pos == 0 || ! std::isalnum (static_cast<unsigned char> (name[pos - 1]));
        const auto end = pos + w.size();
        const bool endOk = end == name.size() || ! std::isalnum (static_cast<unsigned char> (name[end]));
        if (startOk && endOk)
            return true;
    }
    return false;
}

enum class NameHint { none, right, left, backing };

NameHint hintFromName (const std::string& rawName)
{
    const auto name = lowercase (rawName);
    if (containsAny (name, { "right", "treble", "r.h", "правая" }) || isWord (name, "rh"))
        return NameHint::right;
    if (containsAny (name, { "left", "l.h", "левая" }) || isWord (name, "lh"))
        return NameHint::left;
    if (containsAny (name, { "drum", "perc", "bass", "guitar", "string", "backing", "accomp", "pad", "choir",
                             "vocal", "voice", "synth", "brass", "sax", "violin", "cello", "flute", "organ",
                             "ударн", "бас", "гитар", "аккомп" }))
        return NameHint::backing;
    return NameHint::none;
}

bool isPianoProgram (int program)
{
    return program < 0 || program <= 7; // GM 1-8 are pianos; no program change means piano
}
} // namespace

const char* toString (Part::Assignment assignment) noexcept
{
    switch (assignment)
    {
        case Part::Assignment::rightHand:      return "right hand";
        case Part::Assignment::leftHand:       return "left hand";
        case Part::Assignment::splitAtMiddleC: return "both hands (split at C4)";
        case Part::Assignment::backing:        return "backing";
    }
    return "backing";
}

int Song::partIndexOf (const Note& note) const noexcept
{
    for (std::size_t i = 0; i < parts.size(); ++i)
        if (parts[i].track == note.trackId && parts[i].channel == note.channel)
            return static_cast<int> (i);
    return -1;
}

std::vector<Part> inferParts (const smf::SmfFile& file)
{
    std::map<std::pair<std::uint16_t, std::uint8_t>, Part> byKey;
    std::map<std::pair<std::uint16_t, std::uint8_t>, double> pitchSums;
    for (const auto& note : file.notes)
    {
        auto& part = byKey[{ note.track, note.channel }];
        part.track = note.track;
        part.channel = note.channel;
        ++part.noteCount;
        part.lowestPitch = std::min (part.lowestPitch, note.pitch);
        part.highestPitch = std::max (part.highestPitch, note.pitch);
        pitchSums[{ note.track, note.channel }] += note.pitch;
    }
    for (const auto& change : file.programChanges)
        if (auto it = byKey.find ({ change.track, change.channel }); it != byKey.end() && it->second.program < 0)
            it->second.program = change.program;

    std::vector<Part> parts;
    for (auto& [key, part] : byKey)
    {
        part.meanPitch = pitchSums[key] / static_cast<double> (part.noteCount);
        if (key.first < file.tracks.size())
            part.trackName = file.tracks[key.first].name;
        parts.push_back (part);
    }

    // 1. Explicit evidence: percussion channel, names, non-piano programs.
    std::vector<Part*> pianoCandidates;
    for (auto& part : parts)
    {
        const auto hint = hintFromName (part.trackName);
        if (part.channel == percussionChannel)
        {
            part.assignment = Part::Assignment::backing;
            part.reason = "percussion channel 10";
        }
        else if (hint == NameHint::right)
        {
            part.assignment = Part::Assignment::rightHand;
            part.reason = "track name";
        }
        else if (hint == NameHint::left)
        {
            part.assignment = Part::Assignment::leftHand;
            part.reason = "track name";
        }
        else if (hint == NameHint::backing)
        {
            part.assignment = Part::Assignment::backing;
            part.reason = "track name";
        }
        else if (! isPianoProgram (part.program))
        {
            part.assignment = Part::Assignment::backing;
            part.reason = "non-piano instrument (program " + std::to_string (part.program + 1) + ")";
        }
        else
        {
            pianoCandidates.push_back (&part);
        }
    }

    // 2. Unnamed piano parts: pitch decides.
    const bool hasNamedRight = std::any_of (parts.begin(), parts.end(),
                                            [] (const Part& p) { return p.assignment == Part::Assignment::rightHand && p.reason == "track name"; });
    const bool hasNamedLeft = std::any_of (parts.begin(), parts.end(),
                                           [] (const Part& p) { return p.assignment == Part::Assignment::leftHand && p.reason == "track name"; });

    if (pianoCandidates.size() == 1 && ! hasNamedRight && ! hasNamedLeft)
    {
        auto& part = *pianoCandidates.front();
        part.assignment = Part::Assignment::splitAtMiddleC;
        part.reason = "only piano part; split at C4";
    }
    else
    {
        std::sort (pianoCandidates.begin(), pianoCandidates.end(),
                   [] (const Part* a, const Part* b) { return a->meanPitch > b->meanPitch; });
        for (std::size_t i = 0; i < pianoCandidates.size(); ++i)
        {
            auto& part = *pianoCandidates[i];
            if (hasNamedRight && ! hasNamedLeft)
                part.assignment = Part::Assignment::leftHand;
            else if (hasNamedLeft && ! hasNamedRight)
                part.assignment = Part::Assignment::rightHand;
            else if (hasNamedLeft && hasNamedRight)
                part.assignment = Part::Assignment::backing;
            else if (i == 0)
                part.assignment = Part::Assignment::rightHand;
            else if (i == 1)
                part.assignment = Part::Assignment::leftHand;
            else
                part.assignment = Part::Assignment::backing;

            part.reason = hasNamedLeft && hasNamedRight ? "extra piano part; hands already named"
                          : (hasNamedLeft || hasNamedRight) ? "other hand is named"
                          : i < 2 ? "higher/lower average pitch"
                                  : "extra piano part";
        }
    }

    std::sort (parts.begin(), parts.end(), [] (const Part& a, const Part& b) {
        return std::tie (a.track, a.channel) < std::tie (b.track, b.channel);
    });
    return parts;
}

std::vector<double> barStartSeconds (const Song& song, std::size_t maxBars)
{
    std::vector<double> bars;
    const auto ppq = static_cast<double> (song.tempoMap.ticksPerQuarter());
    const auto endTick = song.tempoMap.secondsToTick (song.durationSeconds);

    for (std::size_t i = 0; i < song.timeSignatures.size() && bars.size() < maxBars; ++i)
    {
        const auto& ts = song.timeSignatures[i];
        const double barTicks = ppq * 4.0 * ts.numerator / ts.denominator;
        const double segmentEnd = i + 1 < song.timeSignatures.size()
                                      ? static_cast<double> (song.timeSignatures[i + 1].tick)
                                      : endTick;
        for (double tick = static_cast<double> (ts.tick); tick < segmentEnd - 0.5 && bars.size() < maxBars; tick += barTicks)
            bars.push_back (song.tempoMap.tickToSeconds (static_cast<std::int64_t> (tick + 0.5)));
    }
    return bars;
}

Result<Song> buildSong (const smf::SmfFile& file)
{
    using R = Result<Song>;

    auto tempoMap = smf::TempoMap::create (file.header.ticksPerQuarter, file.tempoChanges);
    if (! tempoMap)
        return R::fail (tempoMap.error());

    Song song;
    song.tempoMap = std::move (tempoMap).value();
    song.parts = inferParts (file);
    song.repairs = file.repairs;
    song.warnings = file.warnings;
    song.format = file.header.format;
    for (const auto& track : file.tracks)
        song.trackNames.push_back (track.name);

    if (file.timeSignatures.empty() || file.timeSignatures.front().tick > 0)
        song.timeSignatures.push_back ({ 0.0, 0, 4, 4 });
    for (const auto& ts : file.timeSignatures)
        song.timeSignatures.push_back ({ song.tempoMap.tickToSeconds (ts.tick), ts.tick, ts.numerator, ts.denominator });

    std::vector<Note> notes;
    notes.reserve (file.notes.size());
    NoteId nextId = 0;
    for (const auto& source : file.notes)
    {
        Note note;
        note.id = nextId++;
        note.trackId = source.track;
        note.channel = source.channel;
        note.pitch = source.pitch;
        note.velocity = source.velocity;
        note.startTick = source.startTick;
        note.endTick = source.endTick;
        note.startSeconds = song.tempoMap.tickToSeconds (source.startTick);
        note.endSeconds = song.tempoMap.tickToSeconds (source.endTick);

        const auto part = std::find_if (song.parts.begin(), song.parts.end(), [&] (const Part& p) {
            return p.track == source.track && p.channel == source.channel;
        });
        switch (part->assignment)
        {
            case Part::Assignment::rightHand:      note.role = NoteRole::rightHand; break;
            case Part::Assignment::leftHand:       note.role = NoteRole::leftHand; break;
            case Part::Assignment::backing:        note.role = NoteRole::backing; break;
            case Part::Assignment::splitAtMiddleC:
                note.role = note.pitch < handSplitPitch ? NoteRole::leftHand : NoteRole::rightHand;
                break;
        }
        notes.push_back (note);
    }

    auto timeline = Timeline::create (std::move (notes));
    if (! timeline)
        return R::fail (timeline.error());
    song.timeline = std::move (timeline).value();
    song.durationSeconds = std::max (song.timeline.durationSeconds(), song.tempoMap.tickToSeconds (file.endTick));
    return R::ok (std::move (song));
}

} // namespace pianoassist::timeline
