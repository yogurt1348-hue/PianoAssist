#include "app/AudioService.h"

#include <algorithm>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;
}

/** What the playback engine renders through: the current routing. */
class AudioService::RoutedRenderer final : public audio::IInstrumentRenderer
{
public:
    explicit RoutedRenderer (AudioService& owner) : owner_ (owner) {}
    void prepare (double, int) override {}
    void render (std::span<const audio::ScheduledMidiEvent> events, audio::RenderBlock block) noexcept override
    {
        for (const auto& e : events)
            if ((e.status & 0xf0) == 0x90 && e.data2 > 0)
                owner_.channelNotes_[e.status & 0x0f].fetch_add (1, std::memory_order_relaxed);
        owner_.router_.render (*owner_.lastRouting_, events, block, owner_.previousRouting_);
    }
    void allNotesOff() noexcept override { forEach ([] (auto& r) { r.allNotesOff(); }); }
    void silence() noexcept override { forEach ([] (auto& r) { r.silence(); }); }
    int reportedLatencyFrames() const noexcept override { return owner_.lastRouting_->main->reportedLatencyFrames(); }
    int activeVoiceCount() const noexcept override { return audio::InstrumentRouter::activeVoices (*owner_.lastRouting_); }

private:
    template <typename F>
    void forEach (F&& f) noexcept
    {
        const auto& routing = *owner_.lastRouting_;
        f (*routing.main);
        for (auto* r : routing.channels)
            if (r != nullptr && r != routing.main)
                f (*r); // a renderer on several channels is called more than once: harmless
        for (auto* r : routing.parts)
            if (r != nullptr && r != routing.main)
                f (*r);
    }

    AudioService& owner_;
};

AudioService::AudioService (diagnostics::RotatingLog& log)
    : log_ (log),
      routing_ (std::make_unique<audio::InstrumentRouting>()),
      routed_ (std::make_unique<RoutedRenderer> (*this))
{
    routing_->main = &synth_;
    activeRouting_.store (routing_.get());
    lastRouting_ = routing_.get();
}

AudioService::~AudioService()
{
    deviceManager_.removeChangeListener (this);
    deviceManager_.removeAudioCallback (this);
    deviceManager_.closeAudioDevice();
}

std::string AudioService::start (const std::string& savedStateXml)
{
    log_.write (LogLevel::info, "audio", savedStateXml.empty() ? "Opening default audio output" : "Opening saved audio output");
    auto saved = savedStateXml.empty() ? nullptr : juce::parseXML (juce::String::fromUTF8 (savedStateXml.c_str()));
    auto error = deviceManager_.initialise (0, 2, saved.get(), true);
    if (error.isNotEmpty() && saved != nullptr)
    {
        log_.write (LogLevel::warning, "audio", "Saved audio output unavailable; using the default", { { "error", error.toStdString() } });
        error = deviceManager_.initialiseWithDefaultDevices (0, 2);
    }
    if (error.isNotEmpty())
    {
        log_.write (LogLevel::error, "audio", "Could not open audio output", { { "error", error.toStdString() } });
        return error.toStdString();
    }
    if (deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        log_.write (LogLevel::error, "audio", "No audio output device found");
        return juce::translate ("No audio output device found.").toStdString();
    }
    deviceManager_.addChangeListener (this);
    deviceManager_.addAudioCallback (this);
    log_.write (LogLevel::info, "audio", "Audio output ready", { { "device", deviceDescription() } });
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        juce::StringArray sizes, rates;
        for (auto size : device->getAvailableBufferSizes())
            sizes.add (juce::String (size));
        for (auto rate : device->getAvailableSampleRates())
            rates.add (juce::String (static_cast<int> (rate)));
        log_.write (LogLevel::info, "audio", "Audio output options",
                    { { "bufferSizes", sizes.joinIntoString (" ").toStdString() },
                      { "sampleRates", rates.joinIntoString (" ").toStdString() },
                      { "devices", availableDevices() } });
    }
    return {};
}

std::string AudioService::deviceStateXml() const
{
    if (auto xml = deviceManager_.createStateXml())
        return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()).toStdString();
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

void AudioService::previewKey (std::uint8_t pitch, bool down)
{
    const double value = static_cast<double> (pitch) + 1.0;
    send ({ audio::PlaybackCommand::Type::preview, down ? value : -value }, "preview");
}

