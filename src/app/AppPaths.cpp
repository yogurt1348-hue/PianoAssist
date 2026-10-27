#include "app/AppPaths.h"

#include "core/Utf8Path.h"

#include <juce_core/juce_core.h>

#include <fstream>

namespace pianoassist::app
{
namespace
{
std::filesystem::path chosenFolder;
bool chosenPortable = false;

std::filesystem::path toPath (const juce::File& file)
{
    return pathFromUtf8 (file.getFullPathName().toStdString());
}
} // namespace

std::filesystem::path userDataDirectory()
{
    return toPath (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)) / "PianoAssist";
}

std::filesystem::path programDirectory()
{
    return toPath (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
}

bool portableMarkerPresent()
{
    std::error_code ec;
    return std::filesystem::is_regular_file (programDirectory() / portableMarkerName, ec);
}

std::filesystem::path portableDataDirectory()
{
    return programDirectory() / "Data";
}

bool isWritableDirectory (const std::filesystem::path& folder)
{
    std::error_code ec;
    std::filesystem::create_directories (folder, ec);
    if (! std::filesystem::is_directory (folder, ec))
        return false;
    const auto probe = folder / ".write-test";
    {
        std::ofstream out (probe, std::ios::binary | std::ios::trunc);
        if (! out)
            return false;
        out << "ok";
        if (! out.flush())
            return false;
    }
    std::filesystem::remove (probe, ec);
    return true;
}

void setDataDirectory (std::filesystem::path folder, bool portable)
{
    chosenFolder = std::move (folder);
    chosenPortable = portable;
}

std::filesystem::path dataDirectory()
{
    return chosenFolder.empty() ? userDataDirectory() : chosenFolder;
}

bool isPortable() { return chosenPortable; }

std::filesystem::path logDirectory() { return dataDirectory() / "logs"; }

std::filesystem::path settingsFile() { return dataDirectory() / "settings.json"; }

std::filesystem::path libraryRoot (const std::string& setting)
{
    if (setting.empty())
        return dataDirectory() / "Songs";
    const auto path = pathFromUtf8 (setting);
    return path.is_absolute() ? path : dataDirectory() / path;
}

std::string librarySetting (const std::filesystem::path& folder)
{
    std::error_code ec;
    const auto relative = std::filesystem::relative (folder, dataDirectory(), ec);
    if (! ec && ! relative.empty() && *relative.begin() != "..")
        return pathToUtf8 (relative);
    return pathToUtf8 (folder);
}

} // namespace pianoassist::app
