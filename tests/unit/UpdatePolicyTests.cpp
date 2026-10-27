#include "settings/UpdatePolicy.h"

#include <catch2/catch_test_macros.hpp>

using namespace pianoassist::settings;

namespace
{
std::string release (const std::string& tag, bool prerelease, const std::string& assetVersion, bool draft = false)
{
    std::string assets = "[]";
    if (! assetVersion.empty())
        assets = R"([{"name": "PianoAssist-)" + assetVersion + R"(-windows-x64-setup.exe", "browser_download_url": "https://github.com/x/setup.exe"},
                     {"name": "PianoAssist-)" + assetVersion + R"(-windows-x64-portable.zip", "browser_download_url": "https://github.com/x/portable.zip"}])";
    return R"({"tag_name": ")" + tag + R"(", "name": ")" + tag + R"(", "prerelease": )" + (prerelease ? "true" : "false")
           + R"(, "draft": )" + (draft ? "true" : "false") + R"(, "html_url": "https://github.com/o/r/releases/tag/)" + tag
           + R"(", "body": "notes", "assets": )" + assets + "}";
}
} // namespace

TEST_CASE ("Versions compare by number, test builds before their stable version")
{
    const BuildVersion v0_9 { 0, 9, 0 }, v0_10 { 0, 10, 0 };
    CHECK (v0_10 > v0_9); // not as text
    const BuildVersion test41 { 0, 2, 0, 41 }, test7 { 0, 2, 0, 7 }, stable { 0, 2, 0 };
    CHECK (test41 > test7); // not "7" > "41"
    CHECK (stable > test41);
    CHECK (BuildVersion { 0, 3, 0, 1 } > stable);
    CHECK (parseVersion ("v1.20.3") == BuildVersion { 1, 20, 3 });
    CHECK_FALSE (parseVersion ("1.2"));
    CHECK_FALSE (parseVersion ("1.2.3-beta"));
    CHECK_FALSE (parseVersion ("test-build-4"));
    CHECK (toString (test41) == "0.2.0 test build 41");
}

TEST_CASE ("Only stable releases are offered unless test builds are wanted")
{
    const auto list = "[" + release ("test-build-50", true, "0.2.0") + "," + release ("v0.2.0", false, "0.2.0") + ","
                      + release ("v0.3.0", false, "0.3.0", true) + "," + release ("test-build-9", true, "") + "]";
    const BuildVersion installed { 0, 1, 0 };

    auto stableOnly = chooseUpdate (list, installed, false);
    REQUIRE (stableOnly.newest);
    CHECK (stableOnly.newest->tag == "v0.2.0"); // the draft v0.3.0 is ignored
    CHECK (stableOnly.newest->setupUrl == "https://github.com/x/setup.exe");
    CHECK (stableOnly.newest->portableUrl == "https://github.com/x/portable.zip");

    const auto upToDate = chooseUpdate (list, BuildVersion { 0, 2, 0 }, true);
    CHECK_FALSE (upToDate.newest); // test-build-50 of 0.2.0 is older than 0.2.0
    CHECK (upToDate.error.empty());

    const auto fromTest = chooseUpdate ("[" + release ("test-build-50", true, "0.2.0") + "]", BuildVersion { 0, 2, 0, 41 }, true);
    REQUIRE (fromTest.newest);
    CHECK (fromTest.newest->testBuild);
    CHECK (fromTest.newest->version == BuildVersion { 0, 2, 0, 50 });
}

TEST_CASE ("A broken release list is an error, not a crash")
{
    CHECK_FALSE (chooseUpdate ("{\"message\": \"API rate limit exceeded\"}", {}, false).error.empty());
    CHECK_FALSE (chooseUpdate ("<html>", {}, false).error.empty());
    CHECK (chooseUpdate ("[1, null, {\"tag_name\": 5}]", {}, true).error.empty());
}
