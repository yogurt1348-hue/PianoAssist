#include "settings/Settings.h"

#include "core/BinaryFile.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <optional>

namespace pianoassist::settings
{
namespace
{
using nlohmann::json;

/** Each entry upgrades a document from version N to N + 1 in place. */
const std::map<int, std::function<void (json&)>>& migrations()
{
    static const std::map<int, std::function<void (json&)>> steps {};
    return steps;
}

class Reader
{
public:
    Reader (const json& root, std::vector<std::string>& warnings) : root_ (root), warnings_ (warnings) {}

    template <typename T, typename Accept>
    void read (std::string_view section, std::string_view key, T& target, Accept accept)
    {
        const auto sectionIt = root_.find (section);
        if (sectionIt == root_.end() || ! sectionIt->is_object())
            return;
        const auto it = sectionIt->find (key);
        if (it == sectionIt->end())
            return;

        const auto path = std::string (section) + "." + std::string (key);
        if (! matchesType<T> (*it))
        {
            warnings_.push_back (path + " has the wrong type; using default.");
            return;
        }
        auto value = it->template get<T>();
        if (! accept (value))
        {
            warnings_.push_back (path + " is out of range; using default.");
            return;
        }
        target = std::move (value);
    }

    template <typename T>
    void read (std::string_view section, std::string_view key, T& target)
    {
        read (section, key, target, [] (const T&) { return true; });
    }

private:
    template <typename T>
    static bool matchesType (const json& value)
    {
        if constexpr (std::is_same_v<T, bool>)
            return value.is_boolean();
        else if constexpr (std::is_same_v<T, std::string>)
            return value.is_string();
        else
            return value.is_number();
    }

