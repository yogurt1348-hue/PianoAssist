#include "app/AppPaths.h"

#include "core/Utf8Path.h"

#include <juce_core/juce_core.h>

namespace pianoassist::app
{

std::filesystem::path userDataDirectory()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return pathFromUtf8 (base.getFullPathName().toStdString()) / "PianoAssist";
}

std::filesystem::path logDirectory()
{
    return userDataDirectory() / "logs";
}

std::filesystem::path settingsFile()
{
    return userDataDirectory() / "settings.json";
}

} // namespace pianoassist::app