void AudioService::loadSong (const timeline::Song& song, const std::function<bool (const timeline::Note&)>& include,
                             const std::function<double (const timeline::Note&)>& gain, bool keepPlaying)
{
    std::scoped_lock lock (listsMutex_);
    auto list = std::make_shared<const audio::PlaybackEventList> (audio::buildPlaybackEvents (song, nextListId_++, include, gain));
    if (current_)
        retired_.push_back (current_);
    current_ = list;
    send ({ keepPlaying ? audio::PlaybackCommand::Type::replaceEvents : audio::PlaybackCommand::Type::setEvents, 0.0, current_.get() },
          "load");
    log_.write (LogLevel::info, "audio", "Playback events prepared",
                { { "events", std::to_string (list->events.size()) }, { "listId", std::to_string (list->id) } });
}

void AudioService::play() { send ({ audio::PlaybackCommand::Type::play }, "play"); }
void AudioService::pause() { send ({ audio::PlaybackCommand::Type::pause }, "pause"); }
void AudioService::stop() { send ({ audio::PlaybackCommand::Type::stop }, "stop"); }
void AudioService::seek (double seconds) { send ({ audio::PlaybackCommand::Type::seek, seconds }, "seek"); }
void AudioService::silence() { send ({ audio::PlaybackCommand::Type::silence }, "silence"); }
void AudioService::setRate (double rate) { send ({ audio::PlaybackCommand::Type::setRate, rate }, "rate"); }
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
        s.rate = rate_.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (blockSeq_.load (std::memory_order_relaxed) == before)
            break;
    }
    return s;
}

std::pair<double, int> AudioService::renderFormat() const
{
    return { deviceRate_.load(), deviceBlock_.load() };
}

int AudioService::addInstrument (std::unique_ptr<audio::IInstrumentRenderer> instrument)
{
    if (! instrument)
        return builtInHandle;
    // Prepared under the lock: a device restart (new sample rate) either
    // happens before and is read here, or after and re-prepares it.
    std::scoped_lock lock (instrumentsMutex_);
    const auto [rate, block] = renderFormat();
    instrument->prepare (rate, block);
    const int handle = nextHandle_++;
    owned_.emplace (handle, std::move (instrument));
    return handle;
}

void AudioService::removeInstrument (int handle)
{
    std::scoped_lock lock (instrumentsMutex_);
    if (const auto it = owned_.find (handle); it != owned_.end())
    {
        jassert (! routing_->uses (it->second.get()));
        retiredInstruments_.emplace_back (std::move (it->second), blocksRendered_.load());
        owned_.erase (it);
    }
}

audio::IInstrumentRenderer* AudioService::instrument (int handle) const
{
    if (handle == builtInHandle)
        return const_cast<audio::InternalSynthRenderer*> (&synth_);
    std::scoped_lock lock (instrumentsMutex_);
    const auto it = owned_.find (handle);
    return it != owned_.end() ? it->second.get() : nullptr;
}

void AudioService::setRouting (int mainHandle, const std::array<int, 16>& channelHandles, const std::vector<int>& partHandles)
{
    auto next = std::make_unique<audio::InstrumentRouting>();
    next->main = instrument (mainHandle);
    if (next->main == nullptr)
        next->main = &synth_;
    std::string routed;
    for (std::size_t ch = 0; ch < 16; ++ch)
        if (channelHandles[ch] >= 0)
            if (auto* r = instrument (channelHandles[ch]); r != nullptr && r != next->main)
            {
                next->channels[ch] = r;
                routed += (routed.empty() ? "" : " ") + std::to_string (ch + 1);
            }
    std::string parts;
    for (std::size_t p = 0; p < partHandles.size() && p < audio::lane::maxParts; ++p)
        if (partHandles[p] >= 0)
            if (auto* r = instrument (partHandles[p]); r != nullptr)
            {
                next->parts[p] = r;
                parts += (parts.empty() ? "" : " ") + std::to_string (p + 1);
            }
    activeRouting_.store (next.get(), std::memory_order_release);
    retiredRoutings_.emplace_back (std::move (routing_), blocksRendered_.load());
    routing_ = std::move (next);
    const auto [rate, block] = renderFormat();
    log_.write (LogLevel::info, "audio", "Instruments routed",
                { { "main", std::to_string (mainHandle) }, { "ownChannels", routed.empty() ? "(none)" : routed },
                  { "ownParts", parts.empty() ? "(none)" : parts },
                  { "latencyFrames", std::to_string (routing_->main->reportedLatencyFrames()) },
                  { "rate", std::to_string (static_cast<int> (rate)) }, { "block", std::to_string (block) } });
}

