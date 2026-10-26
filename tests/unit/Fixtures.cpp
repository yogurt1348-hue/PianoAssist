#include "Fixtures.h"

#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>

namespace pianoassist::test
{

std::filesystem::path fixturePath (std::string_view name)
{
    return pathFromUtf8 (PIANOASSIST_FIXTURE_DIR) / pathFromUtf8 (name);
}

std::vector<std::uint8_t> readFixture (std::string_view name)
{
    auto bytes = readBinaryFile (fixturePath (name), 1024 * 1024);
    INFO (bytes.error());
    REQUIRE (bytes.isOk());
    return std::move (bytes).value();
}

TempDirectory::TempDirectory()
{
    static std::atomic<int> counter { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path()
            / ("pianoassist-test-" + std::to_string (stamp) + "-" + std::to_string (counter++));
    std::filesystem::create_directories (path_);
}

TempDirectory::~TempDirectory()
{
    std::error_code ec;
    std::filesystem::remove_all (path_, ec);
}

} // namespace pianoassist::test
