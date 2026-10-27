#include "diagnostics/DiagnosticReport.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace pianoassist::diagnostics;

TEST_CASE ("Diagnostics never carry the user's folder")
{
    const std::string home = "C:\\Users\\Илья";
    const std::string log = R"({"fields":{"file":"C:\\Users\\Илья\\Music\\song.mid"},"message":"x"})";
    const auto plain = "Settings at " + home + "\\AppData\\Roaming\\PianoAssist";

    const auto cleaned = redact (log + "\n" + plain, { { home, "%USERPROFILE%" } });
    CHECK (cleaned.find ("Илья") == std::string::npos);
    CHECK (cleaned.find (R"(%USERPROFILE%\\Music\\song.mid)") != std::string::npos);
    CHECK (cleaned.find ("%USERPROFILE%\\AppData") != std::string::npos);

    // Too-short texts are ignored rather than blanking common letters.
    CHECK (redact ("C:\\x", { { "C:", "?" } }) == "C:\\x");
}

TEST_CASE ("Log tail keeps the newest lines and survives a missing file")
{
    const auto path = std::filesystem::temp_directory_path() / "pianoassist_tail_test.log";
    {
        std::ofstream out (path, std::ios::binary);
        for (int i = 1; i <= 10; ++i)
            out << "line " << i << "\r\n";
    }
    const auto last3 = tailLines (path, 3);
    REQUIRE (last3.size() == 3);
    CHECK (last3.front() == "line 8");
    CHECK (last3.back() == "line 10");

    const auto bounded = tailLines (path, 100, 20); // only the last 20 bytes
    REQUIRE_FALSE (bounded.empty());
    CHECK (bounded.back() == "line 10");
    CHECK (bounded.front() != "line 1");

    std::filesystem::remove (path);
    CHECK (tailLines (path, 5).empty());
}

TEST_CASE ("Report lists the summary before the log")
{
    DiagnosticSummary s;
    s.appVersion = "0.1.0";
    s.build = "Release";
    s.os = "Windows 11";
    s.midiInputs = { "CASIO USB-MIDI" };
    s.selectedMidiInput = "CASIO USB-MIDI";
    s.practice = "right hand, Normal timing";
    const auto text = buildReport (s, { "a", "b" });
    CHECK (text.find ("Version: 0.1.0 (Release)") != std::string::npos);
    CHECK (text.find ("MIDI inputs seen: [CASIO USB-MIDI]") != std::string::npos);
    CHECK (text.find ("Audio: (none)") != std::string::npos);
    CHECK (text.find ("--- Log (2 lines") < text.find ("\na\nb\n"));
}
