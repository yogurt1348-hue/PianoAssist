#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace pianoassist::test
{

/** Path of a checked-in fixture; name is UTF-8. */
std::filesystem::path fixturePath (std::string_view name);

/** Reads a fixture, failing the current test if it cannot be read. */
std::vector<std::uint8_t> readFixture (std::string_view name);

/** A fresh, empty directory under the system temp folder, removed on exit. */
class TempDirectory
{
public:
    TempDirectory();
    ~TempDirectory();
    TempDirectory (const TempDirectory&) = delete;
    TempDirectory& operator= (const TempDirectory&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace pianoassist::test
