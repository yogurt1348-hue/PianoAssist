#pragma once

#include <filesystem>

namespace pianoassist::app
{

/** Per-user data folder, e.g. %APPDATA%\PianoAssist on Windows. Nothing is
    written next to the executable, so a portable copy works anywhere. */
std::filesystem::path userDataDirectory();
std::filesystem::path logDirectory();
std::filesystem::path settingsFile();

} // namespace pianoassist::app
