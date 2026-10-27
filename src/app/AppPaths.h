#pragma once

#include <filesystem>
#include <string>

namespace pianoassist::app
{

/** File next to PianoAssist.exe that makes a copy portable: then all data
    (settings, logs, songs) lives in Data\ beside the exe instead of the
    per-user folder, and the two never mix. */
inline constexpr const char* portableMarkerName = "PianoAssist.portable";

/** Per-user data folder, e.g. %APPDATA%\PianoAssist on Windows. */
std::filesystem::path userDataDirectory();
/** The folder holding the exe. */
std::filesystem::path programDirectory();
bool portableMarkerPresent();
/** Data\ next to the exe. */
std::filesystem::path portableDataDirectory();
/** Creates the folder if needed and checks a file can be written there. */
bool isWritableDirectory (const std::filesystem::path& folder);

/** Chosen once at startup; every other path follows it. */
void setDataDirectory (std::filesystem::path folder, bool portable);
std::filesystem::path dataDirectory();
bool isPortable();

std::filesystem::path logDirectory();
std::filesystem::path settingsFile();
/** The song library: the user's choice (relative to the data folder when
    relative, so a moved portable folder still finds it) or Songs\. */
std::filesystem::path libraryRoot (const std::string& setting);
/** The setting to store for a chosen folder: relative when it is inside a portable data folder. */
std::string librarySetting (const std::filesystem::path& folder);

} // namespace pianoassist::app
