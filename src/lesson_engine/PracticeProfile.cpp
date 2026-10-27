#include "lesson_engine/PracticeProfile.h"

#include <cmath>

namespace pianoassist::lesson
{
namespace
{
bool inWindowRange (double ms)
{
    return std::isfinite (ms) && ms >= 0.0 && ms <= PracticeProfile::maxWindowMs;
}
} // namespace

PracticeProfile profileFor (Tolerance tolerance)
{
    PracticeProfile profile;
    switch (tolerance)
    {
        case Tolerance::strict: profile.name = "Strict"; profile.onsetEarlyMs = 120.0; profile.onsetLateMs = 180.0; break;
        case Tolerance::normal: break;
        case Tolerance::easy:   profile.name = "Easy";   profile.onsetEarlyMs = 400.0; profile.onsetLateMs = 600.0; break;
    }
    return profile;
}

Tolerance toleranceOf (const PracticeProfile& profile)
{
    if (profile.name == "Strict")
        return Tolerance::strict;
    if (profile.name == "Easy")
        return Tolerance::easy;
    return Tolerance::normal;
}

std::vector<std::string> validate (const PracticeProfile& profile)
{
    std::vector<std::string> problems;
    if (profile.name.empty())
        problems.push_back ("Practice profile name is empty.");
    if (! inWindowRange (profile.onsetEarlyMs))
        problems.push_back ("Early onset window must be between 0 and 2000 ms.");
    if (! inWindowRange (profile.onsetLateMs))
        problems.push_back ("Late onset window must be between 0 and 2000 ms.");
    return problems;
}

std::vector<std::string> validate (const InputCalibration& calibration)
{
    std::vector<std::string> problems;
    if (! std::isfinite (calibration.inputOffsetMs)
        || std::abs (calibration.inputOffsetMs) > InputCalibration::maxAbsOffsetMs)
        problems.push_back ("Input calibration offset must be within +/-500 ms.");
    return problems;
}

} // namespace pianoassist::lesson
