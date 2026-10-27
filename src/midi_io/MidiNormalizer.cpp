#include "midi_io/MidiNormalizer.h"

namespace pianoassist::midi_io
{

std::optional<InputEvent> normalizeMidiMessage (const std::uint8_t* bytes, std::size_t size,
                                                std::int64_t timestampMicros, std::uint64_t sequence) noexcept
{
    if (bytes == nullptr || size == 0)
        return std::nullopt;

    InputEvent event;
    event.timestampMicros = timestampMicros;
    event.sequence = sequence;

    const std::uint8_t status = bytes[0];
    const std::uint8_t kind = status & 0xF0u;
    event.channel = status & 0x0Fu;

    const bool isNote = kind == 0x80u || kind == 0x90u;
    const bool isController = kind == 0xB0u;
    if ((isNote || isController) && size < 3)
        return std::nullopt;

    if (isNote)
    {
        event.data1 = bytes[1] & 0x7Fu;
        event.data2 = bytes[2] & 0x7Fu;
        event.type = (kind == 0x90u && event.data2 > 0) ? InputEventType::noteOn : InputEventType::noteOff;
        return event;
    }

    if (isController && (bytes[1] & 0x7Fu) == 64)
    {
        event.type = InputEventType::sustain;
        event.data1 = 64;
        event.data2 = bytes[2] & 0x7Fu;
        return event;
    }

    event.type = InputEventType::other;
    event.channel = 0;
    event.data1 = status;
    event.data2 = size > 1 ? (bytes[1] & 0x7Fu) : 0;
    return event;
}

} // namespace pianoassist::midi_io
