#pragma once

#include "core/Result.h"
#include "diagnostics/RotatingLog.h"
#include "timeline/Song.h"
#include "ui/PianoRollView.h"

#include <filesystem>
#include <memory>

namespace pianoassist::app
{

/** Reads, parses and normalizes a MIDI file, logging every step and the
    inferred part mapping. Logs carry the file name only, never the path. */
Result<std::shared_ptr<const timeline::Song>> loadSong (const std::filesystem::path& path,
                                                         diagnostics::RotatingLog& log);

ui::PianoRollView makePianoRollView (const timeline::Song& song, const std::string& title);

} // namespace pianoassist::app
