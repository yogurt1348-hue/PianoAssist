#pragma once

#include "lesson_engine/PracticeMode.h"
#include "lesson_engine/PracticeProfile.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pianoassist::settings
{

/** Bump when the stored shape changes, and add a migration step from the
    previous version in Settings.cpp. */
inline constexpr int currentSchemaVersion = 1;

/** Settings files are untrusted input; anything larger is rejected. */
inline constexpr std::size_t maxSettingsFileBytes = 64u * 1024u;

struct MidiInputSettings
{
    std::string deviceId;     ///< stable identifier from the OS; may be empty
    std::string lastSeenName; ///< shown when the device is missing

    friend bool operator== (const MidiInputSettings&, const MidiInputSettings&) = default;
};

struct VisualPreferences
{
    double pixelsPerSecond = 120.0;
    bool showNoteNames = true;

    friend bool operator== (const VisualPreferences&, const VisualPreferences&) = default;
};

/** The learner's last practice choices, restored at start. */
struct PracticeChoices
{
    lesson::PracticeMode mode = lesson::PracticeMode::bothHands;
    lesson::Accompaniment accompaniment;
    bool waitForNotes = false;   ///< "Wait for my notes" (practice) on at start
    double tempoPercent = 100.0; ///< 25-200

    friend bool operator== (const PracticeChoices&, const PracticeChoices&) = default;
};

/** Which instrument plays the song (global main sound), or one track (kind
    may then also be "main": follow the main sound). The plug-in identifier is JUCE's
    description identifier; the SoundFont path is UTF-8. */
struct SoundSettings
{
    std::string kind = "builtIn"; ///< builtIn, soundFont or plugin
    std::string pluginId;
    std::string pluginName;   ///< shown when the plug-in is missing
    std::string soundFontPath;
    std::vector<std::string> pluginFolders; ///< extra VST3 folders to scan (UTF-8), global only

    friend bool operator== (const SoundSettings&, const SoundSettings&) = default;
};

/** Look of the window. Theme and language apply at once or after a restart
    (language); scale is the whole-window zoom. */
struct AppearanceSettings
{
    std::string theme = "dark";   ///< dark or light
    std::string language = "ru";  ///< ru or en
    double uiScale = 1.0;         ///< 0.75-2.0

    friend bool operator== (const AppearanceSettings&, const AppearanceSettings&) = default;
};

/** Audio output chosen in Settings; empty means the system default. */
struct AudioSettings
{
    std::string deviceStateXml; ///< JUCE AudioDeviceManager state

    friend bool operator== (const AudioSettings&, const AudioSettings&) = default;
};

inline constexpr std::size_t maxRecentFiles = 8;

struct Settings
{
    MidiInputSettings midiInput;
    lesson::PracticeProfile practiceProfile;
    VisualPreferences visual;
    lesson::InputCalibration calibration;
    PracticeChoices practice;
    SoundSettings sound;
    AppearanceSettings appearance;
    AudioSettings audio;
    std::vector<std::string> recentFiles; ///< UTF-8 paths, newest first

    friend bool operator== (const Settings&, const Settings&) = default;
};

enum class LoadStatus
{
    loaded,    ///< file read and every field accepted
    defaulted, ///< no file yet; defaults in use
    recovered, ///< file unusable or partly invalid; fallbacks in use
};

struct LoadResult
{
    Settings settings;
    LoadStatus status = LoadStatus::defaulted;
    std::vector<std::string> warnings; ///< one line per rejected value
    int storedSchemaVersion = 0;       ///< 0 when unknown
};

std::string toJson (const Settings& settings);
std::string modeKey (lesson::PracticeMode mode);
std::optional<lesson::PracticeMode> modeFromKey (const std::string& key);

/** Never fails: unusable input yields defaults plus warnings. Invalid single
    fields fall back individually so one bad value does not reset the rest. */
LoadResult fromJson (std::string_view json);

LoadResult loadFromFile (const std::filesystem::path& path);

/** Returns an empty string on success, otherwise the error. */
std::string saveToFile (const std::filesystem::path& path, const Settings& settings);

} // namespace pianoassist::settings
