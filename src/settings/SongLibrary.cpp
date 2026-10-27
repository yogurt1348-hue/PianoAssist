#include "settings/SongLibrary.h"

#include "core/BinaryFile.h"
#include "core/Sha256.h"
#include "core/Utf8Path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <random>

namespace pianoassist::settings
{
namespace
{
using nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* manifestName = "manifest.json";

std::string nowIso()
{
    const auto now = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s (&utc, &now);
#else
    gmtime_r (&now, &utc);
#endif
    char buffer[32];
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

json soundJson (const SoundSettings& s)
{
    return { { "kind", s.kind }, { "pluginId", s.pluginId }, { "pluginName", s.pluginName }, { "soundFontPath", s.soundFontPath } };
}

template <typename T>
bool readValue (const json& object, const char* key, T& target, std::vector<std::string>& warnings, const std::string& where)
{
    const auto it = object.find (key);
    if (it == object.end())
        return false;
    try
    {
        target = it->template get<T>();
        return true;
    }
    catch (const json::exception&)
    {
        warnings.push_back (where + "." + key + " has the wrong type; using default.");
        return false;
    }
}

bool finiteIn (double v, double lo, double hi) { return std::isfinite (v) && v >= lo && v <= hi; }

void readSound (const json& object, SoundSettings& sound, std::vector<std::string>& warnings, const std::string& where)
{
    if (! object.is_object())
        return;
    readValue (object, "kind", sound.kind, warnings, where);
    if (sound.kind != "main" && sound.kind != "builtIn" && sound.kind != "soundFont" && sound.kind != "plugin")
    {
        warnings.push_back (where + ".kind is unknown; using the main sound.");
        sound.kind = "main";
    }
    readValue (object, "pluginId", sound.pluginId, warnings, where);
    readValue (object, "pluginName", sound.pluginName, warnings, where);
    readValue (object, "soundFontPath", sound.soundFontPath, warnings, where);
}

bool isReservedWindowsName (std::string upper)
{
    for (auto& c : upper)
        c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    if (const auto dot = upper.find ('.'); dot != std::string::npos)
        upper.resize (dot);
    static const char* reserved[] = { "CON", "PRN", "AUX", "NUL" };
    for (const auto* r : reserved)
        if (upper == r)
            return true;
    return upper.size() == 4 && (upper.starts_with ("COM") || upper.starts_with ("LPT")) && upper[3] >= '1' && upper[3] <= '9';
}
} // namespace

std::string makeSongId()
{
    std::random_device device;
    std::mt19937_64 engine ((static_cast<std::uint64_t> (device()) << 32) ^ device()
                            ^ static_cast<std::uint64_t> (std::chrono::steady_clock::now().time_since_epoch().count()));
    char buffer[17];
    std::snprintf (buffer, sizeof (buffer), "%016llx", static_cast<unsigned long long> (engine()));
    return buffer;
}

std::string songFolderName (const std::string& name, const std::string& id)
{
    std::string clean;
    for (const unsigned char c : name)
    {
        if (c < 32 || std::string_view ("<>:\"/\\|?*").find (static_cast<char> (c)) != std::string_view::npos)
            clean += '_';
        else
            clean += static_cast<char> (c);
    }
    // Keep it short; never cut a UTF-8 sequence in half.
    if (clean.size() > 60)
    {
        std::size_t cut = 60;
        while (cut > 0 && (static_cast<unsigned char> (clean[cut]) & 0xC0) == 0x80)
            --cut;
        clean.resize (cut);
    }
    while (! clean.empty() && (clean.back() == ' ' || clean.back() == '.'))
        clean.pop_back();
    while (! clean.empty() && clean.front() == ' ')
        clean.erase (clean.begin());
    if (clean.empty())
        clean = "Song";
    if (isReservedWindowsName (clean))
        clean = "_" + clean;
    return clean + " [" + id.substr (0, 8) + "]";
}

std::string toJson (const SongManifest& m)
{
    json tracks = json::array();
    for (const auto& t : m.tracks)
        tracks.push_back ({ { "track", t.track }, { "channel", t.channel }, { "assignment", t.assignment }, { "muted", t.muted },
                            { "volume", t.volume }, { "sound", soundJson (t.sound) }, { "stateFile", t.stateFile } });
    const json root {
        { "schemaVersion", SongManifest::currentSchemaVersion },
        { "id", m.id },
        { "name", m.name },
        { "midiFile", m.midiFile },
        { "midiSha256", m.midiSha256 },
        { "source", { { "path", m.sourcePath }, { "importedAt", m.importedAt } } },
        { "tracks", tracks },
        { "practice",
          { { "mode", modeKey (m.practice.mode) },
            { "waitForNotes", m.practice.waitForNotes },
            { "playOtherPianoPart", m.practice.accompaniment.playOtherPianoPart },
            { "playBacking", m.practice.accompaniment.playBacking },
            { "tempoPercent", m.practice.tempoPercent },
            { "tolerance", m.tolerance } } },
        { "loop", { { "enabled", m.loop.enabled }, { "startSeconds", m.loop.startSeconds }, { "endSeconds", m.loop.endSeconds } } },
        { "view", { { "positionSeconds", m.positionSeconds }, { "pixelsPerSecond", m.pixelsPerSecond }, { "lastOpenedAt", m.lastOpenedAt } } },
    };
    return root.dump (2);
}

LoadedManifest manifestFromJson (std::string_view text)
{
    LoadedManifest result;
    auto& m = result.manifest;
    auto& warnings = result.warnings;
    if (text.size() > maxManifestBytes)
    {
        result.recovered = true;
        warnings.push_back ("Song settings file is too large; using defaults.");
        return result;
    }
    const json root = json::parse (text, nullptr, false);
    if (root.is_discarded() || ! root.is_object())
    {
        result.recovered = true;
        warnings.push_back ("Song settings file is damaged; using defaults.");
        return result;
    }
    int version = 0;
    readValue (root, "schemaVersion", version, warnings, "manifest");
    if (version < 1 || version > SongManifest::currentSchemaVersion)
    {
        result.recovered = true;
        warnings.push_back ("Song settings have an unknown schema version " + std::to_string (version) + "; using defaults.");
        return result;
    }

    readValue (root, "id", m.id, warnings, "manifest");
    readValue (root, "name", m.name, warnings, "manifest");
    readValue (root, "midiFile", m.midiFile, warnings, "manifest");
    // The MIDI must stay inside the song folder.
    if (m.midiFile.empty() || m.midiFile.find_first_of ("/\\:") != std::string::npos || m.midiFile == "..")
    {
        warnings.push_back ("manifest.midiFile is not a plain file name; using song.mid.");
        m.midiFile = "song.mid";
    }
    readValue (root, "midiSha256", m.midiSha256, warnings, "manifest");
    if (const auto source = root.find ("source"); source != root.end() && source->is_object())
    {
        readValue (*source, "path", m.sourcePath, warnings, "source");
        readValue (*source, "importedAt", m.importedAt, warnings, "source");
    }
    if (const auto tracks = root.find ("tracks"); tracks != root.end() && tracks->is_array())
        for (const auto& t : *tracks)
        {
            if (! t.is_object() || m.tracks.size() >= 256)
                continue;
            TrackState state;
            state.sound.kind = "main";
            readValue (t, "track", state.track, warnings, "track");
            readValue (t, "channel", state.channel, warnings, "track");
            readValue (t, "assignment", state.assignment, warnings, "track");
            readValue (t, "muted", state.muted, warnings, "track");
            readValue (t, "volume", state.volume, warnings, "track");
            readValue (t, "stateFile", state.stateFile, warnings, "track");
            if (const auto sound = t.find ("sound"); sound != t.end())
                readSound (*sound, state.sound, warnings, "track.sound");
            if (state.channel < 0 || state.channel > 15)
                state.channel = 0;
            if (state.assignment < -1 || state.assignment > 3)
                state.assignment = -1;
            if (! finiteIn (state.volume, 0.0, 1.0))
                state.volume = 1.0;
            if (state.stateFile.find ("..") != std::string::npos || state.stateFile.find (':') != std::string::npos)
                state.stateFile.clear();
            m.tracks.push_back (std::move (state));
        }
    if (const auto practice = root.find ("practice"); practice != root.end() && practice->is_object())
    {
        std::string mode = modeKey (m.practice.mode);
        readValue (*practice, "mode", mode, warnings, "practice");
        m.practice.mode = modeFromKey (mode).value_or (lesson::PracticeMode::bothHands);
        readValue (*practice, "waitForNotes", m.practice.waitForNotes, warnings, "practice");
        readValue (*practice, "playOtherPianoPart", m.practice.accompaniment.playOtherPianoPart, warnings, "practice");
        readValue (*practice, "playBacking", m.practice.accompaniment.playBacking, warnings, "practice");
        readValue (*practice, "tempoPercent", m.practice.tempoPercent, warnings, "practice");
        readValue (*practice, "tolerance", m.tolerance, warnings, "practice");
        if (! finiteIn (m.practice.tempoPercent, 25.0, 200.0))
            m.practice.tempoPercent = 100.0;
        m.tolerance = std::clamp (m.tolerance, 0, 2);
    }
    if (const auto loop = root.find ("loop"); loop != root.end() && loop->is_object())
    {
        readValue (*loop, "enabled", m.loop.enabled, warnings, "loop");
        readValue (*loop, "startSeconds", m.loop.startSeconds, warnings, "loop");
        readValue (*loop, "endSeconds", m.loop.endSeconds, warnings, "loop");
        if (! finiteIn (m.loop.startSeconds, 0.0, 1.0e6) || ! finiteIn (m.loop.endSeconds, 0.0, 1.0e6)
            || m.loop.endSeconds <= m.loop.startSeconds)
            m.loop = {};
    }
    if (const auto view = root.find ("view"); view != root.end() && view->is_object())
    {
        readValue (*view, "positionSeconds", m.positionSeconds, warnings, "view");
        if (view->contains ("lastOpenedAt"))
            readValue (*view, "lastOpenedAt", m.lastOpenedAt, warnings, "view");
        readValue (*view, "pixelsPerSecond", m.pixelsPerSecond, warnings, "view");
        if (! finiteIn (m.positionSeconds, 0.0, 1.0e6))
            m.positionSeconds = 0.0;
        if (! finiteIn (m.pixelsPerSecond, 10.0, 2000.0))
            m.pixelsPerSecond = 120.0;
    }
    return result;
}

Result<ImportResult> importMidi (const fs::path& root, const fs::path& source, const std::string& displayName)
{
    using R = Result<ImportResult>;
    auto bytes = readBinaryFile (source, maxMidiBytes);
    if (! bytes)
        return R::fail (bytes.error());
    const auto hash = sha256Hex (bytes.value());

    for (const auto& entry : listSongs (root))
        if (entry.midiSha256 == hash)
            return R::ok ({ entry, true });

    std::error_code ec;
    fs::create_directories (root, ec);
    if (ec)
        return R::fail ("Cannot create the library folder: " + ec.message());

    SongManifest manifest;
    manifest.id = makeSongId();
    manifest.name = displayName;
    manifest.midiSha256 = hash;
    manifest.sourcePath = pathToUtf8 (source);
    manifest.importedAt = nowIso();

    auto folder = root / pathFromUtf8 (songFolderName (displayName, manifest.id));
    for (int attempt = 0; fs::exists (folder, ec) && attempt < 8; ++attempt)
    {
        manifest.id = makeSongId();
        folder = root / pathFromUtf8 (songFolderName (displayName, manifest.id));
    }
    fs::create_directories (folder, ec);
    if (ec)
        return R::fail ("Cannot create the song folder: " + ec.message());
    if (const auto written = writeFileAtomically (folder / manifest.midiFile, bytes.value()); ! written)
        return R::fail (written.error());
    if (const auto saved = saveManifest (folder, manifest); ! saved)
        return R::fail (saved.error());
    return R::ok ({ { folder, manifest.id, manifest.name, hash }, false });
}

std::vector<SongEntry> listSongs (const fs::path& root)
{
    std::vector<SongEntry> songs;
    std::error_code ec;
    for (fs::directory_iterator it (root, ec), end; ! ec && it != end; it.increment (ec))
    {
        if (! it->is_directory (ec))
            continue;
        const auto manifestPath = it->path() / manifestName;
        auto bytes = readBinaryFile (manifestPath, maxManifestBytes);
        if (! bytes)
            continue;
        const auto& data = bytes.value();
        const auto loaded = manifestFromJson (std::string_view (reinterpret_cast<const char*> (data.data()), data.size()));
        if (loaded.recovered)
            continue;
        const auto& m = loaded.manifest;
        std::error_code missing;
        songs.push_back ({ it->path(), m.id, m.name, m.midiSha256, m.lastOpenedAt, m.sourcePath,
                           ! fs::is_regular_file (it->path() / pathFromUtf8 (m.midiFile), missing) });
    }
    std::sort (songs.begin(), songs.end(), [] (const SongEntry& a, const SongEntry& b) {
        if (a.lastOpenedAt != b.lastOpenedAt)
            return a.lastOpenedAt > b.lastOpenedAt; // ISO 8601 UTC sorts as text
        return a.name != b.name ? a.name < b.name : a.id < b.id;
    });
    return songs;
}

std::string currentTimeIso() { return nowIso(); }

Result<bool> removeSong (const fs::path& root, const fs::path& folder)
{
    using R = Result<bool>;
    std::error_code ec;
    const auto canonicalRoot = fs::weakly_canonical (root, ec);
    const auto canonicalFolder = fs::weakly_canonical (folder, ec);
    if (ec || canonicalFolder.parent_path() != canonicalRoot)
        return R::fail ("Not a song folder of this library.");
    if (! fs::is_regular_file (canonicalFolder / manifestName, ec))
        return R::fail ("The folder has no song settings; nothing was deleted.");
    fs::remove_all (canonicalFolder, ec);
    if (ec)
        return R::fail ("Could not delete the song folder: " + ec.message());
    return R::ok (true);
}

LoadedManifest loadManifest (const fs::path& folder)
{
    const auto path = folder / manifestName;
    std::error_code ec;
    if (! fs::exists (path, ec))
    {
        LoadedManifest result;
        result.recovered = true;
        result.warnings.push_back ("Song settings are missing; using defaults.");
        return result;
    }
    auto bytes = readBinaryFile (path, maxManifestBytes);
    LoadedManifest result;
    if (! bytes)
    {
        result.recovered = true;
        result.warnings.push_back (bytes.error() + "; using defaults.");
    }
    else
    {
        const auto& data = bytes.value();
        result = manifestFromJson (std::string_view (reinterpret_cast<const char*> (data.data()), data.size()));
    }
    if (result.recovered)
    {
        // Keep one copy of the damaged file for diagnosis; the next save replaces the original.
        fs::copy_file (path, folder / "manifest.json.bad", fs::copy_options::overwrite_existing, ec);
        // Fall back to the last good copy when there is one.
        if (fs::exists (folder / "manifest.json.bak", ec))
            if (auto backup = readBinaryFile (folder / "manifest.json.bak", maxManifestBytes); backup)
            {
                const auto& data = backup.value();
                auto previous = manifestFromJson (std::string_view (reinterpret_cast<const char*> (data.data()), data.size()));
                if (! previous.recovered)
                {
                    previous.warnings.insert (previous.warnings.begin(), result.warnings.begin(), result.warnings.end());
                    previous.warnings.push_back ("Restored the previous song settings (manifest.json.bak).");
                    previous.recovered = true;
                    return previous;
                }
            }
    }
    return result;
}

Result<bool> saveManifest (const fs::path& folder, const SongManifest& manifest)
{
    std::error_code ec;
    const auto path = folder / manifestName;
    if (fs::exists (path, ec))
    {
        // Only a manifest that still parses becomes the backup.
        if (auto bytes = readBinaryFile (path, maxManifestBytes); bytes)
        {
            const auto& data = bytes.value();
            if (! manifestFromJson (std::string_view (reinterpret_cast<const char*> (data.data()), data.size())).recovered)
                fs::copy_file (path, folder / "manifest.json.bak", fs::copy_options::overwrite_existing, ec);
        }
    }
    const auto text = toJson (manifest);
    return writeFileAtomically (path, std::vector<std::uint8_t> (text.begin(), text.end()));
}

Result<std::string> writeTrackState (const fs::path& folder, std::size_t track, const std::vector<std::uint8_t>& bytes)
{
    using R = Result<std::string>;
    std::error_code ec;
    fs::create_directories (folder / "plugin-state", ec);
    if (ec)
        return R::fail (ec.message());
    const auto relative = "plugin-state/track-" + std::to_string (track + 1) + ".bin";
    if (const auto written = writeFileAtomically (folder / pathFromUtf8 (relative), bytes); ! written)
        return R::fail (written.error());
    return R::ok (relative);
}

std::vector<std::uint8_t> readTrackState (const fs::path& folder, const std::string& relativeFile)
{
    if (relativeFile.empty() || relativeFile.find ("..") != std::string::npos)
        return {};
    auto bytes = readBinaryFile (folder / pathFromUtf8 (relativeFile), 256u * 1024u * 1024u);
    return bytes ? std::move (bytes).value() : std::vector<std::uint8_t> {};
}

} // namespace pianoassist::settings
