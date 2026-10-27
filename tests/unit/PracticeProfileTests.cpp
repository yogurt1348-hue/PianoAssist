#include "lesson_engine/PracticeProfile.h"
#include "transport/TransportState.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace pianoassist;

TEST_CASE ("Default practice profile is the Normal tolerance")
{
    const lesson::PracticeProfile profile;
    CHECK (profile == lesson::profileFor (lesson::Tolerance::normal));
    CHECK (profile.onsetEarlyMs == 250.0);
    CHECK (profile.onsetLateMs == 300.0);
    CHECK (profile.ignoreVelocity);
    CHECK (profile.requireExactPitch);
    CHECK (lesson::validate (profile).empty());
}

TEST_CASE ("Tolerance presets round-trip through their names")
{
    using lesson::Tolerance;
    for (const auto t : { Tolerance::strict, Tolerance::normal, Tolerance::easy })
    {
        const auto profile = lesson::profileFor (t);
        CHECK (lesson::toleranceOf (profile) == t);
        CHECK (lesson::validate (profile).empty());
    }
    CHECK (lesson::profileFor (Tolerance::strict).onsetEarlyMs == 120.0);
    CHECK (lesson::profileFor (Tolerance::easy).onsetLateMs == 600.0);

    lesson::PracticeProfile old;
    old.name = "Default"; // written by builds before the presets existed
    CHECK (lesson::toleranceOf (old) == Tolerance::normal);
}

TEST_CASE ("Practice profile validation rejects unusable windows")
{
    lesson::PracticeProfile profile;
    profile.onsetEarlyMs = -1.0;
    profile.onsetLateMs = std::numeric_limits<double>::quiet_NaN();
    profile.name.clear();
    CHECK (lesson::validate (profile).size() == 3);
}

TEST_CASE ("Calibration offset is bounded")
{
    CHECK (lesson::validate (lesson::InputCalibration { 499.0 }).empty());
    CHECK (lesson::validate (lesson::InputCalibration { -501.0 }).size() == 1);
}

TEST_CASE ("Transport state names match the architecture")
{
    CHECK (transport::toString (transport::TransportState::waitingForGate) == "WaitingForGate");
    CHECK (transport::toString (transport::TransportState::preRoll) == "PreRoll");
}
