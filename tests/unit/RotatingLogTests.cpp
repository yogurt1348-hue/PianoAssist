#include "Fixtures.h"
#include "core/BinaryFile.h"
#include "diagnostics/RotatingLog.h"
#include "diagnostics/SessionReport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>
#include <sstream>

using namespace pianoassist;
using namespace pianoassist::diagnostics;
using Catch::Matchers::ContainsSubstring;

namespace
{
std::vector<nlohmann::json> readLines (const std::filesystem::path& path)
{
    const auto bytes = readBinaryFile (path, 1024 * 1024);
    REQUIRE (bytes.isOk());
    std::istringstream in (std::string (bytes.value().begin(), bytes.value().end()));
    std::vector<nlohmann::json> lines;
    for (std::string line; std::getline (in, line);)
        lines.push_back (nlohmann::json::parse (line));
    return lines;
}

RotatingLog::Clock fixedClock()
{
    return [] { return std::string ("2026-10-26T00:00:00.000Z"); };
}
} // namespace

TEST_CASE ("Log lines are structured JSON")
{
    test::TempDirectory temp;
    RotatingLog log ({ temp.path() }, fixedClock());
    REQUIRE (log.isOpen());
    log.write (LogLevel::warning, "midi", "Device \"CASIO\" lost\n", { { "device", "CASIO USB-MIDI" } });

    const auto lines = readLines (log.currentFile());
    REQUIRE (lines.size() == 1);
    CHECK (lines[0]["time"] == "2026-10-26T00:00:00.000Z");
    CHECK (lines[0]["level"] == "warning");
    CHECK (lines[0]["category"] == "midi");
    CHECK (lines[0]["message"] == "Device \"CASIO\" lost\n");
    CHECK (lines[0]["fields"]["device"] == "CASIO USB-MIDI");
}

TEST_CASE ("Invalid UTF-8 from drivers does not break the log")
{
    test::TempDirectory temp;
    RotatingLog log ({ temp.path() }, fixedClock());
    log.write (LogLevel::info, "midi", std::string ("bad \xFF name"));
    CHECK (readLines (log.currentFile()).size() == 1);
}

TEST_CASE ("Log rotates by size and keeps a bounded number of archives")
{
    test::TempDirectory temp;
    RotatingLog::Options options;
    options.directory = temp.path();
    options.maxFileBytes = 300;
    options.maxArchivedFiles = 2;
    RotatingLog log (options, fixedClock());

    for (int i = 0; i < 40; ++i)
        log.write (LogLevel::info, "test", "line " + std::to_string (i));

    CHECK (std::filesystem::exists (temp.path() / "pianoassist.log"));
    CHECK (std::filesystem::exists (temp.path() / "pianoassist.1.log"));
    CHECK (std::filesystem::exists (temp.path() / "pianoassist.2.log"));
    CHECK_FALSE (std::filesystem::exists (temp.path() / "pianoassist.3.log"));
    CHECK (std::filesystem::file_size (temp.path() / "pianoassist.log") <= 300);

    const auto current = readLines (temp.path() / "pianoassist.log");
    REQUIRE_FALSE (current.empty());
    CHECK (current.back()["message"] == "line 39");
}

TEST_CASE ("Session start records version, OS and devices")
{
    test::TempDirectory temp;
    RotatingLog log ({ temp.path() }, fixedClock());
    logSessionStart (log, { "0.1.0", "Windows 11", { "CASIO USB-MIDI", "Loopback" }, "", "" });

    const auto lines = readLines (log.currentFile());
    REQUIRE (lines.size() == 1);
    const auto& fields = lines[0]["fields"];
    CHECK (fields["appVersion"] == "0.1.0");
    CHECK (fields["os"] == "Windows 11");
    CHECK (fields["midiInputs"] == "CASIO USB-MIDI; Loopback");
    CHECK (fields["selectedMidiInput"] == "(none)");
    CHECK (fields["audioDevice"] == "(not opened)");
}

TEST_CASE ("Timestamps are ISO-8601 UTC with milliseconds")
{
    const auto stamp = utcTimestampNow();
    CHECK (stamp.size() == 24);
    CHECK (stamp.back() == 'Z');
    CHECK (stamp[10] == 'T');
}
