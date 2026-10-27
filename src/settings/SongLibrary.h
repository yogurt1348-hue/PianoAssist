#pragma once

#include "core/Result.h"
#include "settings/Settings.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pianoassist::settings
{

/** One track's saved choices inside a song. */
struct TrackState
{
    int track = 0;           ///< SMF track index, to check the saved list still matches the file
    int channel = 0;         ///< 0-based MIDI channel
    int assignment = -1;     ///< 0 right, 1 left, 2 both split, 3 accompaniment, 4 excluded; -1 = as guessed from the file
    bool muted = false;
    bool solo = false;
    double volume = 1.0;     ///< 0-1, scales the track's note velocities
    SoundSettings sound;     ///< kind "main" = the global main sound
    std::string stateFile;   ///< VST3 state file inside the song folder, relative; empty when none

    friend bool operator== (const TrackState&, const TrackState&) = default;
};

struct LoopRange
{
    bool enabled = false;
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    friend bool operator== (const LoopRange&, const LoopRange&) = default;
};

/** manifest.json of one song folder. Everything about practising this song;
    nothing about this computer's devices. */
struct SongManifest
{
    static constexpr int currentSchemaVersion = 1;

    std::string id;              ///< stable, unique, 16 hex digits
    std::string name;            ///< shown in lists
    std::string midiFile = "song.mid";
    std::string midiSha256;
    std::string sourcePath;      ///< where the MIDI was imported from (UTF-8), for information only
    std::string importedAt;      ///< ISO 8601 UTC
    std::string lastOpenedAt;    ///< ISO 8601 UTC, empty until first opened
    std::vector<TrackState> tracks;
    PracticeChoices practice;
    int tolerance = 1;           ///< 0 strict, 1 normal, 2 easy
    LoopRange loop;
    double positionSeconds = 0.0;
    double pixelsPerSecond = 120.0;

    friend bool operator== (const SongManifest&, const SongManifest&) = default;
};

struct SongEntry
{
    std::filesystem::path folder;
    std::string id;
    std::string name;
    std::string midiSha256;
    std::string lastOpenedAt;
    std::string sourcePath;   ///< where it was imported from (information only)
    bool midiMissing = false; ///< the copy in the song folder is gone
};

struct LoadedManifest
{
    SongManifest manifest;
    bool recovered = false;            ///< file unusable: defaults in use, old file kept as manifest.json.bad
    std::vector<std::string> warnings;
};

struct ImportResult
{
    SongEntry entry;
    bool reused = false; ///< the same MIDI (by SHA-256) was already in the library
};

inline constexpr std::size_t maxManifestBytes = 1024u * 1024u;
inline constexpr std::size_t maxMidiBytes = 32u * 1024u * 1024u;

std::string makeSongId();
/** "Name [id8]" without characters or names Windows rejects. */
std::string songFolderName (const std::string& name, const std::string& id);

std::string toJson (const SongManifest& manifest);
LoadedManifest manifestFromJson (std::string_view json);

/** Copies the MIDI into a new song folder under root, or returns the folder
    that already holds the same MIDI. The source file is only read. */
Result<ImportResult> importMidi (const std::filesystem::path& root, const std::filesystem::path& source,
                                 const std::string& displayName);
/** Songs in the library, most recently opened first, then by name. */
std::vector<SongEntry> listSongs (const std::filesystem::path& root);
/** Deletes a song folder from the library: its MIDI copy, settings and
    plug-in states. Only a folder directly inside root that holds a
    manifest is touched; the original MIDI it was imported from never is. */
Result<bool> removeSong (const std::filesystem::path& root, const std::filesystem::path& folder);
std::string currentTimeIso();

/** Never fails: a missing or damaged manifest yields defaults (and keeps the
    damaged file as manifest.json.bad for diagnosis). */
LoadedManifest loadManifest (const std::filesystem::path& folder);
/** Atomic write; the previous manifest is kept as manifest.json.bak. */
Result<bool> saveManifest (const std::filesystem::path& folder, const SongManifest& manifest);

/** VST3 state of one track, stored beside the manifest (plugin-state/track-N.bin). */
Result<std::string> writeTrackState (const std::filesystem::path& folder, std::size_t track, const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> readTrackState (const std::filesystem::path& folder, const std::string& relativeFile);

} // namespace pianoassist::settings
