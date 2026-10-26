#pragma once

#include <string>
#include <vector>

namespace pianoassist::lesson
{

/** Every gate-matching tolerance lives here, never as a loose constant. */
struct PracticeProfile
{
    static constexpr double defaultEarlyMs = 120.0;
    static constexpr double defaultLateMs = 180.0;
    static constexpr double maxWindowMs = 2000.0;

    std::string name = "Default";
    double onsetEarlyMs = defaultEarlyMs; ///< accept this long before the onset
    double onsetLateMs = defaultLateMs;   ///< accept this long after the onset
    bool ignoreVelocity = true;
    bool requireExactPitch = true;

    friend bool operator== (const PracticeProfile&, const PracticeProfile&) = default;
};

/** Offset added to input timestamps before matching, to compensate for the
    keyboard/driver input latency. Audio output latency never moves it. */
struct InputCalibration
{
    static constexpr double maxAbsOffsetMs = 500.0;

    double inputOffsetMs = 0.0;

    friend bool operator== (const InputCalibration&, const InputCalibration&) = default;
};

/** Empty when valid, otherwise one message per problem. */
std::vector<std::string> validate (const PracticeProfile& profile);
std::vector<std::string> validate (const InputCalibration& calibration);

} // namespace pianoassist::lesson
