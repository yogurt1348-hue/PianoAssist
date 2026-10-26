#include "lesson_engine/PracticeProfile.h"
#include "transport/TransportState.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace pianoassist;

TEST_CASE ("Default practice profile matches the documented tolerance")
{
    const lesson::PracticeProfile profile;
    CHECK (profile.onsetEarlyMs == 120.0);
    CHECK (profile.onsetLateMs == 180.0);
    CHECK (profile.ignoreVelocity);
    CHECK (profile.requireExactPitch);
    CHECK (lesson::validate (profile).empty());
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
