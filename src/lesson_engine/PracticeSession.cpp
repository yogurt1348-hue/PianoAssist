#include "lesson_engine/PracticeSession.h"

#include <algorithm>

namespace pianoassist::lesson
{
namespace
{
// Window edges are inclusive; this absorbs floating-point noise at them.
constexpr double edgeToleranceMs = 1.0e-6;

std::size_t countOf (const std::vector<std::uint8_t>& values, std::uint8_t value)
{
    return static_cast<std::size_t> (std::count (values.begin(), values.end(), value));
}

template <typename T>
std::size_t countPitch (const std::vector<T>& presses, std::uint8_t pitch)
{
    return static_cast<std::size_t> (std::count_if (presses.begin(), presses.end(), [pitch] (const T& p) { return p.pitch == pitch; }));
}
} // namespace

const char* toString (PressVerdict verdict) noexcept
{
    switch (verdict)
    {
        case PressVerdict::acceptedEarly:  return "accepted (early)";
        case PressVerdict::acceptedOnTime: return "accepted";
        case PressVerdict::acceptedLate:   return "accepted (late)";
        case PressVerdict::alreadyMatched: return "already matched";
        case PressVerdict::wrongPitch:     return "wrong pitch";
        case PressVerdict::tooEarly:       return "too early";
        case PressVerdict::noGate:         return "no gate";
    }
    return "no gate";
}

PracticeSession::PracticeSession (std::vector<Gate> gates, PracticeProfile profile)
    : gates_ (std::move (gates)), profile_ (std::move (profile))
{
}

void PracticeSession::resetTo (double songSeconds)
{
    // A gate whose onset is a hair before the position (seek to a note) is
    // still current, so seeking to a note asks for that note.
    constexpr double epsilon = 1.0e-6;
    current_ = static_cast<std::size_t> (
        std::lower_bound (gates_.begin(), gates_.end(), songSeconds - epsilon,
                          [] (const Gate& g, double t) { return g.onsetSeconds < t; })
        - gates_.begin());
    matched_.clear();
    retained_.clear();
}

const Gate* PracticeSession::currentGate() const noexcept
{
    return finished() ? nullptr : &gates_[current_];
}

std::optional<double> PracticeSession::nextBoundarySeconds() const noexcept
{
    if (finished())
        return std::nullopt;
    return gates_[current_].onsetSeconds + profile_.onsetLateMs / 1000.0;
}

std::vector<std::uint8_t> PracticeSession::missingPitches() const
{
    std::vector<std::uint8_t> missing;
    if (finished())
        return missing;
    std::vector<std::uint8_t> remaining;
    for (const auto& m : matched_)
        remaining.push_back (m.pitch);
    for (const auto pitch : gates_[current_].requiredPitches)
    {
        const auto it = std::find (remaining.begin(), remaining.end(), pitch);
        if (it != remaining.end())
            remaining.erase (it);
        else
            missing.push_back (pitch);
    }
    return missing;
}

std::vector<std::uint8_t> PracticeSession::matchedPitches() const
{
    std::vector<std::uint8_t> sorted;
    for (const auto& m : matched_)
        sorted.push_back (m.pitch);
    std::sort (sorted.begin(), sorted.end());
    return sorted;
}

PressResult PracticeSession::judge (std::uint8_t pitch, double songSeconds)
{
    PressResult result;
    result.pitch = pitch;
    if (finished())
        return result;

    const auto& gate = gates_[current_];
    result.gateIndex = gate.index;
    result.offsetMs = (songSeconds - gate.onsetSeconds) * 1000.0;

    const auto required = countOf (gate.requiredPitches, pitch);
    if (required == 0)
    {
        // Not for this gate, but it may be an early press for the next one
        // in a fast passage.
        result.verdict = PressVerdict::wrongPitch;
        retain (pitch, songSeconds);
        return result;
    }
    if (result.offsetMs < -profile_.onsetEarlyMs - edgeToleranceMs)
    {
        result.verdict = PressVerdict::tooEarly;
        return result;
    }
    // An incomplete attempt expires: notes matched too long ago drop out.
    std::erase_if (matched_, [songSeconds] (const Retained& m) { return songSeconds - m.songSeconds > chordWindowSeconds; });

    if (countPitch (matched_, pitch) >= required)
    {
        result.verdict = PressVerdict::alreadyMatched;
        retain (pitch, songSeconds);
        return result;
    }

    matched_.push_back ({ pitch, songSeconds });
    result.verdict = result.offsetMs < 0.0                  ? PressVerdict::acceptedEarly
                     : result.offsetMs <= profile_.onsetLateMs + edgeToleranceMs ? PressVerdict::acceptedOnTime
                                                               : PressVerdict::acceptedLate;
    if (matched_.size() == gate.requiredPitches.size())
    {
        result.gatePassed = true;
        ++passed_;
        advance (songSeconds);
    }
    return result;
}

void PracticeSession::retain (std::uint8_t pitch, double songSeconds)
{
    constexpr std::size_t maxRetained = 16;
    if (retained_.size() == maxRetained)
        retained_.erase (retained_.begin());
    retained_.push_back ({ pitch, songSeconds });
}

PressResult PracticeSession::press (std::uint8_t pitch, double songSeconds)
{
    return judge (pitch, songSeconds);
}

void PracticeSession::advance (double passedAt)
{
    ++current_;
    matched_.clear();

    // Presses that did not fit the previous gate may be inside the early
    // window of this one; everything else expires now.
    auto retained = std::move (retained_);
    retained_.clear();
    if (finished())
        return;
    const auto onset = gates_[current_].onsetSeconds;
    for (const auto& press : retained)
    {
        const double offsetMs = (press.songSeconds - onset) * 1000.0;
        const bool inWindow = offsetMs >= -profile_.onsetEarlyMs - edgeToleranceMs;
        const bool recent = passedAt - press.songSeconds <= carryWindowSeconds + 1.0e-9;
        if (inWindow && recent && countOf (gates_[current_].requiredPitches, press.pitch) > countPitch (matched_, press.pitch))
            matched_.push_back (press);
    }
    if (! finished() && ! gates_[current_].requiredPitches.empty() && matched_.size() == gates_[current_].requiredPitches.size())
    {
        // Fully satisfied by retained presses; extremely rare but consistent.
        ++passed_;
        advance (passedAt);
    }
}

} // namespace pianoassist::lesson
