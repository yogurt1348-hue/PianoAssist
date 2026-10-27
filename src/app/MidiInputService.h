#pragma once

#include "core/SpscQueue.h"
#include "diagnostics/RotatingLog.h"
#include "midi_io/DeviceSelection.h"
#include "midi_io/InputMonitor.h"
#include "ui/InputMonitorView.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
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

    using InputQueue = SpscQueue<midi_io::InputEvent, 4096>;
    /** Second copy of every input event for the lesson thread (its only consumer). */
    InputQueue& lessonQueue() noexcept { return lessonQueue_; }
    void selectByUser (const std::string& identifier);
    /** Writes the input path counters to the log (message thread). */
    void logPath (const char* why) { logStats (why); }
    void rescan (const char* reason);
    /** Sends one synthetic C4 note (on, then off) through the same callback,
        queues and consumers as the piano's own events. For diagnosis. */
    void injectTestNote();
    /** Practice reports how many input events it took (any thread). */
    void setLessonProcessedSource (std::function<std::uint64_t()> source) { lessonProcessed_ = std::move (source); }

private:
    using Clock = std::chrono::steady_clock;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void receive (const juce::uint8* raw, int size) noexcept;
    void timerCallback() override;

    void drainQueue();
    void logStats (const char* why);
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

    InputQueue queue_;
    InputQueue lessonQueue_;
    std::uint64_t sequence_ = 0; // written only by the device callback thread
    midi_io::InputMonitor monitor_;

    // Written by the device callback only (lock-free): what reached it.
    std::atomic<std::uint64_t> callbacks_ { 0 };
    std::atomic<std::uint64_t> queued_ { 0 };
    std::atomic<std::uint64_t> overflows_ { 0 };
    std::atomic<std::uint32_t> lastRaw_ { 0 };     ///< status, data1, data2, size
    std::atomic<std::int64_t> lastRawMicros_ { 0 };
    // Message thread only.
    std::uint64_t processed_ = 0;
    std::uint64_t callbacksAtOpen_ = 0;
    bool firstEventLogged_ = false;
    std::uint64_t loggedCallbacks_ = 0, loggedLesson_ = 0;
    std::int64_t lastStatsLogMicros_ = 0;
    std::uint64_t lessonSeenProcessed_ = 0;
    std::atomic_flag producerBusy_ = ATOMIC_FLAG_INIT; ///< the queues have one producer at a time
    std::int64_t lessonBehindSinceMicros_ = 0;
    bool lessonStallLogged_ = false;
    std::string openError_;
    std::function<std::uint64_t()> lessonProcessed_;

    int ticks_ = 0;
    std::uint64_t otherMessagesLogged_ = 0;
    std::int64_t lastOtherLogMicros_ = 0;
};

} // namespace pianoassist::app
