#include "audio/PlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace pianoassist::audio
{

const char* toString (PlaybackState state) noexcept
{
    switch (state)
    {
        case PlaybackState::stopped:   return "stopped";
        case PlaybackState::playing:   return "playing";
        case PlaybackState::paused:    return "paused";
        case PlaybackState::completed: return "completed";
        case PlaybackState::waitingForGate: return "waiting for gate";
    }
    return "stopped";
}

void PlaybackEngine::prepare (double sampleRate) noexcept
{
    // Keep the song position across device restarts.
    const auto position = positionSeconds();
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    anchorSeconds_ = position;
    framesSinceAnchor_ = 0;
}

double PlaybackEngine::positionSeconds() const noexcept
{
    return anchorSeconds_ + static_cast<double> (framesSinceAnchor_) * rate_ / sampleRate_;
}

std::size_t PlaybackEngine::soundingNoteCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& slot : owned_)
        count += slot.count();
    return count;
}

bool PlaybackEngine::queueEvent (const ScheduledMidiEvent& event) noexcept
{
    if (pendingCount_ == pending_.size())
    {
        ++dropped_;
        return false;
    }
    pending_[pendingCount_++] = event;
    return true;
}

std::size_t PlaybackEngine::slotOf (std::uint8_t laneId, std::uint8_t channel) noexcept
{
    return laneId < lane::maxParts ? laneId : lane::maxParts + (channel & 0x0Fu);
}

std::uint8_t PlaybackEngine::laneOfSlot (std::size_t slot) noexcept
{
    return slot < lane::maxParts ? static_cast<std::uint8_t> (slot) : lane::byChannel;
}

void PlaybackEngine::releaseSlot (std::size_t slot, int sampleOffset, bool viaPrevious) noexcept
{
    const auto channel = slotChannel_[slot];
    const auto laneId = laneOfSlot (slot);
    for (std::uint8_t pitch = 0; pitch < 128; ++pitch)
        if (owned_[slot].test (pitch))
            queueEvent ({ sampleOffset, static_cast<std::uint8_t> (0x80u | channel), pitch, 64, laneId, viaPrevious });
    owned_[slot].reset();
    if (sustainOwned_.test (slot))
        queueEvent ({ sampleOffset, static_cast<std::uint8_t> (0xB0u | channel), 64, 0, laneId, viaPrevious });
    sustainOwned_.reset (slot);
}

void PlaybackEngine::releaseOwned (int sampleOffset) noexcept
{
    for (std::size_t slot = 0; slot < slotCount; ++slot)
        if (owned_[slot].any() || sustainOwned_.test (slot))
            releaseSlot (slot, sampleOffset);
}

void PlaybackEngine::releaseLanes (const std::bitset<lane::maxParts + 16>& slots) noexcept
{
    for (std::size_t slot = 0; slot < slotCount; ++slot)
        if (slots.test (slot) && (owned_[slot].any() || sustainOwned_.test (slot)))
            releaseSlot (slot, 0);
}

void PlaybackEngine::releaseRerouted (const InstrumentRouting& before, const InstrumentRouting& after) noexcept
{
    for (std::size_t slot = 0; slot < slotCount; ++slot)
    {
        if (owned_[slot].none() && ! sustainOwned_.test (slot))
            continue;
        const auto laneId = laneOfSlot (slot);
        const int channel = slot < lane::maxParts ? slotChannel_[slot] : static_cast<int> (slot - lane::maxParts);
        if (before.forEvent (laneId, channel) != after.forEvent (laneId, channel))
            releaseSlot (slot, 0, true);
    }
}