    const json& root_;
    std::vector<std::string>& warnings_;
};

bool finiteIn (double value, double lo, double hi)
{
    return std::isfinite (value) && value >= lo && value <= hi;
}

LoadResult defaultsWith (std::string warning, int storedVersion = 0)
{
    LoadResult result;
    result.status = LoadStatus::recovered;
    result.warnings.push_back (std::move (warning));
    result.storedSchemaVersion = storedVersion;
    return result;
}
} // namespace

std::string modeKey (lesson::PracticeMode mode)
{
    switch (mode)
    {
        case lesson::PracticeMode::bothHands: return "bothHands";
        case lesson::PracticeMode::rightHand: return "rightHand";
        case lesson::PracticeMode::leftHand:  return "leftHand";
    }
    return "bothHands";
}

std::optional<lesson::PracticeMode> modeFromKey (const std::string& key)
{
    for (const auto mode : { lesson::PracticeMode::bothHands, lesson::PracticeMode::rightHand, lesson::PracticeMode::leftHand })
        if (key == modeKey (mode))
            return mode;
    return std::nullopt;
}

std::string toJson (const Settings& s)
{
    const json root {
        { "schemaVersion", currentSchemaVersion },
        { "midiInput", { { "deviceId", s.midiInput.deviceId }, { "lastSeenName", s.midiInput.lastSeenName } } },
        { "practiceProfile",
          { { "name", s.practiceProfile.name },
            { "onsetEarlyMs", s.practiceProfile.onsetEarlyMs },
            { "onsetLateMs", s.practiceProfile.onsetLateMs },
            { "ignoreVelocity", s.practiceProfile.ignoreVelocity },
            { "requireExactPitch", s.practiceProfile.requireExactPitch } } },
        { "visual", { { "pixelsPerSecond", s.visual.pixelsPerSecond }, { "showNoteNames", s.visual.showNoteNames } } },
        { "calibration", { { "inputOffsetMs", s.calibration.inputOffsetMs } } },
        { "sound",
          { { "kind", s.sound.kind },
            { "pluginId", s.sound.pluginId },
            { "pluginName", s.sound.pluginName },
            { "soundFontPath", s.sound.soundFontPath },
            { "pluginFolders", s.sound.pluginFolders } } },
        { "practice",
          { { "mode", modeKey (s.practice.mode) },
            { "playOtherPianoPart", s.practice.accompaniment.playOtherPianoPart },
            { "playBacking", s.practice.accompaniment.playBacking },
            { "waitForNotes", s.practice.waitForNotes },
            { "tempoPercent", s.practice.tempoPercent } } },
        { "appearance",
          { { "theme", s.appearance.theme }, { "language", s.appearance.language }, { "uiScale", s.appearance.uiScale } } },
        { "audio", { { "deviceStateXml", s.audio.deviceStateXml } } },
        { "library", { { "root", s.library.root } } },
        { "updates", { { "includeTestBuilds", s.updates.includeTestBuilds } } },
        { "recent", { { "files", s.recentFiles } } },
    };
    return root.dump (2);
}

LoadResult fromJson (std::string_view text)
{
    if (text.size() > maxSettingsFileBytes)
        return defaultsWith ("Settings file is too large; using defaults.");

    json root = json::parse (text, nullptr, false);
    if (root.is_discarded() || ! root.is_object())
        return defaultsWith ("Settings file is not valid JSON; using defaults.");

    const auto versionIt = root.find ("schemaVersion");
    if (versionIt == root.end() || ! versionIt->is_number_integer())
        return defaultsWith ("Settings file has no schema version; using defaults.");

    const int storedVersion = versionIt->get<int>();
    if (storedVersion > currentSchemaVersion)
        return defaultsWith ("Settings were written by a newer version (schema "
                                 + std::to_string (storedVersion) + "); using defaults.",
                             storedVersion);

    for (int version = storedVersion; version < currentSchemaVersion; ++version)
    {
        const auto step = migrations().find (version);
        if (step == migrations().end())
            return defaultsWith ("No migration from settings schema " + std::to_string (version)
                                     + "; using defaults.",
                                 storedVersion);
        step->second (root);
    }

    LoadResult result;
    result.storedSchemaVersion = storedVersion;
    auto& s = result.settings;
    Reader reader (root, result.warnings);

    reader.read ("midiInput", "deviceId", s.midiInput.deviceId);
    reader.read ("midiInput", "lastSeenName", s.midiInput.lastSeenName);

    const auto window = [] (const double& ms) { return finiteIn (ms, 0.0, lesson::PracticeProfile::maxWindowMs); };
    reader.read ("practiceProfile", "name", s.practiceProfile.name,
                 [] (const std::string& name) { return ! name.empty() && name.size() <= 64; });
    reader.read ("practiceProfile", "onsetEarlyMs", s.practiceProfile.onsetEarlyMs, window);
    reader.read ("practiceProfile", "onsetLateMs", s.practiceProfile.onsetLateMs, window);
    reader.read ("practiceProfile", "ignoreVelocity", s.practiceProfile.ignoreVelocity);
    reader.read ("practiceProfile", "requireExactPitch", s.practiceProfile.requireExactPitch);

    reader.read ("visual", "pixelsPerSecond", s.visual.pixelsPerSecond,
                 [] (const double& v) { return finiteIn (v, 10.0, 2000.0); });
    reader.read ("visual", "showNoteNames", s.visual.showNoteNames);

    const auto maxOffset = lesson::InputCalibration::maxAbsOffsetMs;
    reader.read ("calibration", "inputOffsetMs", s.calibration.inputOffsetMs,
                 [maxOffset] (const double& v) { return finiteIn (v, -maxOffset, maxOffset); });

    std::string mode = modeKey (s.practice.mode);
    reader.read ("practice", "mode", mode, [] (const std::string& key) { return modeFromKey (key).has_value(); });
    s.practice.mode = modeFromKey (mode).value_or (lesson::PracticeMode::bothHands);
    reader.read ("practice", "playOtherPianoPart", s.practice.accompaniment.playOtherPianoPart);
    reader.read ("practice", "playBacking", s.practice.accompaniment.playBacking);

    reader.read ("practice", "waitForNotes", s.practice.waitForNotes);
    reader.read ("practice", "tempoPercent", s.practice.tempoPercent,
                 [] (const double& v) { return finiteIn (v, 25.0, 200.0); });

    reader.read ("appearance", "theme", s.appearance.theme,
                 [] (const std::string& v) { return v == "dark" || v == "light"; });
    reader.read ("appearance", "language", s.appearance.language,
                 [] (const std::string& v) { return v == "ru" || v == "en"; });
    reader.read ("appearance", "uiScale", s.appearance.uiScale, [] (const double& v) { return finiteIn (v, 0.75, 2.0); });
    reader.read ("audio", "deviceStateXml", s.audio.deviceStateXml,
                 [] (const std::string& v) { return v.size() <= 16 * 1024; });

    reader.read ("library", "root", s.library.root, [] (const std::string& v) { return v.size() <= 4096; });
    reader.read ("updates", "includeTestBuilds", s.updates.includeTestBuilds);

    if (const auto recent = root.find ("recent"); recent != root.end() && recent->is_object())
        if (const auto files = recent->find ("files"); files != recent->end())
        {
            if (! files->is_array())
                result.warnings.push_back ("recent.files has the wrong type; using default.");
            else
                for (const auto& file : *files)
                    if (file.is_string() && ! file.get<std::string>().empty() && file.get<std::string>().size() <= 4096
                        && s.recentFiles.size() < maxRecentFiles)
                        s.recentFiles.push_back (file.get<std::string>());
        }

    reader.read ("sound", "kind", s.sound.kind, [] (const std::string& kind) {
        return kind == "builtIn" || kind == "soundFont" || kind == "plugin";
    });
    const auto bounded = [] (const std::string& value) { return value.size() <= 4096; };
    reader.read ("sound", "pluginId", s.sound.pluginId, bounded);
    reader.read ("sound", "pluginName", s.sound.pluginName, bounded);
    reader.read ("sound", "soundFontPath", s.sound.soundFontPath, bounded);
    if (const auto sound = root.find ("sound"); sound != root.end() && sound->is_object())
        if (const auto folders = sound->find ("pluginFolders"); folders != sound->end() && folders->is_array())
            for (const auto& folder : *folders)
                if (folder.is_string() && ! folder.get<std::string>().empty() && folder.get<std::string>().size() <= 4096
                    && s.sound.pluginFolders.size() < 32)
                    s.sound.pluginFolders.push_back (folder.get<std::string>());

    result.status = result.warnings.empty() ? LoadStatus::loaded : LoadStatus::recovered;
    return result;
}

LoadResult loadFromFile (const std::filesystem::path& path)
{
    std::error_code ec;
    if (! std::filesystem::exists (path, ec))
        return {};

    auto bytes = readBinaryFile (path, maxSettingsFileBytes);
    if (! bytes)
        return defaultsWith (bytes.error() + "; using defaults.");

    const auto& data = bytes.value();
    return fromJson (std::string_view (reinterpret_cast<const char*> (data.data()), data.size()));
}

std::string saveToFile (const std::filesystem::path& path, const Settings& settings)
{
    const auto text = toJson (settings);
    const auto written = writeFileAtomically (path, std::vector<std::uint8_t> (text.begin(), text.end()));
    return written ? std::string {} : written.error();
}

} // namespace pianoassist::settings
