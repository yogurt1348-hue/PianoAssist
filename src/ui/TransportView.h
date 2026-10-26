#pragma once

#include <string>

namespace pianoassist::ui
{

struct TransportView
{
    enum class State
    {
        noSong,
        stopped,
        playing,
        paused,
        completed,
    };

    State state = State::noSong;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    bool audioAvailable = true;
    std::string audioText; ///< device summary or the error
    bool mayRingOut = false; ///< voices still releasing after pause/stop
};

} // namespace pianoassist::ui