void PlaybackEngine::queuePrograms() noexcept
{
    // Each channel and each part gets the sound the song has at the playhead:
    // the last program change (and bank) before it, or the part's first one.
    if (events_ == nullptr)
        return;
    for (std::uint8_t channel = 0; channel < 16; ++channel)
        if (const auto program = events_->programs[channel]; program >= 0)
            queueEvent ({ 0, static_cast<std::uint8_t> (0xC0u | channel), static_cast<std::uint8_t> (program), 0, lane::byChannel });

    std::array<LaneStart, lane::maxParts> now = events_->lanes;
    std::bitset<lane::maxParts> known;
    const auto& list = events_->events;
    for (auto i = std::min (cursor_, list.size()); i > 0 && ! known.all(); --i)
    {
        const auto& e = list[i - 1];
        if (e.lane >= lane::maxParts || (e.status & 0xF0u) != 0xC0u || known.test (e.lane))
            continue;
        known.set (e.lane);
        now[e.lane].program = e.data1;
        now[e.lane].bank = -1;
        // Its bank selects come right before it, at the same time.
        int msb = -1, lsb = -1;
        for (auto j = i - 1; j > 0; --j)
        {
            const auto& b = list[j - 1];
            if (b.seconds != e.seconds)
                break;
            if (b.lane == e.lane && (b.status & 0xF0u) == 0xB0u && (b.data1 == 0 || b.data1 == 32))
                (b.data1 == 0 ? msb : lsb) = b.data2;
        }
        if (msb >= 0)
            now[e.lane].bank = static_cast<std::int16_t> (msb * 128 + std::max (lsb, 0));
    }
    for (std::size_t l = 0; l < lane::maxParts; ++l)
    {
        const auto& start = now[l];
        if (start.program < 0)
            continue;
        const auto laneId = static_cast<std::uint8_t> (l);
        const auto channel = static_cast<std::uint8_t> (start.channel & 0x0Fu);
        if (start.bank >= 0)
        {
            queueEvent ({ 0, static_cast<std::uint8_t> (0xB0u | channel), 0, static_cast<std::uint8_t> ((start.bank >> 7) & 0x7F), laneId });
            queueEvent ({ 0, static_cast<std::uint8_t> (0xB0u | channel), 32, static_cast<std::uint8_t> (start.bank & 0x7F), laneId });
        }
        queueEvent ({ 0, static_cast<std::uint8_t> (0xC0u | channel), static_cast<std::uint8_t> (start.program), 0, laneId });
    }
}

void PlaybackEngine::moveTo (double seconds) noexcept
{
    anchorSeconds_ = std::max (0.0, seconds);
    framesSinceAnchor_ = 0;
    cursor_ = 0;
    if (events_ != nullptr)
    {
        const auto& list = events_->events;
        cursor_ = static_cast<std::size_t> (
            std::lower_bound (list.begin(), list.end(), anchorSeconds_,
                              [] (const PlaybackEvent& e, double t) { return e.seconds < t; })
            - list.begin());
    }
}

void PlaybackEngine::replaceEvents (const PlaybackEventList* events) noexcept
{
    // The same song with other parts switched on or off (mute, solo, practice
    // mode): playback goes on from the same position. A sounding note stays
    // when the new list still ends it later; otherwise it is released now, so
    // switching a track off neither leaves hanging notes nor cuts the others.
    if (events == nullptr || events_ == nullptr)
    {
        releaseOwned (0);
        events_ = events;
        moveTo (positionSeconds());
        return;
    }
    events_ = events;
    const auto& list = events_->events;
    const auto position = positionSeconds();
    cursor_ = static_cast<std::size_t> (std::lower_bound (list.begin(), list.end(), position,
                                                          [] (const PlaybackEvent& e, double t) { return e.seconds < t; })
                                        - list.begin());
    constexpr std::size_t lookAhead = 8192; // bounded work on the audio thread
    for (std::size_t slot = 0; slot < slotCount; ++slot)
    {
        if (owned_[slot].none())
            continue;
        const auto channel = slotChannel_[slot];
        for (std::uint8_t pitch = 0; pitch < 128; ++pitch)
        {
            if (! owned_[slot].test (pitch))
                continue;
            bool continues = false;
            const auto end = std::min (list.size(), cursor_ + lookAhead);
            for (auto i = cursor_; i < end; ++i)
            {
                const auto& e = list[i];
                if ((e.status & 0x0Fu) != channel || e.data1 != pitch || slotOf (e.lane, channel) != slot)
                    continue;
                const auto kind = e.status & 0xF0u;
                if (kind == 0x80u || (kind == 0x90u && e.data2 == 0))
                {
                    continues = true;
                    break;
                }
                if (kind == 0x90u)
                    break;
            }
            if (! continues)
            {
                queueEvent ({ 0, static_cast<std::uint8_t> (0x80u | channel), pitch, 64, laneOfSlot (slot) });
                owned_[slot].reset (pitch);
            }
        }
    }
    // The pedal follows the new list at this position.
    for (std::size_t slot = 0; slot < slotCount; ++slot)
    {
        if (! sustainOwned_.test (slot))
            continue;
        const auto channel = slotChannel_[slot];
        bool down = false;
        for (auto i = cursor_; i > 0; --i)
        {
            const auto& e = list[i - 1];
            if ((e.status & 0x0Fu) == channel && (e.status & 0xF0u) == 0xB0u && e.data1 == 64 && slotOf (e.lane, channel) == slot)
            {
                down = e.data2 >= 64;
                break;
            }
        }
        if (! down)
        {
            queueEvent ({ 0, static_cast<std::uint8_t> (0xB0u | channel), 64, 0, laneOfSlot (slot) });
            sustainOwned_.reset (slot);
        }
    }
    queuePrograms();
    if (state_ == PlaybackState::completed && cursor_ < list.size())
        state_ = PlaybackState::paused;
}

