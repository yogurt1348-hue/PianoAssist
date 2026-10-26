#pragma once

#include "lesson_engine/PracticeProfile.h"

#include <filesystem>
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

struct Settings
{
    MidiInputSettings midiInput;
    lesson::PracticeProfile practiceProfile;
    VisualPreferences visual;
    lesson::InputCalibration calibration;

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

/** Never fails: unusable input yields defaults plus warnings. Invalid single
    fields fall back individually so one bad value does not reset the rest. */
LoadResult fromJson (std::string_view json);

LoadResult loadFromFile (const std::filesystem::path& path);

/** Returns an empty string on success, otherwise the error. */
std::string saveToFile (const std::filesystem::path& path, const Settings& settings);

} // namespace pianoassist::settings
