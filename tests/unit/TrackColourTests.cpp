// Automatic track colours for the owner's example file (bass, synth strings,
// two piccolos, Star Theme, drums, piano) and the colour text format.
#include "ui/TrackColours.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace pianoassist::ui;
namespace tc = pianoassist::ui::trackcolours;

TEST_CASE ("Instrument families get their colours and a second piccolo a darker blue")
{
    // 0-based General MIDI programs as stored in the song.
    const std::vector<TrackColourKey> keys {
        { 33, false },  // Fingered bass
        { 51, false },  // Synth Strings 2
        { 72, false },  // Piccolo
        { 72, false },  // Piccolo again
        { 103, false }, // Star Theme (FX 8)
        { 0, true },    // Drum kit on channel 10
        { 1, false },   // Piano Bright
    };
    const auto colours = autoTrackColours (keys);
    REQUIRE (colours.size() == keys.size());
    CHECK (colours[0] == tc::bass);
    CHECK (colours[1] == tc::strings);
    CHECK (colours[2] == tc::pipe);
    CHECK (colours[3] == tc::pipeDark);
    CHECK (colours[4] == tc::effects);
    CHECK (colours[5] == tc::drums);
    CHECK (colours[6] == tc::piano);
    CHECK (std::set<std::uint32_t> (colours.begin(), colours.end()).size() == colours.size());
}

TEST_CASE ("Many tracks of one family stay distinct, and no program means piano")
{
    const auto colours = autoTrackColours ({ { 40, false }, { 41, false }, { 42, false }, { 48, false }, { -1, false } });
    CHECK (std::set<std::uint32_t> (colours.begin(), colours.end()).size() == colours.size());
    CHECK (colours[4] == tc::piano);
}

TEST_CASE ("Hex colours parse and print")
{
    CHECK (parseHexColour ("#3fbfb4") == 0xff3fbfb4u);
    CHECK (parseHexColour (" 3FBFB4 ") == 0xff3fbfb4u);
    CHECK_FALSE (parseHexColour ("#3fbfb").has_value());
    CHECK_FALSE (parseHexColour ("#zzzzzz").has_value());
    CHECK (hexColour (0xff3fbfb4u) == "#3FBFB4");
}
