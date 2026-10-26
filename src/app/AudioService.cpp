#include "app/AudioService.h"

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;
}

AudioService::AudioService (diagnostics::RotatingLog& log) : log_ (log) {}

AudioService::~AudioService()
{
    deviceManager_.removeChangeListener (this);
    deviceManager_.removeAudioCallback (this);
    deviceManager_.closeAudioDevice();
}

std::string AudioService::start()
{
    log_.write (LogLevel::info, "audio", "Opening default audio output");
    const auto error = deviceManager_.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty())
    {
        log_.write (LogLevel::error, "audio", "Could not open audio output", { { "error", error.toStdString() } });
        return error.toStdString();
    }
    if (deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        log_.write (LogLevel::error, "audio", "No audio output device found");
        return "no audio output device found";
    }
    deviceManager_.addChangeListener (this);
    deviceManager_.addAudioCallback (this);
    log_.write (LogLevel::info, "audio", "Audio output ready", { { "device", deviceDescription() } });
    return {};
}

std::string AudioService::deviceDescription() const
{
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
        return "(none)";
    const auto rate = device->getCurrentSampleRate();
    const auto block = device->getCurrentBufferSizeSamples();
    const auto latency = device->getOutputLatencyInSamples();
    return device->getTypeName().toStdString() + " / " + device->getName().toStdString() + " · "
           + std::to_string (static_cast<int> (rate)) + " Hz · " + std::to_string (block) + " frames ("
           + juce::String (block * 1000.0 / rate, 1).toStdString() + " ms) · output latency "
           + juce::String (latency * 1000.0 / rate, 1).toStdString() + " ms";
}

void AudioService::send (const audio::PlaybackCommand& command, const char* what)
{
    if (! commands_.push (command))
        log_.write (LogLevel::error, "audio", "Playback command queue full; command dropped", { { "command", what } });
}

std::int64_t AudioService::nowMicros() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AudioService::setGate (double boundarySeconds)
{
    send ({ audio::PlaybackCommand::Type::setGate, boundarySeconds }, "gate");
}

void AudioService::loadSong (const timeline::Song& song, const std::function<bool (timeline::NoteRole)>& include)
{
    std::scoped_lock lock (listsMutex_);
    auto list = std::make_shared<const audio::PlaybackEventList> (audio::buildPlaybackEvents (song, nextListId_++, include));
    if (current_)
        retired_.push_back (current_);
    current_ = list;
    send ({ audio::PlaybackCommand::Type::setEvents, 0.0, current_.get() }, "load");
    log_.write (LogLevel::info, "audio", "Playback events prepared",
                { { "events", std::to_string (list->events.size()) }, { "listId", std::to_string (list->id) } });
}

void AudioService::play() { send ({ audio::PlaybackCommand::Type::play }, "play"); }
void AudioService::pause() { send ({ audio::PlaybackCommand::Type::pause }, "pause"); }
void AudioService::stop() { send ({ audio::PlaybackCommand::Type::stop }, "stop"); }
void AudioService::seek (double seconds) { send ({ audio::PlaybackCommand::Type::seek, seconds }, "seek"); }
void AudioService::silence() { send ({ audio::PlaybackCommand::Type::silence }, "silence"); }
void AudioService::setGain (float linearGain) { targetGain_.store (juce::jlimit (0.0f, 1.0f, linearGain)); }

AudioService::Snapshot AudioService::snapshot() const
{
    Snapshot s;
    s.state = static_cast<audio::PlaybackState> (state_.load());
    s.positionSeconds = position_.load();
    {
        std::scoped_lock lock (listsMutex_);
        s.durationSeconds = current_ ? current_->durationSeconds : 0.0;
    }
    s.activeVoices = activeVoices_.load();
    s.deviceOpen = deviceOpen_.load();
    // Seqlock read: the block timing fields are written together by the audio
    // thread and must be read as one consistent set.
    for (;;)
    {
        const auto before = blockSeq_.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        s.blockStartPosition = blockStartPosition_.load (std::memory_order_relaxed);
        s.blockStartMicros = blockStartMicros_.load (std::memory_order_relaxed);
        s.gateArrivals = gateArrivals_.load (std::memory_order_relaxed);
        s.arrivalMicros = arrivalMicros_.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (blockSeq_.load (std::memory_order_relaxed) == before)
            break;
    }
    return s;
}

void AudioService::collectGarbage()
{
    {
        std::scoped_lock lock (listsMutex_);
        if (current_ && activeListId_.load() == current_->id)
            retired_.clear();
    }
    if (errorPending_.exchange (false))
        log_.write (LogLevel::error, "audio", "Audio device reported an error");
}

void AudioService::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called before the callback runs, so touching the engine here is safe.
    const auto rate = device->getCurrentSampleRate();
    synth_.prepare (rate, device->getCurrentBufferSizeSamples());
    engine_.prepare (rate);
    gain_.reset (rate, 0.02);
    gain_.setCurrentAndTargetValue (targetGain_.load());
    deviceOpen_.store (true);
}

void AudioService::audioDeviceStopped()
{
    deviceOpen_.store (false);
}

void AudioService::audioDeviceError (const juce::String&)
{
    errorPending_.store (true);
}

void AudioService::changeListenerCallback (juce::ChangeBroadcaster*)
{
    log_.write (LogLevel::info, "audio", "Audio device changed", { { "device", deviceDescription() } });
}

void AudioService::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                     int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Audio thread: no allocation, locks, logging or file access.
    const auto blockStart = nowMicros();
    while (const auto command = commands_.pop())
        engine_.handle (*command, synth_);

    const auto startPosition = engine_.positionSeconds();
    engine_.process (synth_, { outputs, numOutputs, numSamples });
    const auto seq = blockSeq_.load (std::memory_order_relaxed);
    blockSeq_.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    if (engine_.gateArrivals() != gateArrivals_.load (std::memory_order_relaxed))
    {
        arrivalMicros_.store (blockStart, std::memory_order_relaxed);
        gateArrivals_.store (engine_.gateArrivals(), std::memory_order_relaxed);
    }
    blockStartPosition_.store (startPosition, std::memory_order_relaxed);
    blockStartMicros_.store (blockStart, std::memory_order_relaxed);
    blockSeq_.store (seq + 2, std::memory_order_release);

    gain_.setTargetValue (targetGain_.load (std::memory_order_relaxed));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gain_.getNextValue();
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][i] *= g;
    }

    position_.store (engine_.positionSeconds(), std::memory_order_relaxed);
    state_.store (static_cast<int> (engine_.state()), std::memory_order_relaxed);
    activeListId_.store (engine_.activeListId(), std::memory_order_release);
    activeVoices_.store (synth_.activeVoiceCount(), std::memory_order_relaxed);
}

} // namespace pianoassist::app
