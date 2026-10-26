#pragma once

#include <cstdint>
#include <string_view>

namespace pianoassist::transport
{

/** Transport states from the architecture. WaitingForGate freezes the logical
    timeline only: sound already emitted may ring out. */
enum class TransportState : std::uint8_t
{
    stopped,
    preRoll,
    playing,
    waitingForGate,
    paused,
    completed,
    error,
};

constexpr std::string_view toString (TransportState state) noexcept
{
    switch (state)
    {
        case TransportState::stopped:        return "Stopped";
        case TransportState::preRoll:        return "PreRoll";
        case TransportState::playing:        return "Playing";
        case TransportState::waitingForGate: return "WaitingForGate";
        case TransportState::paused:         return "Paused";
        case TransportState::completed:      return "Completed";
        case TransportState::error:          return "Error";
    }
    return "Unknown";
}

} // namespace pianoassist::transport
