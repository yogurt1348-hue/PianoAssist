#include "smf/ByteReader.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using pianoassist::smf::ByteReader;

TEST_CASE ("Variable-length quantities decode the SMF specification examples")
{
    struct Case
    {
        std::vector<std::uint8_t> bytes;
        std::uint32_t expected;
    };
    const std::array cases {
        Case { { 0x00 }, 0x00 },
        Case { { 0x7F }, 0x7F },
        Case { { 0x81, 0x00 }, 0x80 },
        Case { { 0xC0, 0x00 }, 0x2000 },
        Case { { 0xFF, 0x7F }, 0x3FFF },
        Case { { 0x81, 0x80, 0x00 }, 0x4000 },
        Case { { 0xFF, 0xFF, 0x7F }, 0x1FFFFF },
        Case { { 0x81, 0x80, 0x80, 0x00 }, 0x200000 },
        Case { { 0xFF, 0xFF, 0xFF, 0x7F }, 0x0FFFFFFF },
    };

    for (const auto& c : cases)
    {
        ByteReader reader (c.bytes);
        const auto value = reader.readVariableLength();
        REQUIRE (value.has_value());
        CHECK (*value == c.expected);
        CHECK (reader.atEnd());
    }
}

TEST_CASE ("A variable-length quantity longer than four bytes is rejected")
{
    const std::array<std::uint8_t, 5> bytes { 0xFF, 0xFF, 0xFF, 0xFF, 0x7F };
    ByteReader reader (bytes);
    CHECK_FALSE (reader.readVariableLength().has_value());
    CHECK (reader.position() == 0);
}

TEST_CASE ("A truncated variable-length quantity is rejected without advancing")
{
    const std::array<std::uint8_t, 2> bytes { 0x81, 0x80 };
    ByteReader reader (bytes);
    CHECK_FALSE (reader.readVariableLength().has_value());
    CHECK (reader.position() == 0);
}

TEST_CASE ("Fixed-width reads never run past the end of the buffer")
{
    const std::array<std::uint8_t, 3> bytes { 0x12, 0x34, 0x56 };
    ByteReader reader (bytes);
    CHECK_FALSE (reader.readU32().has_value());
    CHECK (reader.readU16() == std::optional<std::uint16_t> (0x1234));
    CHECK_FALSE (reader.readU16().has_value());
    CHECK (reader.readU8() == std::optional<std::uint8_t> (0x56));
    CHECK_FALSE (reader.readU8().has_value());
    CHECK_FALSE (reader.skip (1));
    CHECK_FALSE (reader.readBytes (1).has_value());
}
