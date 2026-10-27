#include "app/SongLoader.h"

#include "core/BinaryFile.h"
#include "core/NoteName.h"
#include "core/Utf8Path.h"
#include "smf/SmfReader.h"

#include <chrono>
#include <cmath>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;

std::string formatTime (double seconds)
{
    const auto total = static_cast<int> (std::lround (seconds));
    const auto secs = total % 60;
    return std::to_string (total / 60) + ":" + (secs < 10 ? "0" : "") + std::to_string (secs);
}

ui::RollRole toRollRole (timeline::NoteRole role)
{
    switch (role)
    {
        case timeline::NoteRole::rightHand: return ui::RollRole::rightHand;
        case timeline::NoteRole::leftHand:  return ui::RollRole::leftHand;
        case timeline::NoteRole::backing:   return ui::RollRole::backing;
        case timeline::NoteRole::unknown:   break;
    }
    return ui::RollRole::unknown;
}

ui::RollRole partRollRole (timeline::Part::Assignment assignment)
{
    switch (assignment)
    {
        case timeline::Part::Assignment::rightHand:      return ui::RollRole::rightHand;
        case timeline::Part::Assignment::leftHand:       return ui::RollRole::leftHand;
        case timeline::Part::Assignment::backing:        return ui::RollRole::backing;
        case timeline::Part::Assignment::splitAtMiddleC: return ui::RollRole::unknown;
    }
    return ui::RollRole::unknown;
}

std::string partLabel (const timeline::Part& part)
{
    std::string label = "Track " + std::to_string (part.track + 1);
    if (! part.trackName.empty())
        label += " '" + part.trackName + "'";
    return label + " · ch " + std::to_string (part.channel + 1);
}

std::string roleText (timeline::Part::Assignment assignment)
{
    switch (assignment)
    {
        case timeline::Part::Assignment::rightHand:      return "Right hand";
        case timeline::Part::Assignment::leftHand:       return "Left hand";
        case timeline::Part::Assignment::splitAtMiddleC: return "Both hands";
        case timeline::Part::Assignment::backing:        return "Backing";
    }
    return "Backing";
}
} // namespace

Result<std::shared_ptr<const timeline::Song>> loadSong (const std::filesystem::path& path, diagnostics::RotatingLog& log)
{
    using R = Result<std::shared_ptr<const timeline::Song>>;
    const auto name = fileNameForLog (path);
    const auto started = std::chrono::steady_clock::now();
    log.write (LogLevel::info, "song", "Opening MIDI file", { { "file", name } });

    auto bytes = readBinaryFile (path, smf::maxSmfFileBytes);
    if (! bytes)
    {
        log.write (LogLevel::error, "song", "Could not read MIDI file", { { "file", name }, { "error", bytes.error() } });
        return R::fail (bytes.error());
    }

    auto file = smf::readSmf (bytes.value());
    if (! file)
    {
        log.write (LogLevel::error, "song", "MIDI file rejected", { { "file", name }, { "error", file.error() } });
        return R::fail (file.error());
    }

    auto song = timeline::buildSong (file.value());
    if (! song)
    {
        log.write (LogLevel::error, "song", "Could not build song", { { "file", name }, { "error", song.error() } });
        return R::fail (song.error());
    }

    const auto& s = song.value();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count();
    log.write (LogLevel::info, "song", "MIDI file loaded",
               { { "file", name },
                 { "bytes", std::to_string (bytes.value().size()) },
                 { "format", std::to_string (s.format) },
                 { "tracks", std::to_string (s.trackNames.size()) },
                 { "ppq", std::to_string (s.tempoMap.ticksPerQuarter()) },
                 { "tempoChanges", std::to_string (s.tempoMap.changes().size()) },
                 { "startBpm", std::to_string (static_cast<int> (std::lround (s.tempoMap.tempoBpmAt (0)))) },
                 { "timeSignatures", std::to_string (s.timeSignatures.size()) },
                 { "notes", std::to_string (s.timeline.notes().size()) },
                 { "duration", formatTime (s.durationSeconds) },
                 { "loadMs", std::to_string (ms) } });

    for (const auto& part : s.parts)
        log.write (LogLevel::info, "song", "Part mapping",
                   { { "part", partLabel (part) },
                     { "assignment", timeline::toString (part.assignment) },
                     { "reason", part.reason },
                     { "program", part.program < 0 ? "none" : std::to_string (part.program + 1) },
                     { "notes", std::to_string (part.noteCount) },
                     { "range", noteName (part.lowestPitch) + "-" + noteName (part.highestPitch) } });
    for (const auto& repair : s.repairs)
        log.write (LogLevel::warning, "song", "Repair applied", { { "detail", repair } });
    for (const auto& warning : s.warnings)
        log.write (LogLevel::warning, "song", "Song warning", { { "detail", warning } });

    return R::ok (std::make_shared<const timeline::Song> (std::move (song).value()));
}

ui::PianoRollView makePianoRollView (const timeline::Song& song, const std::string& title)
{
    ui::PianoRollView view;
    view.title = title;
    view.durationSeconds = song.durationSeconds;
    view.barStarts = timeline::barStartSeconds (song);
    view.notices = song.repairs;
    view.notices.insert (view.notices.end(), song.warnings.begin(), song.warnings.end());

    const auto& ts = song.timeSignatures.front();
    view.summary = "Format " + std::to_string (song.format) + " · " + std::to_string (song.trackNames.size())
                   + " tracks · " + std::to_string (song.timeline.notes().size()) + " notes · "
                   + formatTime (song.durationSeconds) + " · "
                   + std::to_string (static_cast<int> (std::lround (song.tempoMap.tempoBpmAt (0)))) + " BPM"
                   + (song.tempoMap.changes().size() > 1 ? " (tempo changes)" : "") + " · "
                   + std::to_string (ts.numerator) + "/" + std::to_string (ts.denominator);

    view.lowestPitch = 127;
    view.highestPitch = 0;
    for (const auto& note : song.timeline.notes())
    {
        view.notes.push_back ({ note.startSeconds, note.endSeconds, note.pitch, toRollRole (note.role),
                                song.partIndexOf (note) });
        view.lowestPitch = std::min (view.lowestPitch, note.pitch);
        view.highestPitch = std::max (view.highestPitch, note.pitch);
    }
    if (view.notes.empty())
    {
        view.lowestPitch = 48;
        view.highestPitch = 84;
    }

    for (const auto& part : song.parts)
        view.parts.push_back ({ partLabel (part),
                                std::to_string (part.noteCount) + (part.noteCount == 1 ? " note · " : " notes · ") + noteName (part.lowestPitch) + "-"
                                    + noteName (part.highestPitch) + " · " + part.reason,
                                partRollRole (part.assignment),
                                roleText (part.assignment),
                                static_cast<int> (part.assignment),
                                part.guessedFromPitch });
    view.needsPartCheck = timeline::needsPartCheck (song);
    return view;
}

} // namespace pianoassist::app
