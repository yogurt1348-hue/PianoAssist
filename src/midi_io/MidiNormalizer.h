#pragma once

#include "midi_io/InputEvent.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace pianoassist::midi_io
{

/** Converts one complete raw MIDI message into an InputEvent. Safe for the
    MIDI callback: no allocation, no locks. Note-on with velocity 0 becomes
    note-off. Returns nullopt only for empty or truncated channel messages. */
std::optional<InputEvent> normalizeMidiMessage (const std::uint8_t* bytes, std::size_t size,
                                                std::int64_t timestampMicros, std::uint64_t sequence) noexcept;

} // namespace pianoassist::midi_io