void AudioService::collectGarbage()
{
    {
        // An instrument retired at block N may still be in use during N and
        // gets its silence() call in N + 1; after that nobody touches it.
        const auto blocks = blocksRendered_.load();
        const bool deviceRunning = deviceOpen_.load();
        std::erase_if (retiredRoutings_, [blocks, deviceRunning] (const auto& entry) {
            return ! deviceRunning || blocks >= entry.second + 2;
        });
        std::scoped_lock lock (instrumentsMutex_);
        std::erase_if (retiredInstruments_, [blocks, deviceRunning] (const auto& entry) {
            return ! deviceRunning || blocks >= entry.second + 2;
        });
    }
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
    const auto block = device->getCurrentBufferSizeSamples();
    deviceRate_.store (rate);
    deviceBlock_.store (block);
    synth_.prepare (rate, block);
    {
        std::scoped_lock lock (instrumentsMutex_);
        for (auto& [handle, instrument] : owned_)
            instrument->prepare (rate, block);
    }
    lastRouting_ = activeRouting_.load(); // the previous one may already be deleted
    previousRouting_ = nullptr;
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
    const auto* routing = activeRouting_.load (std::memory_order_acquire);
    if (routing != lastRouting_)
    {
        // New routing: instruments no longer used fall silent, the others
        // learn the channel programs. Notes they were playing are dropped.
        // Parts whose instrument changed release their notes on the old one
        // (this block only), so a sound change never leaves a hanging note
        // and never cuts the notes of other parts.
        audio::InstrumentRouter::silenceDropped (*lastRouting_, *routing);
        engine_.releaseRerouted (*lastRouting_, *routing);
        previousRouting_ = lastRouting_;
        lastRouting_ = routing;
        engine_.queuePrograms();
    }
    else
        previousRouting_ = nullptr;
    if (lastCallbackMicros_ > 0)
    {
        const auto gap = blockStart - lastCallbackMicros_;
        if (gap > healthMaxGapMicros_.load (std::memory_order_relaxed))
            healthMaxGapMicros_.store (gap, std::memory_order_relaxed);
    }
    lastCallbackMicros_ = blockStart;
    auto* instrument = routed_.get();
    while (const auto command = commands_.pop())
    {
        if (command->type == audio::PlaybackCommand::Type::preview && command->value > 0.0)
            lastPreviewMicros_.store (blockStart, std::memory_order_release);
        engine_.handle (*command, *instrument);
    }

    const auto startPosition = engine_.positionSeconds();
    engine_.process (*instrument, { outputs, numOutputs, numSamples });
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
    rate_.store (engine_.rate(), std::memory_order_relaxed);
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
    activeVoices_.store (instrument->activeVoiceCount(), std::memory_order_relaxed);
    blocksRendered_.fetch_add (1, std::memory_order_release);

    const auto took = nowMicros() - blockStart;
    healthCallbacks_.fetch_add (1, std::memory_order_relaxed);
    healthMicrosSum_.fetch_add (static_cast<std::uint64_t> (std::max<std::int64_t> (took, 0)), std::memory_order_relaxed);
    if (took > healthMaxMicros_.load (std::memory_order_relaxed))
        healthMaxMicros_.store (took, std::memory_order_relaxed);
    const auto rate = deviceRate_.load (std::memory_order_relaxed);
    if (rate > 0.0 && static_cast<double> (took) > numSamples * 1.0e6 / rate)
        healthOverBudget_.fetch_add (1, std::memory_order_relaxed);
}

AudioService::Health AudioService::takeHealth()
{
    Health h;
    h.callbacks = healthCallbacks_.exchange (0, std::memory_order_relaxed);
    const auto sum = healthMicrosSum_.exchange (0, std::memory_order_relaxed);
    h.avgCallbackMs = h.callbacks > 0 ? static_cast<double> (sum) / static_cast<double> (h.callbacks) / 1000.0 : 0.0;
    h.maxCallbackMs = static_cast<double> (healthMaxMicros_.exchange (0, std::memory_order_relaxed)) / 1000.0;
    h.overBudget = healthOverBudget_.exchange (0, std::memory_order_relaxed);
    h.maxGapMs = static_cast<double> (healthMaxGapMicros_.exchange (0, std::memory_order_relaxed)) / 1000.0;
    const auto rate = deviceRate_.load();
    h.budgetMs = rate > 0.0 ? deviceBlock_.load() * 1000.0 / rate : 0.0;
    for (std::size_t ch = 0; ch < 16; ++ch)
        h.channelNotes[ch] = channelNotes_[ch].exchange (0, std::memory_order_relaxed);
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        h.xruns = device->getXRunCount();
    return h;
}

double AudioService::reportedOutputLatencyMs() const
{
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr || device->getCurrentSampleRate() <= 0.0)
        return 0.0;
    const auto frames = device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples();
    return frames * 1000.0 / device->getCurrentSampleRate();
}

std::string AudioService::availableDevices()
{
    std::string text;
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        text += (text.empty() ? "" : "; ") + type->getTypeName().toStdString() + ": "
                + type->getDeviceNames (false).joinIntoString (", ").toStdString();
    }
    return text;
}

} // namespace pianoassist::app