void PlaybackEngine::restorePedal() noexcept
{
    // After a seek or a loop jump into a pedalled passage, the pedal is down
    // as the song has it there. Notes that started before the new position
    // are not replayed. Bounded by the event list; seeks are rare.
    if (events_ == nullptr)
        return;
    const auto& list = events_->events;
    std::bitset<slotCount> known;
    for (auto i = cursor_; i > 0; --i)
    {
        const auto& e = list[i - 1];
        if ((e.status & 0xF0u) != 0xB0u || e.data1 != 64)
            continue;
        const auto channel = static_cast<std::uint8_t> (e.status & 0x0Fu);
        const auto slot = slotOf (e.lane, channel);
        if (known.test (slot))
            continue;
        known.set (slot);
        if (e.data2 >= 64)
        {
            sustainOwned_.set (slot);
            slotChannel_[slot] = channel;
            queueEvent ({ 0, e.status, 64, e.data2, e.lane });
        }
    }
    // The sound of each part at this point, too (programs may change during the song).
    queuePrograms();
}

void PlaybackEngine::handle (const PlaybackCommand& command, IInstrumentRenderer& renderer) noexcept
{
    using Type = PlaybackCommand::Type;
    switch (command.type)
    {
        case Type::setEvents:
            releaseOwned (0);
            events_ = command.events;
            state_ = PlaybackState::stopped;
            moveTo (0.0);
            queuePrograms();
            break;

        case Type::replaceEvents:
            replaceEvents (command.events);
            break;

        case Type::play:
            if (events_ == nullptr)
                break;
            if (state_ == PlaybackState::completed)
            {
                moveTo (0.0);
                queuePrograms();
            }
            state_ = PlaybackState::playing;
            break;

        case Type::pause:
            if (state_ == PlaybackState::playing || state_ == PlaybackState::waitingForGate)
            {
                releaseOwned (0);
                state_ = PlaybackState::paused;
            }
            break;

        case Type::stop:
            releaseOwned (0);
            state_ = PlaybackState::stopped;
            moveTo (0.0);
            queuePrograms();
            break;

        case Type::seek:
        {
            releaseOwned (0);
            const double limit = events_ != nullptr ? events_->durationSeconds : 0.0;
            moveTo (std::clamp (command.value, 0.0, limit));
            restorePedal();
            if (state_ == PlaybackState::completed || state_ == PlaybackState::waitingForGate)
                state_ = PlaybackState::paused;
            break;
        }

        case Type::silence:
            releaseOwned (0);
            renderer.silence();
            if (state_ == PlaybackState::playing)
                state_ = PlaybackState::paused;
            break;

        case Type::setGain:
            break; // applied by the audio callback owner

        case Type::preview:
        {
            const auto pitch = static_cast<std::uint8_t> (std::clamp (std::abs (command.value) - 1.0, 0.0, 127.0));
            const bool on = command.value > 0.0;
            queueEvent ({ 0, static_cast<std::uint8_t> ((on ? 0x90u : 0x80u) | PlaybackCommand::previewChannel), pitch,
                          static_cast<std::uint8_t> (on ? 90 : 64), lane::preview });
            break;
        }

        case Type::setRate:
        {
            // Re-anchor at the current position so time already played is kept.
            anchorSeconds_ = positionSeconds();
            framesSinceAnchor_ = 0;
            rate_ = std::clamp (std::isfinite (command.value) ? command.value : 1.0, minRate, maxRate);
            break;
        }

        case Type::setGate:
            gateSeconds_ = command.value;
            if (state_ == PlaybackState::waitingForGate
                && (gateSeconds_ < 0.0 || gateSeconds_ > positionSeconds() + 1.0e-9))
                state_ = PlaybackState::playing; // gate passed: resume from the boundary
            break;
    }
}

