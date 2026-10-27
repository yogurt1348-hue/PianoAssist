#include "Fixtures.h"
#include "core/Utf8Path.h"
#include "settings/Settings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <nlohmann/json.hpp>

using namespace pianoassist;
using namespace pianoassist::settings;
using Catch::Matchers::ContainsSubstring;

namespace
{
Settings customSettings()
{
    Settings s;
    s.midiInput.deviceId = "\\\\?\\usb#vid_07cf&pid_6803#5&1a2b3c&0&1";
    s.midiInput.lastSeenName = "CASIO USB-MIDI";
    s.practiceProfile.name = "Медленно";
    s.practiceProfile.onsetEarlyMs = 200.0;
    s.practiceProfile.onsetLateMs = 250.5;
    s.practiceProfile.ignoreVelocity = false;
    s.visual.pixelsPerSecond = 240.0;
    s.visual.showNoteNames = false;
    s.calibration.inputOffsetMs = -12.5;
    s.practice.mode = lesson::PracticeMode::leftHand;
    s.practice.accompaniment.playOtherPianoPart = false;
    s.practice.accompaniment.playBacking = false;
    s.sound.kind = "soundFont";
    s.sound.soundFontPath = "C:\\Users\\Илья\\Music\\GeneralUser GS.sf2";
    s.practice.waitForNotes = true;
    s.sound.pluginFolders = { "D:\\VST3", "E:\\Плагины" };
    s.practice.tempoPercent = 75.0;
    s.appearance = { "light", "en", 1.25 };
    s.audio.deviceStateXml = "<DEVICESETUP deviceType=\"Windows Audio\"/>";
    s.recentFiles = { "C:\\Music\\Прелюдия.mid", "D:\\song.mid" };
    return s;
}
} // namespace

TEST_CASE ("Settings survive a JSON round trip")
{
    const auto original = customSettings();
    const auto loaded = fromJson (toJson (original));
    CHECK (loaded.status == LoadStatus::loaded);
    CHECK (loaded.warnings.empty());
    CHECK (loaded.storedSchemaVersion == currentSchemaVersion);
    CHECK (loaded.settings == original);
}

TEST_CASE ("Default settings survive a round trip")
{
    CHECK (fromJson (toJson (Settings {})).settings == Settings {});
}

TEST_CASE ("Settings survive a file round trip under a non-ASCII directory")
{
    test::TempDirectory temp;
    const auto path = temp.path() / pathFromUtf8 ("Пользователь") / "settings.json";
    REQUIRE (saveToFile (path, customSettings()).empty());

    const auto loaded = loadFromFile (path);
    CHECK (loaded.status == LoadStatus::loaded);
    CHECK (loaded.settings == customSettings());
}

TEST_CASE ("A missing settings file yields defaults without warnings")
{
    test::TempDirectory temp;
    const auto loaded = loadFromFile (temp.path() / "settings.json");
    CHECK (loaded.status == LoadStatus::defaulted);
    CHECK (loaded.warnings.empty());
    CHECK (loaded.settings == Settings {});
}

TEST_CASE ("Unusable settings files fall back to defaults safely")
{
    const auto check = [] (std::string_view text, std::string_view expectedWarning) {
        const auto loaded = fromJson (text);
        CHECK (loaded.status == LoadStatus::recovered);
        CHECK (loaded.settings == Settings {});
        REQUIRE (loaded.warnings.size() == 1);
        CHECK_THAT (loaded.warnings.front(), ContainsSubstring (std::string (expectedWarning)));
    };

    check ("{ not json", "not valid JSON");
    check ("[1, 2, 3]", "not valid JSON");
    check ("", "not valid JSON");
    check (R"({"practiceProfile": {}})", "no schema version");
    check (R"({"schemaVersion": "1"})", "no schema version");
    check (R"({"schemaVersion": 99})", "newer version");
    check (R"({"schemaVersion": 0})", "No migration");
    check (std::string (maxSettingsFileBytes + 1, ' '), "too large");
}

TEST_CASE ("Invalid individual fields fall back without resetting the rest")
{
    auto doc = nlohmann::json::parse (toJson (customSettings()));
    doc["practiceProfile"]["onsetEarlyMs"] = -5;
    doc["visual"]["showNoteNames"] = "yes";
    doc["calibration"]["inputOffsetMs"] = 10000;

    const auto loaded = fromJson (doc.dump());
    CHECK (loaded.status == LoadStatus::recovered);
    CHECK (loaded.warnings.size() == 3);

    auto expected = customSettings();
    expected.practiceProfile.onsetEarlyMs = lesson::PracticeProfile::defaultEarlyMs;
    expected.visual.showNoteNames = VisualPreferences {}.showNoteNames;
    expected.calibration.inputOffsetMs = 0.0;
    CHECK (loaded.settings == expected);
}

TEST_CASE ("Missing sections keep defaults and unknown keys are ignored")
{
    const auto loaded = fromJson (R"({"schemaVersion": 1, "futureFeature": {"x": 1},
                                      "midiInput": {"lastSeenName": "CDP-S110"}})");
    CHECK (loaded.status == LoadStatus::loaded);
    CHECK (loaded.settings.midiInput.lastSeenName == "CDP-S110");
    CHECK (loaded.settings.practiceProfile == lesson::PracticeProfile {});
}

TEST_CASE ("Practice choices are optional and validated")
{
    // Files from phase 3 builds have no "practice" section: defaults, no warning.
    const auto old = fromJson (R"({"schemaVersion": 1, "visual": {"pixelsPerSecond": 200}})");
    CHECK (old.status == LoadStatus::loaded);
    CHECK (old.settings.practice == PracticeChoices {});

    const auto bad = fromJson (R"({"schemaVersion": 1, "practice": {"mode": "feet", "playBacking": false}})");
    CHECK (bad.status == LoadStatus::recovered);
    CHECK (bad.settings.practice.mode == lesson::PracticeMode::bothHands);
    CHECK_FALSE (bad.settings.practice.accompaniment.playBacking);
}

TEST_CASE ("Unknown instrument kinds fall back to the built-in synth")
{
    const auto loaded = fromJson (R"({"schemaVersion": 1, "sound": {"kind": "vst2", "pluginName": "X"}})");
    CHECK (loaded.status == LoadStatus::recovered);
    CHECK (loaded.settings.sound.kind == "builtIn");
    CHECK (loaded.settings.sound.pluginName == "X");
}

TEST_CASE ("Appearance, tempo and recent files are validated")
{
    const auto loaded = fromJson (R"({"schemaVersion": 1,
        "appearance": {"theme": "pink", "language": "de", "uiScale": 9},
        "practice": {"tempoPercent": 10},
        "recent": {"files": ["a.mid", 5, "", "b.mid", "c.mid", "d.mid", "e.mid", "f.mid", "g.mid", "h.mid", "i.mid"]}})");
    CHECK (loaded.status == LoadStatus::recovered);
    CHECK (loaded.settings.appearance == AppearanceSettings {});
    CHECK (loaded.settings.practice.tempoPercent == 100.0);
    REQUIRE (loaded.settings.recentFiles.size() == maxRecentFiles);
    CHECK (loaded.settings.recentFiles.front() == "a.mid");
    CHECK (loaded.settings.recentFiles[1] == "b.mid");

    const auto notArray = fromJson (R"({"schemaVersion": 1, "recent": {"files": "a.mid"}})");
    CHECK (notArray.status == LoadStatus::recovered);
    CHECK (notArray.settings.recentFiles.empty());
}
