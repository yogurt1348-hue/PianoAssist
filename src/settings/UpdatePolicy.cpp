#include "settings/UpdatePolicy.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <regex>

namespace pianoassist::settings
{
namespace
{
using json = nlohmann::json;

bool readInt (std::string_view text, int& out)
{
    if (text.empty() || text.size() > 6)
        return false;
    const auto [end, ec] = std::from_chars (text.data(), text.data() + text.size(), out);
    return ec == std::errc() && end == text.data() + text.size();
}

std::string stringOf (const json& object, const char* key)
{
    const auto it = object.find (key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<int> testBuildNumber (const std::string& tag)
{
    constexpr std::string_view prefix = "test-build-";
    int n = 0;
    if (tag.rfind (prefix, 0) == 0 && readInt (std::string_view (tag).substr (prefix.size()), n) && n > 0)
        return n;
    return std::nullopt;
}
} // namespace

std::optional<BuildVersion> parseVersion (std::string_view text)
{
    if (! text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix (1);
    BuildVersion v;
    int* parts[] = { &v.major, &v.minor, &v.patch };
    for (int i = 0; i < 3; ++i)
    {
        const auto dot = i < 2 ? text.find ('.') : std::string_view::npos;
        if (i < 2 && dot == std::string_view::npos)
            return std::nullopt;
        if (! readInt (text.substr (0, dot), *parts[i]) || *parts[i] < 0)
            return std::nullopt;
        text = dot == std::string_view::npos ? std::string_view() : text.substr (dot + 1);
    }
    return v;
}

std::string toString (const BuildVersion& v)
{
    auto text = std::to_string (v.major) + "." + std::to_string (v.minor) + "." + std::to_string (v.patch);
    if (v.testBuild > 0)
        text += " test build " + std::to_string (v.testBuild);
    return text;
}

UpdateCheck chooseUpdate (std::string_view releasesJson, const BuildVersion& installed, bool includeTestBuilds)
{
    UpdateCheck result;
    const auto root = json::parse (releasesJson, nullptr, false);
    if (root.is_discarded() || ! root.is_array())
    {
        result.error = "The release list from GitHub could not be read.";
        return result;
    }

    static const std::regex setupName (R"(PianoAssist-(\d+\.\d+\.\d+)-windows-x64-setup\.exe)");
    static const std::regex portableName (R"(PianoAssist-(\d+\.\d+\.\d+)-windows-x64-portable\.zip)");

    for (const auto& release : root)
    {
        if (! release.is_object() || release.value ("draft", false))
            continue;
        const bool prerelease = release.value ("prerelease", false);
        const auto tag = stringOf (release, "tag_name");

        ReleaseOffer offer;
        offer.tag = tag;
        offer.name = stringOf (release, "name");
        offer.pageUrl = stringOf (release, "html_url");
        offer.notes = stringOf (release, "body").substr (0, 1500);
        std::optional<BuildVersion> assetVersion;
        if (const auto assets = release.find ("assets"); assets != release.end() && assets->is_array())
            for (const auto& asset : *assets)
            {
                if (! asset.is_object())
                    continue;
                const auto name = stringOf (asset, "name");
                const auto url = stringOf (asset, "browser_download_url");
                std::smatch m;
                if (std::regex_match (name, m, setupName))
                {
                    offer.setupUrl = url;
                    assetVersion = parseVersion (m[1].str());
                }
                else if (std::regex_match (name, m, portableName))
                    offer.portableUrl = url;
            }

        if (const auto n = testBuildNumber (tag))
        {
            // Older test builds have no version in their file names: skip them.
            if (! includeTestBuilds || ! assetVersion)
                continue;
            offer.version = *assetVersion;
            offer.version.testBuild = *n;
            offer.testBuild = true;
        }
        else if (const auto v = parseVersion (tag); v && ! prerelease)
            offer.version = *v;
        else
            continue;

        if (offer.pageUrl.rfind ("https://github.com/", 0) != 0)
            continue; // only ever open GitHub pages
        if (offer.version > installed && (! result.newest || offer.version > result.newest->version))
            result.newest = offer;
    }
    return result;
}

} // namespace pianoassist::settings
