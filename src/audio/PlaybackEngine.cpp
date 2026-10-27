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
    return anchorSeconds_ + static_cast<double> (framesSinceAnchor_) / sampleRate_;
}

std::size_t PlaybackEngine::soundingNoteCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& channel : owned_)
        count += channel.count();
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

void PlaybackEngine::releaseOwned (int sampleOffset) noexcept
{
    for (std::uint8_t channel = 0; channel < 16; ++channel)
    {
        for (std::uint8_t pitch = 0; pitch < 128; ++pitch)
            if (owned_[channel].test (pitch))
                queueEvent ({ sampleOffset, static_cast<std::uint8_t> (0x80u | channel), pitch, 64 });
        owned_[channel].reset();
        if (sustainOwned_.test (channel))
            queueEvent ({ sampleOffset, static_cast<std::uint8_t> (0xB0u | channel), 64, 0 });
    }
    sustainOwned_.reset();
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
            break;

        case Type::play:
            if (events_ == nullptr)
                break;
            if (state_ == PlaybackState::completed)
                moveTo (0.0);
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
            break;

        case Type::seek:
        {
            releaseOwned (0);
            const double limit = events_ != nullptr ? events_->durationSeconds : 0.0;
            moveTo (std::clamp (command.value, 0.0, limit));
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
                          static_cast<std::uint8_t> (on ? 90 : 64) });
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
            return static_cast<std::int64_t> (std::llround ((seconds - anchorSeconds_) * sampleRate_));
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
            const auto channel = e.status & 0x0Fu;
            if (kind == 0x90u)
            {
                if (owned_[channel].test (e.data1))
                    queueEvent ({ offset, static_cast<std::uint8_t> (0x80u | channel), e.data1, 64 });
                owned_[channel].set (e.data1);
            }
            else if (kind == 0x80u)
            {
                if (! owned_[channel].test (e.data1))
                    continue; // started before a seek point: nothing to release
                owned_[channel].reset (e.data1);
            }
            else if (kind == 0xB0u && e.data1 == 64)
            {
                sustainOwned_.set (channel, e.data2 >= 64);
            }
            queueEvent ({ offset, e.status, e.data1, e.data2 });
        }

        framesSinceAnchor_ += framesToPlay;
        if (reachesGate)
        {
            // Hold exactly at the boundary. Owned notes and pedal are released
            // so voices decay naturally; future events are not scheduled.
            releaseOwned (static_cast<int> (std::min<std::int64_t> (framesToPlay, block.numFrames - 1)));
            anchorSeconds_ = gateSeconds_;
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
