#include "app/UpdateChecker.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace pianoassist::app
{

settings::BuildVersion installedVersion()
{
    auto version = settings::parseVersion (PIANOASSIST_VERSION).value_or (settings::BuildVersion {});
    version.testBuild = PIANOASSIST_BUILD_NUMBER;
    return version;
}

UpdateChecker::UpdateChecker() : alive_ (std::make_shared<std::atomic<bool>> (true)) {}

UpdateChecker::~UpdateChecker() { *alive_ = false; }

void UpdateChecker::check (bool includeTestBuilds, std::function<void (const settings::UpdateCheck&)> done)
{
    if (checking_)
        return;
    checking_ = true;
    auto alive = alive_;
    auto finish = [this, alive, done = std::move (done)] (settings::UpdateCheck result) {
        juce::MessageManager::callAsync ([this, alive, done, result = std::move (result)] {
            if (! *alive)
                return;
            checking_ = false;
            done (result);
        });
    };
    juce::Thread::launch ([includeTestBuilds, finish] {
        int status = 0;
        juce::StringPairArray headers;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: PianoAssist-update-check")
                                 .withConnectionTimeoutMs (10000)
                                 .withStatusCode (&status)
                                 .withResponseHeaders (&headers);
        settings::UpdateCheck result;
        const auto stream = juce::URL (releasesUrl).createInputStream (options);
        if (stream == nullptr)
        {
            result.error = "No connection to GitHub. Check the internet connection and try again later.";
            finish (std::move (result));
            return;
        }
        const auto body = stream->readEntireStreamAsString();
        if ((status == 403 || status == 429) && headers.getValue ("X-RateLimit-Remaining", "1") == "0")
        {
            const auto reset = headers.getValue ("X-RateLimit-Reset", "0").getLargeIntValue();
            const auto when = reset > 0 ? juce::Time (reset * 1000).toString (false, true, false, true) : juce::String ("later");
            result.error = ("GitHub allows only a few checks per hour from one address. Try again after " + when + ".").toStdString();
        }
        else if (status != 200)
            result.error = "GitHub answered with code " + std::to_string (status) + ". Try again later.";
        else
            result = settings::chooseUpdate (body.toStdString(), installedVersion(), includeTestBuilds);
        finish (std::move (result));
    });
}

} // namespace pianoassist::app
