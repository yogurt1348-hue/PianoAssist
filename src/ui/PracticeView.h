#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** Practice bar and keyboard highlights. Mode: 0 both hands, 1 right, 2 left. */
struct PracticeView
{
    bool practising = false;
    int mode = 0;
    bool waiting = false;
    bool accompanimentOnly = false;
    std::vector<std::uint8_t> required; ///< current gate, still to play
    std::vector<std::uint8_t> matched;  ///< current gate, already played
    std::vector<std::uint8_t> wrong;    ///< recent wrong presses
    std::size_t gateNumber = 0;
    std::size_t gateCount = 0;
    std::string requiredText; ///< note names of `required`
};

} // namespace pianoassist::ui
