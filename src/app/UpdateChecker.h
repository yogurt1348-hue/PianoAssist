#pragma once

#include "settings/UpdatePolicy.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace pianoassist::app
{

/** The version this exe is, from the build (CI passes the test build number). */
settings::BuildVersion installedVersion();

/** Reads the public release list on GitHub in the background (no sign-in)
    and reports on the message thread. Never downloads or installs anything:
    the user opens the release page themselves. */
class UpdateChecker final
{
public:
    static constexpr const char* releasesUrl = "https://api.github.com/repos/yogurt1348-hue/PianoAssist/releases?per_page=30";
    static constexpr const char* releasesPage = "https://github.com/yogurt1348-hue/PianoAssist/releases";

    UpdateChecker();
    ~UpdateChecker();

    /** Results arrive on the message thread; nothing arrives after destruction. */
    void check (bool includeTestBuilds, std::function<void (const settings::UpdateCheck&)> done);
    bool isChecking() const { return checking_; }

private:
    std::shared_ptr<std::atomic<bool>> alive_;
    bool checking_ = false;
};

} // namespace pianoassist::app
