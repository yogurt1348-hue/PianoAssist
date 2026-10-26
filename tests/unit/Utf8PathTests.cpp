#include "Fixtures.h"
#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

#include <catch2/catch_test_macros.hpp>

using namespace pianoassist;

TEST_CASE ("UTF-8 paths round-trip on every platform")
{
    const std::string name = "Пьесы/гамма до мажор.mid";
    CHECK (pathToUtf8 (pathFromUtf8 (name)) == name);
    CHECK (fileNameForLog (pathFromUtf8 (name)) == "гамма до мажор.mid");
}

TEST_CASE ("Reading a file enforces the size limit")
{
    const auto path = test::fixturePath ("format0_c_major_scale_120bpm.mid");
    CHECK (readBinaryFile (path, 1024).isOk());
    CHECK_FALSE (readBinaryFile (path, 10).isOk());
    CHECK_FALSE (readBinaryFile (test::fixturePath ("does_not_exist.mid"), 1024).isOk());
}

TEST_CASE ("Error messages name the file, not the full path")
{
    const auto result = readBinaryFile (pathFromUtf8 ("/home/someone/private/song.mid"), 1024);
    REQUIRE_FALSE (result.isOk());
    CHECK (result.error().find ("someone") == std::string::npos);
    CHECK (result.error().find ("song.mid") != std::string::npos);
}
