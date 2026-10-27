// Phase 5: settings writes are crash-safe and opened MIDI files are never modified.
#include "Fixtures.h"
#include "core/BinaryFile.h"
#include "settings/Settings.h"
#include "smf/SmfReader.h"
#include "timeline/Song.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace pianoassist;

namespace
{
std::filesystem::path scratchDir (const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all (dir);
    std::filesystem::create_directories (dir);
    return dir;
}
} // namespace

TEST_CASE ("Settings writes replace the file whole and leave no temporary file")
{
    const auto dir = scratchDir ("pianoassist_settings_safety");
    const auto file = dir / "settings.json";

    settings::Settings first;
    first.visual.pixelsPerSecond = 200.0;
    REQUIRE (settings::saveToFile (file, first).empty());

    // A crash during an earlier write left a half-written temporary file.
    {
        std::ofstream stale (dir / "settings.json.tmp", std::ios::binary);
        stale << "{\"schemaVer";
    }
    CHECK (settings::loadFromFile (file).settings == first); // the real file is intact

    settings::Settings second = first;
    second.practice.mode = lesson::PracticeMode::rightHand;
    REQUIRE (settings::saveToFile (file, second).empty());
    CHECK (settings::loadFromFile (file).settings == second);
    CHECK_FALSE (std::filesystem::exists (dir / "settings.json.tmp"));

    std::filesystem::remove_all (dir);
}

TEST_CASE ("Opening a MIDI file never changes it")
{
    const auto dir = scratchDir ("pianoassist_source_safety");
    const auto copy = dir / "song.mid";
    std::filesystem::copy_file (test::fixturePath ("format0_parser_edge_cases.mid"), copy);
    const auto before = test::readFixture ("format0_parser_edge_cases.mid");
    const auto stamp = std::filesystem::last_write_time (copy);

    for (int i = 0; i < 3; ++i)
    {
        auto bytes = readBinaryFile (copy, 64u * 1024u * 1024u);
        REQUIRE (bytes);
        auto file = smf::readSmf (bytes.value());
        REQUIRE (file);
        CHECK (timeline::buildSong (file.value())); // repairs happen in memory only
    }

    auto after = readBinaryFile (copy, 64u * 1024u * 1024u);
    REQUIRE (after);
    CHECK (after.value() == before);
    CHECK (std::filesystem::last_write_time (copy) == stamp);
    CHECK (std::distance (std::filesystem::directory_iterator (dir), std::filesystem::directory_iterator {}) == 1);

    std::filesystem::remove_all (dir);
}
