#pragma once

#include "audio/IInstrumentRenderer.h"
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
};

const char* toString (PlaybackState state) noexcept;

/** Commands the control thread sends to the audio thread. */
struct PlaybackCommand
{
    enum class Type : std::uint8_t
    {
        setEvents, ///< list pointer; stops playback
        play,
        pause,
        stop,      ///< back to zero
        seek,      ///< value = seconds
        silence,   ///< panic: release and reset voices now
        setGain,   ///< value = linear gain
    };

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

    void prepare (double sampleRate) noexcept;
    void handle (const PlaybackCommand& command, IInstrumentRenderer& renderer) noexcept;
    void process (IInstrumentRenderer& renderer, RenderBlock block) noexcept;

    PlaybackState state() const noexcept { return state_; }
    double positionSeconds() const noexcept;
    std::uint64_t activeListId() const noexcept { return events_ != nullptr ? events_->id : 0; }
    std::size_t soundingNoteCount() const noexcept;
    std::size_t droppedEventCount() const noexcept { return dropped_; }

private:
    void releaseOwned (int sampleOffset) noexcept;
    void moveTo (double seconds) noexcept;
    bool queueEvent (const ScheduledMidiEvent& event) noexcept;

    const PlaybackEventList* events_ = nullptr;
    double sampleRate_ = 48000.0;
    PlaybackState state_ = PlaybackState::stopped;

    double anchorSeconds_ = 0.0;      ///< song position when anchorFrames_ was zero
    std::int64_t framesSinceAnchor_ = 0;
    std::size_t cursor_ = 0;          ///< next event to schedule

    std::array<std::bitset<128>, 16> owned_ {};
    std::bitset<16> sustainOwned_ {};

    std::array<ScheduledMidiEvent, maxEventsPerBlock> pending_ {};
    std::size_t pendingCount_ = 0;
    std::size_t dropped_ = 0;
};

} // namespace pianoassist::audio
