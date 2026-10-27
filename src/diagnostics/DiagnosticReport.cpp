#include "diagnostics/DiagnosticReport.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pianoassist::diagnostics
{
namespace
{
void replaceAll (std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    for (auto pos = text.find (from); pos != std::string::npos; pos = text.find (from, pos + to.size()))
        text.replace (pos, from.size(), to);
}

std::string jsonEscaped (const std::string& text)
{
    std::string escaped;
    for (const char c : text)
    {
        if (c == '\\' || c == '"')
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}
} // namespace

std::string redact (std::string text, std::vector<std::pair<std::string, std::string>> replacements)
{
    std::sort (replacements.begin(), replacements.end(),
               [] (const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (const auto& [from, to] : replacements)
    {
        if (from.size() < 3) // never blank out short, common text
            continue;
        replaceAll (text, jsonEscaped (from), to);
        replaceAll (text, from, to);
    }
    return text;
}

std::vector<std::string> tailLines (const std::filesystem::path& path, std::size_t maxLines, std::size_t maxBytes)
{
    std::vector<std::string> lines;
    std::ifstream in (path, std::ios::binary);
    if (! in || maxLines == 0)
        return lines;
    in.seekg (0, std::ios::end);
    const auto size = static_cast<std::size_t> (std::max<std::streamoff> (0, in.tellg()));
    const auto start = size > maxBytes ? size - maxBytes : 0;
    in.seekg (static_cast<std::streamoff> (start));

    std::string line;
    if (start > 0)
        std::getline (in, line); // drop the partial first line
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back (line);
        if (lines.size() > maxLines)
            lines.erase (lines.begin());
    }
    return lines;
}

std::string buildReport (const DiagnosticSummary& s, const std::vector<std::string>& logLines)
{
    std::ostringstream out;
    out << "PianoAssist diagnostics\n"
        << "Version: " << s.appVersion << " (" << s.build << ")\n"
        << "Windows: " << s.os << "\n"
        << "Audio: " << (s.audio.empty() ? "(none)" : s.audio) << "\n"
        << "MIDI input: " << (s.selectedMidiInput.empty() ? "(none selected)" : s.selectedMidiInput) << "\n"
        << "MIDI inputs seen:";
    if (s.midiInputs.empty())
        out << " (none)";
    for (const auto& name : s.midiInputs)
        out << " [" << name << "]";
    out << "\nSong: " << (s.song.empty() ? "(none)" : s.song) << "\n"
        << "Practice: " << s.practice << "\n"
        << "\n--- Log (" << logLines.size() << " lines, newest last) ---\n";
    for (const auto& line : logLines)
        out << line << "\n";
    return out.str();
}

} // namespace pianoassist::diagnostics
