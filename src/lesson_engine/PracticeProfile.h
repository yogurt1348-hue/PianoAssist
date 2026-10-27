#pragma once

#include <string>
#include <vector>

namespace pianoassist::lesson
{

/** Every gate-matching tolerance lives here, never as a loose constant. */
struct PracticeProfile
{
    static constexpr double defaultEarlyMs = 250.0;
    static constexpr double defaultLateMs = 300.0;
    static constexpr double maxWindowMs = 2000.0;

    std::string name = "Normal";
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

/** Named tolerance presets offered in the window. */
enum class Tolerance
{
    strict, ///< 120 ms early, 180 ms late: the brief's original default
    normal, ///< 250 ms early, 300 ms late
    easy,   ///< 400 ms early, 600 ms late
};

PracticeProfile profileFor (Tolerance tolerance);

/** The preset a profile matches by name; normal for a custom or old profile. */
Tolerance toleranceOf (const PracticeProfile& profile);

/** Empty when valid, otherwise one message per problem. */
std::vector<std::string> validate (const PracticeProfile& profile);
std::vector<std::string> validate (const InputCalibration& calibration);

} // namespace pianoassist::lesson
