#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** The pitches of one practice step, split by hand. */
struct StepView
{
    std::vector<std::uint8_t> right;
    std::vector<std::uint8_t> left;

    bool empty() const noexcept { return right.empty() && left.empty(); }
};

/** Practice bar, "next notes" board and keyboard highlights.
    Mode: 0 both hands, 1 right, 2 left. Tolerance: 0 strict, 1 normal, 2 easy. */
struct PracticeView
{
    bool practising = false;
    int mode = 0;
    int tolerance = 1;
    bool waiting = false;
    bool accompanimentOnly = false;
    std::vector<std::uint8_t> required; ///< current step, still to play
    std::vector<std::uint8_t> matched;  ///< current step, already played
    std::vector<std::uint8_t> wrong;    ///< recent wrong presses
    StepView current;                   ///< current step, both played and not
    std::vector<StepView> upcoming;     ///< the next few steps
    std::size_t gateNumber = 0;
    std::size_t gateCount = 0;
};

} // namespace pianoassist::ui
