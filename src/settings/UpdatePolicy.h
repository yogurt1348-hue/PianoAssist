#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pianoassist::settings
{

/** A PianoAssist version as published on GitHub.

    Policy (docs/plan/14-updates-and-versions.md):
    - stable releases are tagged vMAJOR.MINOR.PATCH and are not pre-releases;
    - test builds are pre-releases tagged test-build-N; their version comes
      from the installer's name, PianoAssist-<version>-windows-x64-setup.exe;
    - versions compare by number, never as text: 0.10.0 > 0.9.0;
    - a test build of version V comes before the stable V (like 0.2.0-rc),
      and test builds of the same version compare by N. */
struct BuildVersion
{
    int major = 0, minor = 0, patch = 0;
    int testBuild = 0; ///< 0 = stable

    friend auto operator<=> (const BuildVersion& a, const BuildVersion& b)
    {
        if (auto c = a.major <=> b.major; c != 0) return c;
        if (auto c = a.minor <=> b.minor; c != 0) return c;
        if (auto c = a.patch <=> b.patch; c != 0) return c;
        if (a.testBuild == b.testBuild) return std::strong_ordering::equal;
        if (a.testBuild == 0) return std::strong_ordering::greater; // stable after its test builds
        if (b.testBuild == 0) return std::strong_ordering::less;
        return a.testBuild <=> b.testBuild;
    }
    friend bool operator== (const BuildVersion&, const BuildVersion&) = default;
};

/** "1.2.3" or "v1.2.3"; nothing else. */
std::optional<BuildVersion> parseVersion (std::string_view text);
std::string toString (const BuildVersion& version); ///< "0.2.0" or "0.2.0 test build 41"

struct ReleaseOffer
{
    BuildVersion version;
    std::string tag;
    std::string name;
    std::string notes;      ///< release description, shortened
    std::string pageUrl;    ///< html_url of the release
    std::string setupUrl;   ///< the installer asset, empty when none
    std::string portableUrl;
    bool testBuild = false;
};

struct UpdateCheck
{
    std::optional<ReleaseOffer> newest; ///< newer than installed; empty when up to date
    std::string error;                  ///< the list could not be read
};

/** Picks the newest release newer than installed from the GitHub releases
    API response (a JSON array). Drafts are ignored; pre-releases only when
    includeTestBuilds. Never throws. */
UpdateCheck chooseUpdate (std::string_view releasesJson, const BuildVersion& installed, bool includeTestBuilds);

} // namespace pianoassist::settings
