#pragma once

#include "audio/InternalSynthRenderer.h"
#include "audio/PlaybackEngine.h"
#include "core/SpscQueue.h"
#include "diagnostics/RotatingLog.h"
#include "timeline/Song.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace pianoassist::app
{

/** Owns the audio device, the playback engine and the internal synth. One
    control thread (the lesson thread) sends commands through a wait-free
    queue; the audio callback publishes position and state through atomics. */
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
        double blockStartPosition = 0.0;   ///< song position at the start of the last callback
        std::int64_t blockStartMicros = 0; ///< steady clock at the start of the last callback
        std::uint64_t gateArrivals = 0;
        std::int64_t arrivalMicros = 0;    ///< steady clock of the block that reached the last gate
    };

    static std::int64_t nowMicros() noexcept;

    explicit AudioService (diagnostics::RotatingLog& log);
    ~AudioService() override;

    /** Opens the default output device; returns an empty string or the error. */
    std::string start();

    /** Plays only notes whose role passes the filter (practised hands are
        played by the learner). Stops playback and returns to 0. */
    void loadSong (const timeline::Song& song, const std::function<bool (timeline::NoteRole)>& include);
    void setGate (double boundarySeconds); ///< negative clears
    void previewKey (std::uint8_t pitch, bool down);
    void play();
    void pause();
    void stop();
    void seek (double seconds);
    void silence();
    void setGain (float linearGain);

    Snapshot snapshot() const;
    std::string deviceDescription() const;

    /** Frees event lists and instruments the audio thread no longer uses.
        Call from a timer on the message thread. */
    void collectGarbage();

    /** Message thread. Prepares the instrument for the current device and
        hands it to the audio thread; nullptr returns to the built-in synth.
        The previous instrument is destroyed later, on the message thread,
        once the audio thread can no longer be using it. */
    void setInstrument (std::unique_ptr<audio::IInstrumentRenderer> instrument);
    audio::IInstrumentRenderer* instrument() const noexcept { return external_.get(); }

    /** Sample rate and block size the instrument must be prepared for. */
    std::pair<double, int> renderFormat() const;

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

    mutable std::mutex listsMutex_; // the lesson thread loads, the UI thread collects; never the audio thread
    std::shared_ptr<const audio::PlaybackEventList> current_;
    std::vector<std::shared_ptr<const audio::PlaybackEventList>> retired_;
    std::uint64_t nextListId_ = 1;

    // Instruments: the audio thread reads active_ once per block. Retired
    // instruments wait for two more blocks before the message thread deletes them.
    std::unique_ptr<audio::IInstrumentRenderer> external_;
    std::vector<std::pair<std::unique_ptr<audio::IInstrumentRenderer>, std::uint64_t>> retiredInstruments_;
    std::atomic<audio::IInstrumentRenderer*> active_ { &synth_ };
    audio::IInstrumentRenderer* lastUsed_ = &synth_; // audio thread only
    std::atomic<std::uint64_t> blocksRendered_ { 0 };
    std::atomic<double> deviceRate_ { 48000.0 };
    std::atomic<int> deviceBlock_ { 512 };

    juce::SmoothedValue<float> gain_ { 0.8f };
    std::atomic<float> targetGain_ { 0.8f };
    std::atomic<double> position_ { 0.0 };
    std::atomic<int> state_ { 0 };
    std::atomic<std::uint64_t> activeListId_ { 0 };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<bool> deviceOpen_ { false };
    std::atomic<bool> errorPending_ { false };
    std::atomic<std::uint32_t> blockSeq_ { 0 }; // odd while the audio thread writes the block fields
    std::atomic<double> blockStartPosition_ { 0.0 };
    std::atomic<std::int64_t> blockStartMicros_ { 0 };
    std::atomic<std::uint64_t> gateArrivals_ { 0 };
    std::atomic<std::int64_t> arrivalMicros_ { 0 };
};

} // namespace pianoassist::app