void PlaybackEngine::process (IInstrumentRenderer& renderer, RenderBlock block) noexcept
{
    if (state_ == PlaybackState::playing && events_ != nullptr && block.numFrames > 0)
    {
        const auto& list = events_->events;
        // Frames are counted from the anchor and rounded to the nearest
        // sample, so scheduling is identical for every buffer size.
        const auto frameOf = [this] (double seconds) {
            return static_cast<std::int64_t> (std::llround ((seconds - anchorSeconds_) * sampleRate_ / rate_));
        };

        // A gate inside this block cuts it short: events at the gate itself
        // wait until the learner passes it.
        std::int64_t framesToPlay = block.numFrames;
        bool reachesGate = false;
        if (gateSeconds_ >= 0.0)
        {
            const auto gateFrame = std::max (frameOf (gateSeconds_), framesSinceAnchor_);
            if (gateFrame < framesSinceAnchor_ + block.numFrames)
            {
                framesToPlay = gateFrame - framesSinceAnchor_;
                reachesGate = true;
            }
        }
        const auto blockEndFrame = framesSinceAnchor_ + framesToPlay;

        while (cursor_ < list.size() && frameOf (list[cursor_].seconds) < blockEndFrame)
        {
            const auto& e = list[cursor_++];
            const auto offset = static_cast<int> (std::clamp<std::int64_t> (frameOf (e.seconds) - framesSinceAnchor_, 0,
                                                                            block.numFrames - 1));
            const auto kind = e.status & 0xF0u;
            const auto channel = static_cast<std::uint8_t> (e.status & 0x0Fu);
            const auto slot = slotOf (e.lane, channel);
            if (kind == 0x90u)
            {
                if (owned_[slot].test (e.data1))
                    queueEvent ({ offset, static_cast<std::uint8_t> (0x80u | channel), e.data1, 64, e.lane });
                owned_[slot].set (e.data1);
                slotChannel_[slot] = channel;
            }
            else if (kind == 0x80u)
            {
                if (! owned_[slot].test (e.data1))
                    continue; // started before a seek point: nothing to release
                owned_[slot].reset (e.data1);
            }
            else if (kind == 0xB0u && e.data1 == 64)
            {
                sustainOwned_.set (slot, e.data2 >= 64);
                slotChannel_[slot] = channel;
            }
            queueEvent ({ offset, e.status, e.data1, e.data2, e.lane });
        }

        framesSinceAnchor_ += framesToPlay;
        if (reachesGate)
        {
            // Hold exactly at the boundary. Owned notes and pedal are released
            // so voices decay naturally; future events are not scheduled.
            releaseOwned (static_cast<int> (std::min<std::int64_t> (framesToPlay, block.numFrames - 1)));
            // A boundary already behind the playhead (a fast passage where the
            // next gate's window ended before the previous gate was passed)
            // holds where the song is; it never jumps back.
            anchorSeconds_ = std::max (gateSeconds_, positionSeconds());
            framesSinceAnchor_ = 0;
            state_ = PlaybackState::waitingForGate;
            ++gateArrivals_;
        }
        else if (positionSeconds() >= events_->durationSeconds && cursor_ >= list.size())
        {
            releaseOwned (block.numFrames - 1);
            state_ = PlaybackState::completed;
            moveTo (events_->durationSeconds);
        }
    }

    renderer.render (std::span<const ScheduledMidiEvent> (pending_.data(), pendingCount_), block);
    pendingCount_ = 0;
}

} // namespace pianoassist::audio
