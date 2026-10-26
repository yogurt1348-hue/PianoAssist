#pragma once

#include "audio/InternalSynthRenderer.h"
#include "audio/PlaybackEngine.h"
#include "core/SpscQueue.h"
#include "diagnostics/RotatingLog.h"
#include "timeline/Song.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>

namespace pianoassist::app
{

/** Owns the audio device, the playback engine and the internal synth. The
    control (message) thread sends commands through a wait-free queue; the
    audio callback publishes position and state through atomics. */
class AudioService final : private juce::AudioIODeviceCallback, private juce::ChangeListener
{
public:
    struct Snapshot
    {
        audio::PlaybackState state = audio::PlaybackState::stopped;
        double positionSeconds = 0.0;
        double durationSeconds = 0.0;
        int activeVoices = 0;
        bool deviceOpen = false;
    };

    explicit AudioService (diagnostics::RotatingLog& log);
    ~AudioService() override;

    /** Opens the default output device; returns an empty string or the error. */
    std::string start();

    void loadSong (const timeline::Song& song);
    void play();
    void pause();
    void stop();
    void seek (double seconds);
    void silence();
    void setGain (float linearGain);

    Snapshot snapshot() const;
    std::string deviceDescription() const;

    /** Frees event lists the audio thread no longer uses. Call from a timer. */
    void collectGarbage();

private:
    void send (const audio::PlaybackCommand& command, const char* what);

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    diagnostics::RotatingLog& log_;
    juce::AudioDeviceManager deviceManager_;
    audio::InternalSynthRenderer synth_;
    audio::PlaybackEngine engine_;               // audio thread only after start
    SpscQueue<audio::PlaybackCommand, 256> commands_;

    std::shared_ptr<const audio::PlaybackEventList> current_;
    std::vector<std::shared_ptr<const audio::PlaybackEventList>> retired_;
    std::uint64_t nextListId_ = 1;

    juce::SmoothedValue<float> gain_ { 0.8f };
    std::atomic<float> targetGain_ { 0.8f };
    std::atomic<double> position_ { 0.0 };
    std::atomic<int> state_ { 0 };
    std::atomic<std::uint64_t> activeListId_ { 0 };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<bool> deviceOpen_ { false };
    std::atomic<bool> errorPending_ { false };
};

} // namespace pianoassist::app
