#pragma once

#include <cstdint>
#include <type_traits>

namespace pianoassist::midi_io
{

enum class InputEventType : std::uint8_t
{
    noteOn,
    noteOff,
    sustain, ///< CC64; value >= 64 means pedal down
};

/** Fixed-size event pushed from the MIDI callback into a lock-free queue.
    Trivially copyable so the callback never allocates. */
struct InputEvent
{
    std::int64_t timestampMicros = 0; ///< monotonic clock at callback receipt
    std::uint64_t sequence = 0;       ///< orders events with equal timestamps
    InputEventType type = InputEventType::noteOn;
    std::uint8_t channel = 0;         ///< 0-15
    std::uint8_t data1 = 0;           ///< pitch, or controller number
    std::uint8_t data2 = 0;           ///< velocity, or controller value
};

static_assert (std::is_trivially_copyable_v<InputEvent>);

} // namespace pianoassist::midi_io
