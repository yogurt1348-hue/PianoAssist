#pragma once

#include "core/SpscQueue.h"
#include "diagnostics/RotatingLog.h"
#include "midi_io/DeviceSelection.h"
#include "midi_io/InputMonitor.h"
#include "ui/InputMonitorView.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>

namespace pianoassist::app
{

/** JUCE adapter for one MIDI input. The device callback only timestamps,
    normalizes and queues; everything else (monitor state, device polling,
    logging, reconnect) runs on the message thread, which is the control
    thread until the lesson engine gets its own. */
class MidiInputService final : private juce::MidiInputCallback, private juce::Timer
{
public:
    struct Remembered
    {
        std::string identifier;
        std::string name;
    };

    using ViewListener = std::function<void (const ui::InputMonitorView&)>;
    using ChoiceListener = std::function<void (const midi_io::MidiDeviceInfo&)>;

    MidiInputService (diagnostics::RotatingLog& log, Remembered remembered);
    ~MidiInputService() override;

    void setViewListener (ViewListener listener) { viewListener_ = std::move (listener); }
    /** Called when the user picks a device, so the app can persist it. */
    void setChoiceListener (ChoiceListener listener) { choiceListener_ = std::move (listener); }

    void start();
    void selectByUser (const std::string& identifier);
    void rescan (const char* reason);

private:
    using Clock = std::chrono::steady_clock;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void timerCallback() override;

    void drainQueue();
    void openDevice (const midi_io::MidiDeviceInfo& device, const char* reason);
    void closeDevice (const char* reason);
    void publishView();
    std::vector<midi_io::MidiDeviceInfo> enumerate() const;
    static std::int64_t nowMicros() noexcept;

    diagnostics::RotatingLog& log_;
    Remembered remembered_;
    ViewListener viewListener_;
    ChoiceListener choiceListener_;

    std::unique_ptr<juce::MidiInput> input_;
    midi_io::MidiDeviceInfo openDevice_;
    std::vector<midi_io::MidiDeviceInfo> devices_;
    bool deviceLost_ = false;
    bool openFailed_ = false;

    SpscQueue<midi_io::InputEvent, 4096> queue_;
    std::uint64_t sequence_ = 0; // written only by the device callback thread
    midi_io::InputMonitor monitor_;

    int ticks_ = 0;
    std::uint64_t otherMessagesLogged_ = 0;
    std::int64_t lastOtherLogMicros_ = 0;
};

} // namespace pianoassist::app
