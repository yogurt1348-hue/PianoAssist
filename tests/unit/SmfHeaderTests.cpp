#include "Fixtures.h"
#include "smf/SmfHeader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using pianoassist::smf::readSmfHeader;
using Catch::Matchers::ContainsSubstring;

TEST_CASE ("Format 0 fixture header is read")
{
    const auto bytes = pianoassist::test::readFixture ("format0_c_major_scale_120bpm.mid");
    const auto header = readSmfHeader (bytes);
    INFO (header.error());
    REQUIRE (header.isOk());
    CHECK (header.value().format == 0);
    CHECK (header.value().trackCount == 1);
    CHECK (header.value().ticksPerQuarter == 480);
}

TEST_CASE ("Format 1 fixture header lists every track")
{
    const auto header = readSmfHeader (pianoassist::test::readFixture ("format1_tempo_changes.mid"));
    REQUIRE (header.isOk());
    CHECK (header.value().format == 1);
    CHECK (header.value().trackCount == 3);
}

TEST_CASE ("A fixture with a non-ASCII file name loads")
{
    const auto header = readSmfHeader (pianoassist::test::readFixture ("гамма_до_мажор.mid"));
    CHECK (header.isOk());
}

TEST_CASE ("Malformed and unsupported files fail with actionable text")
{
    SECTION ("truncated track")
    {
        const auto header = readSmfHeader (pianoassist::test::readFixture ("malformed_truncated_track.mid"));
        REQUIRE_FALSE (header.isOk());
        CHECK_THAT (header.error(), ContainsSubstring ("truncated"));
    }
    SECTION ("format 2")
    {
        const auto header = readSmfHeader (pianoassist::test::readFixture ("unsupported_format2.mid"));
        REQUIRE_FALSE (header.isOk());
        CHECK_THAT (header.error(), ContainsSubstring ("format 0 or 1"));
    }
    SECTION ("not a MIDI file")
    {
        const std::vector<std::uint8_t> text { 'h', 'e', 'l', 'l', 'o' };
        CHECK_FALSE (readSmfHeader (text).isOk());
    }
    SECTION ("empty input")
    {
        CHECK_FALSE (readSmfHeader ({}).isOk());
    }
    SECTION ("SMPTE division")
    {
        std::vector<std::uint8_t> bytes { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE7, 0x28,
                                          'M', 'T', 'r', 'k', 0, 0, 0, 0 };
        const auto header = readSmfHeader (bytes);
        REQUIRE_FALSE (header.isOk());
        CHECK_THAT (header.error(), ContainsSubstring ("SMPTE"));
    }
    SECTION ("track length pointing past the end")
    {
        std::vector<std::uint8_t> bytes { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                                          'M', 'T', 'r', 'k', 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK_FALSE (readSmfHeader (bytes).isOk());
    }
}
