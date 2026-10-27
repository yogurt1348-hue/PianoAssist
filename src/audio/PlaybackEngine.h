#pragma once

#include "audio/IInstrumentRenderer.h"
#include "audio/InstrumentRouter.h"
#include "audio/PlaybackEvents.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

namespace pianoassist::audio
{

enum class PlaybackState : std::uint8_t
{
    stopped,
    playing,
    paused,
    completed,
    waitingForGate, ///< held at a gate boundary until the learner plays it
};

const char* toString (PlaybackState state) noexcept;

/** Commands the control thread sends to the audio thread. */
struct PlaybackCommand
{
    enum class Type : std::uint8_t
    {
        setEvents, ///< list pointer; stops playback
        replaceEvents, ///< list pointer for the same song; keeps position, state and the notes still in it
        play,
        pause,
        stop,      ///< back to zero
        seek,      ///< value = seconds
        silence,   ///< panic: release and reset voices now
        setGain,   ///< value = linear gain
        setGate,   ///< value = boundary seconds, negative = no gate; releases a hold when moved past it
        preview,   ///< value = pitch + 1 for note-on, -(pitch + 1) for note-off; independent of the song
        setRate,   ///< value = song seconds per real second (tempo), clamped to [minRate, maxRate]
    };

    /** MIDI channel (0-based) for previewed keys, kept apart from song notes. */
    static constexpr std::uint8_t previewChannel = 15;

    Type type = Type::stop;
    double value = 0.0;
    const PlaybackEventList* events = nullptr;
};

/** Audio-thread half of playback. Advances the song position by the number of
    rendered frames (no wall clock, so no drift), schedules the events of each
    block at sample offsets and owns the notes it started, so pause, stop and
    seek can release exactly those. Never allocates, locks or does I/O.

    Pause/stop/seek send note-offs and sustain-off for transport-owned notes:
    voices then release naturally, so sound may ring out. Only silence()
    resets voices immediately. */
class PlaybackEngine
{
public:
    static constexpr std::size_t maxEventsPerBlock = 2048;
    static constexpr double minRate = 0.25;
    static constexpr double maxRate = 2.0;

    void prepare (double sampleRate) noexcept;
    /** Queues each channel's program for the next block; call after the
        instrument changes. */
    void queuePrograms() noexcept;

    void handle (const PlaybackCommand& command, IInstrumentRenderer& renderer) noexcept;
    /** Releases the notes and pedal of these lanes (part lanes, then 16
        channel lanes); call before their sound changes, then render the
        pending events through the old routing (process with zero frames). */
    void releaseLanes (const std::bitset<lane::maxParts + 16>& slots) noexcept;
    /** Releases what the engine plays on lanes whose instrument differs
        between the two routings. The releases are marked to go to the
        instrument of the previous routing (see InstrumentRouter::render). */
    void releaseRerouted (const InstrumentRouting& before, const InstrumentRouting& after) noexcept;
    void process (IInstrumentRenderer& renderer, RenderBlock block) noexcept;

    PlaybackState state() const noexcept { return state_; }
    double positionSeconds() const noexcept;
    std::uint64_t activeListId() const noexcept { return events_ != nullptr ? events_->id : 0; }
    std::size_t soundingNoteCount() const noexcept;
    std::size_t droppedEventCount() const noexcept { return dropped_; }
    /** Increments every time playback arrives at a gate and holds. */
    std::uint64_t gateArrivals() const noexcept { return gateArrivals_; }
    /** Tempo: song seconds per real second (1 = as written). */
    double rate() const noexcept { return rate_; }

private:
    void releaseOwned (int sampleOffset) noexcept;
    void releaseSlot (std::size_t slot, int sampleOffset, bool viaPrevious = false) noexcept;
    static std::size_t slotOf (std::uint8_t laneId, std::uint8_t channel) noexcept;
    static std::uint8_t laneOfSlot (std::size_t slot) noexcept;
    void moveTo (double seconds) noexcept;
    void restorePedal() noexcept;
    void replaceEvents (const PlaybackEventList* events) noexcept;
    bool queueEvent (const ScheduledMidiEvent& event) noexcept;

    const PlaybackEventList* events_ = nullptr;
    double sampleRate_ = 48000.0;
    double rate_ = 1.0;
    PlaybackState state_ = PlaybackState::stopped;

    double anchorSeconds_ = 0.0;      ///< song position when anchorFrames_ was zero
    std::int64_t framesSinceAnchor_ = 0;
    std::size_t cursor_ = 0;          ///< next event to schedule

    double gateSeconds_ = -1.0; ///< hold before events at this time; negative = none
    std::uint64_t gateArrivals_ = 0;

    // Notes and pedal this engine started, per part lane (0..63) and per
    // channel for events without a part (64..79).
    static constexpr std::size_t slotCount = lane::maxParts + 16;
    std::array<std::bitset<128>, slotCount> owned_ {};
    std::bitset<slotCount> sustainOwned_ {};
    std::array<std::uint8_t, slotCount> slotChannel_ {};

    std::array<ScheduledMidiEvent, maxEventsPerBlock> pending_ {};
    std::size_t pendingCount_ = 0;
    std::size_t dropped_ = 0;
};

} // namespace pianoassist::audio
