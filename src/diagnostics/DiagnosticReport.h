#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pianoassist::diagnostics
{

/** What "Copy diagnostics" and "Save diagnostics" put at the top of the
    report. Device names are driver display names; the song is a file name
    without its folder. */
struct DiagnosticSummary
{
    std::string appVersion;
    std::string build;
    std::string os;
    std::string audio;
    std::vector<std::string> midiInputs;
    std::string selectedMidiInput;
    std::string song;
    std::string practice;
};

/** Replaces each sensitive text (for example the user's profile folder) with
    its placeholder, in plain and in JSON-escaped (doubled backslash) form, so
    a report never carries the Windows user name. Longest texts first. */
std::string redact (std::string text, std::vector<std::pair<std::string, std::string>> replacements);

/** The last maxLines lines of a text file, oldest first; empty when the file
    cannot be read. Reads at most maxBytes from the end. */
std::vector<std::string> tailLines (const std::filesystem::path& path, std::size_t maxLines,
                                    std::size_t maxBytes = 8u * 1024u * 1024u);

/** Plain-text report: summary, then the given log lines. Nothing leaves the
    computer unless the user pastes or sends it. */
std::string buildReport (const DiagnosticSummary& summary, const std::vector<std::string>& logLines);

} // namespace pianoassist::diagnostics
