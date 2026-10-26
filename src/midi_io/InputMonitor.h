#pragma once

#include "midi_io/InputEvent.h"

#include <bitset>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace pianoassist::midi_io
{

/** Live input state for the monitor screen and, later, the gate matcher.
    Runs on the control thread; consumes events drained from the queue. */
class InputMonitor
{
public:
    struct LastNote
    {
        std::uint8_t pitch = 0;
        std::uint8_t velocity = 0;
        std::int64_t timestampMicros = 0;
    };

    struct Counters
    {
        std::uint64_t noteOns = 0;
        std::uint64_t noteOffs = 0;
        std::uint64_t sustainChanges = 0;
        std::uint64_t otherMessages = 0;
        std::uint64_t duplicateNoteOns = 0;  ///< note-on for a pitch already held
        std::uint64_t orphanNoteOffs = 0;    ///< note-off for a pitch not held
        std::uint64_t outOfOrderEvents = 0;  ///< timestamp/sequence went backwards
    };

    void apply (const InputEvent& event);

    /** Device lost or switched: forget held notes and pedal so nothing stays
        stuck. Returns the pitches that were still held. */
    std::vector<std::uint8_t> releaseAll();

    bool isHeld (std::uint8_t pitch) const noexcept { return pitch < 128 && held_.test (pitch); }
    std::vector<std::uint8_t> heldPitches() const;
    bool sustainDown() const noexcept { return sustainDown_; }
    const std::optional<LastNote>& lastNote() const noexcept { return lastNote_; }
    const Counters& counters() const noexcept { return counters_; }

    /** Events received in the window ending at nowMicros. */
    double eventsPerSecond (std::int64_t nowMicros, std::int64_t windowMicros = 1'000'000) const;

private:
    std::bitset<128> held_;
    bool sustainDown_ = false;
    std::optional<LastNote> lastNote_;
    Counters counters_;
    std::optional<std::pair<std::int64_t, std::uint64_t>> lastOrder_;
    std::deque<std::int64_t> recentTimestamps_;
};

} // namespace pianoassist::midi_io
