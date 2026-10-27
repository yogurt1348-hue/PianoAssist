// Song library: one folder per song, a copy of the MIDI, settings that
// survive damage, and no sharing between different songs.
#include "Fixtures.h"
#include "core/BinaryFile.h"
#include "core/Sha256.h"
#include "core/Utf8Path.h"
#include "settings/SongLibrary.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

using namespace pianoassist;
using namespace pianoassist::settings;
namespace fs = std::filesystem;

namespace
{
fs::path scratchDir (const char* name)
{
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all (dir);
    fs::create_directories (dir);
    return dir;
}

fs::path writeFile (const fs::path& path, const std::string& content)
{
    fs::create_directories (path.parent_path());
    std::ofstream out (path, std::ios::binary);
    out << content;
    return path;
}

std::string readText (const fs::path& path)
{
    std::ifstream in (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), {} };
}
} // namespace

TEST_CASE ("SHA-256 matches the FIPS test vectors")
{
    const std::string abc = "abc";
    CHECK (sha256Hex ({ reinterpret_cast<const std::uint8_t*> (abc.data()), abc.size() })
           == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (sha256Hex ({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK (sha256Hex ({ reinterpret_cast<const std::uint8_t*> (twoBlocks.data()), twoBlocks.size() })
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE ("Song folder names are safe on Windows and carry the ID")
{
    CHECK (songFolderName ("Прелюдия до мажор", "0123456789abcdef") == "Прелюдия до мажор [01234567]");
    CHECK (songFolderName ("a<b>c:d\"e/f\\g|h?i*", "aaaaaaaabbbbbbbb") == "a_b_c_d_e_f_g_h_i_ [aaaaaaaa]");
    CHECK (songFolderName ("CON", "1111111122222222") == "_CON [11111111]");
    CHECK (songFolderName ("  trailing. ", "1111111122222222") == "trailing [11111111]");
    CHECK (songFolderName ("", "1111111122222222") == "Song [11111111]");
    const auto longName = songFolderName (std::string (40, 'x') + "яяяяяяяяяяяяяяя", "1111111122222222");
    CHECK (longName.size() <= 60 + 11);
    CHECK (makeSongId().size() == 16);
    CHECK (makeSongId() != makeSongId());
}

TEST_CASE ("Importing copies the MIDI, never changes the original, and reuses identical files")
{
    const auto dir = scratchDir ("pianoassist_library_import");
    const auto root = dir / "Songs";
    const auto source = writeFile (dir / "in" / "song.mid", readText (test::fixturePath ("format0_c_major_scale_120bpm.mid")));
    const auto before = readText (source);
    const auto time = fs::last_write_time (source);

    auto first = importMidi (root, source, "Scale");
    REQUIRE (first.isOk());
    CHECK_FALSE (first.value().reused);
    CHECK (readText (first.value().entry.folder / "song.mid") == before);
    CHECK (readText (source) == before);
    CHECK (fs::last_write_time (source) == time);

    // Same content from another place: same song, same settings.
    const auto copy = writeFile (dir / "other" / "song.mid", before);
    auto again = importMidi (root, copy, "Scale again");
    REQUIRE (again.isOk());
    CHECK (again.value().reused);
    CHECK (again.value().entry.folder == first.value().entry.folder);

    // Same file name, different music: a separate song.
    const auto different = writeFile (dir / "third" / "song.mid", readText (test::fixturePath ("practice_split_hands.mid")));
    auto other = importMidi (root, different, "Scale");
    REQUIRE (other.isOk());
    CHECK_FALSE (other.value().reused);
    CHECK (other.value().entry.folder != first.value().entry.folder);
    CHECK (other.value().entry.id != first.value().entry.id);
    CHECK (listSongs (root).size() == 2);
}

TEST_CASE ("The library lists recent songs first, flags a missing copy and removes only its own folder")
{
    const auto dir = scratchDir ("pianoassist_library_list");
    const auto root = dir / "Songs";
    const auto a = writeFile (dir / "in" / "a.mid", readText (test::fixturePath ("format0_c_major_scale_120bpm.mid")));
    const auto b = writeFile (dir / "in2" / "a.mid", readText (test::fixturePath ("practice_split_hands.mid")));
    const auto first = importMidi (root, a, "Same name").value().entry;
    const auto second = importMidi (root, b, "Same name").value().entry;
    CHECK (first.folder != second.folder); // same names, separate songs

    auto opened = loadManifest (second.folder).manifest;
    opened.lastOpenedAt = "2026-10-27T09:00:00Z";
    REQUIRE (saveManifest (second.folder, opened).isOk());
    auto songs = listSongs (root);
    REQUIRE (songs.size() == 2);
    CHECK (songs[0].id == second.id); // opened most recently
    CHECK (songs[0].lastOpenedAt == "2026-10-27T09:00:00Z");
    CHECK (songs[1].sourcePath == pathToUtf8 (a));

    fs::remove (first.folder / "song.mid");
    songs = listSongs (root);
    CHECK (songs[1].midiMissing);

    // Removing never reaches outside the library, and keeps the original.
    CHECK_FALSE (removeSong (root, dir / "in").isOk());
    CHECK (fs::exists (a));
    fs::create_directories (root / "not a song");
    CHECK_FALSE (removeSong (root, root / "not a song").isOk()); // no manifest: left alone
    CHECK (fs::exists (root / "not a song"));
    CHECK_FALSE (removeSong (root, root).isOk());
    REQUIRE (removeSong (root, first.folder).isOk());
    CHECK_FALSE (fs::exists (first.folder));
    CHECK (fs::exists (a));
    CHECK (listSongs (root).size() == 1);
}

TEST_CASE ("Song settings round-trip and stay separate per song")
{
    const auto dir = scratchDir ("pianoassist_library_roundtrip");
    SongManifest m;
    m.id = "00112233aabbccdd";
    m.name = "Тест";
    m.midiSha256 = "abc";
    m.sourcePath = "C:\\Музыка\\тест.mid";
    m.importedAt = "2026-10-27T10:00:00Z";
    TrackState bass;
    bass.track = 2;
    bass.channel = 1;
    bass.assignment = 3;
    bass.muted = true;
    bass.volume = 0.5;
    bass.sound.kind = "soundFont";
    bass.sound.soundFontPath = "C:\\SoundFonts\\GeneralUser.sf2";
    bass.sound.presetBank = 128;
    bass.sound.presetProgram = 0;
    bass.sound.presetName = "Standard Kit";
    bass.stateFile = "plugin-state/track-3.bin";
    bass.colour = "#3FBFB4";
    bass.visible = false;
    bass.order = 0;
    TrackState piano;
    piano.assignment = 0;
    piano.solo = true;
    piano.sound.kind = "main";
    m.tracks = { piano, bass };
    m.practice.mode = lesson::PracticeMode::leftHand;
    m.practice.waitForNotes = true;
    m.practice.tempoPercent = 70.0;
    m.tolerance = 2;
    m.loop = { true, 4.0, 12.5 };
    m.positionSeconds = 8.25;
    m.pixelsPerSecond = 300.0;

    const auto folder = dir / "Song [00112233]";
    fs::create_directories (folder);
    REQUIRE (saveManifest (folder, m).isOk());
    const auto loaded = loadManifest (folder);
    CHECK_FALSE (loaded.recovered);
    CHECK (loaded.warnings.empty());
    CHECK (loaded.manifest == m);

    const std::vector<std::uint8_t> state { 1, 2, 3, 0, 255 };
    auto written = writeTrackState (folder, 2, state);
    REQUIRE (written.isOk());
    CHECK (written.value() == "plugin-state/track-3.bin");
    CHECK (readTrackState (folder, written.value()) == state);
    CHECK (readTrackState (folder, "../../etc/passwd").empty());
}

TEST_CASE ("A damaged manifest falls back to the last good copy, then to defaults")
{
    const auto dir = scratchDir ("pianoassist_library_damage");
    const auto folder = dir / "Song [aaaaaaaa]";
    fs::create_directories (folder);
    SongManifest good;
    good.id = "aaaaaaaabbbbbbbb";
    good.name = "Good";
    good.practice.tempoPercent = 80.0;
    REQUIRE (saveManifest (folder, good).isOk());
    good.practice.tempoPercent = 90.0;
    REQUIRE (saveManifest (folder, good).isOk()); // the 80% version becomes the backup

    writeFile (folder / "manifest.json", "{\"schemaVersion\": 1, \"tracks\": [");
    const auto restored = loadManifest (folder);
    CHECK (restored.recovered);
    CHECK (restored.manifest.practice.tempoPercent == 80.0);
    CHECK (fs::exists (folder / "manifest.json.bad"));

    fs::remove (folder / "manifest.json.bak");
    writeFile (folder / "manifest.json", "not json");
    const auto defaults = loadManifest (folder);
    CHECK (defaults.recovered);
    CHECK (defaults.manifest == SongManifest {});

    const auto bad = manifestFromJson (R"({"schemaVersion": 1, "midiFile": "..\\..\\x.mid",
        "practice": {"tempoPercent": 5, "tolerance": 9}, "loop": {"enabled": true, "startSeconds": 5, "endSeconds": 2},
        "tracks": [{"channel": 40, "volume": 3, "assignment": 7, "stateFile": "../x", "sound": {"kind": "vst2"}}]})");
    CHECK (bad.manifest.midiFile == "song.mid");
    CHECK (bad.manifest.practice.tempoPercent == 100.0);
    CHECK (bad.manifest.tolerance == 2);
    CHECK_FALSE (bad.manifest.loop.enabled);
    REQUIRE (bad.manifest.tracks.size() == 1);
    CHECK (bad.manifest.tracks[0].channel == 0);
    CHECK (bad.manifest.tracks[0].volume == 1.0);
    CHECK (bad.manifest.tracks[0].assignment == -1);
    CHECK (bad.manifest.tracks[0].stateFile.empty());
    CHECK (bad.manifest.tracks[0].sound.kind == "main");
}

TEST_CASE ("A missing library folder lists nothing and import creates it")
{
    const auto dir = scratchDir ("pianoassist_library_missing");
    CHECK (listSongs (dir / "nope").empty());
    const auto source = writeFile (dir / "a.mid", readText (test::fixturePath ("format0_c_major_scale_120bpm.mid")));
    CHECK (importMidi (dir / "new" / "Songs", source, "A").isOk());
    CHECK_FALSE (importMidi (dir / "x", dir / "missing.mid", "B").isOk());
}
