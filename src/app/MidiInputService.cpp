#include "app/MidiInputService.h"

#include "core/NoteName.h"
#include "midi_io/MidiNormalizer.h"

#include <algorithm>

namespace pianoassist::app
{
namespace
{
/** Translated status text; %1 is replaced by the device name. */
std::string tr (const char* text, const std::string& name = {})
{
    return TRANS (text).replace ("%1", juce::String::fromUTF8 (name.c_str())).toStdString();
}
} // namespace

namespace
{
using diagnostics::LogLevel;
using midi_io::InputEventType;

constexpr int drainIntervalMs = 15;
constexpr int rescanEveryTicks = 1000 / drainIntervalMs; // about once per second
constexpr std::int64_t otherMessageLogIntervalMicros = 5'000'000;

std::string describe (const midi_io::MidiDeviceInfo& device)
{
    return device.name + " [" + device.identifier + "]";
}
} // namespace

MidiInputService::MidiInputService (diagnostics::RotatingLog& log, Remembered remembered)
    : log_ (log), remembered_ (std::move (remembered))
{
}

MidiInputService::~MidiInputService()
{
    stopTimer();
    closeDevice ("application exit");
}

std::int64_t MidiInputService::nowMicros() noexcept // same clock as AudioService::nowMicros
{
    return std::chrono::duration_cast<std::chrono::microseconds> (Clock::now().time_since_epoch()).count();
}

std::vector<midi_io::MidiDeviceInfo> MidiInputService::enumerate() const
{
    std::vector<midi_io::MidiDeviceInfo> devices;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        devices.push_back ({ device.identifier.toStdString(), device.name.toStdString() });
    return devices;
}

void MidiInputService::start()
{
    log_.write (LogLevel::info, "midi", "MIDI input service starting",
                { { "rememberedName", remembered_.name.empty() ? "(none)" : remembered_.name },
                  { "rememberedIdentifier", remembered_.identifier.empty() ? "(none)" : remembered_.identifier } });
    rescan ("startup");
    startTimer (drainIntervalMs);
}

void MidiInputService::rescan (const char* reason)
{
    const auto now = enumerate();
    const auto change = midi_io::diffDevices (devices_, now);
    const bool first = ticks_ == 0 && std::string (reason) == "startup";

    if (first || std::string (reason) != "poll")
    {
        std::string list;
        for (const auto& device : now)
            list += (list.empty() ? "" : "; ") + describe (device);
        log_.write (LogLevel::info, "midi", "MIDI inputs enumerated",
                    { { "reason", reason },
                      { "count", std::to_string (now.size()) },
                      { "devices", list.empty() ? "(none)" : list } });
        if (now.empty())
            log_.write (LogLevel::warning, "midi",
                        "Windows reports no MIDI inputs. Check the USB cable is in the piano's USB TO HOST port, "
                        "the piano is on, and the device appears in Device Manager.");
    }
    for (const auto& device : change.added)
        log_.write (LogLevel::info, "midi", "MIDI input appeared", { { "device", describe (device) } });
    for (const auto& device : change.removed)
        log_.write (LogLevel::warning, "midi", "MIDI input disappeared", { { "device", describe (device) } });

    devices_ = now;

    if (input_ != nullptr)
    {
        const bool stillPresent = std::find (devices_.begin(), devices_.end(), openDevice_) != devices_.end();
        if (! stillPresent)
        {
            closeDevice ("device disappeared");
            deviceLost_ = true;
        }
    }

    if (input_ == nullptr && (! change.empty() || first || std::string (reason) != "poll"))
    {
        if (const auto choice = midi_io::chooseInputDevice (devices_, remembered_.identifier, remembered_.name))
        {
            log_.write (LogLevel::info, "midi", "Automatic input choice",
                        { { "device", describe (choice->device) }, { "reason", midi_io::toString (choice->reason) } });
            openDevice (choice->device, midi_io::toString (choice->reason));
        }
        else if (! remembered_.name.empty() && ! devices_.empty())
        {
            log_.write (LogLevel::info, "midi", "Remembered input not present; waiting for it or a user choice",
                        { { "remembered", remembered_.name } });
        }
    }

    publishView();
}

void MidiInputService::selectByUser (const std::string& identifier)
{
    const auto it = std::find_if (devices_.begin(), devices_.end(),
                                  [&] (const auto& d) { return d.identifier == identifier; });
    if (it == devices_.end())
    {
        log_.write (LogLevel::warning, "ui", "User chose an input that is no longer listed",
                    { { "identifier", identifier } });
        rescan ("user choice of missing device");
        return;
    }

    log_.write (LogLevel::info, "ui", "User chose MIDI input", { { "device", describe (*it) } });
    const auto device = *it;
    remembered_ = { device.identifier, device.name };
    if (choiceListener_)
        choiceListener_ (device);

    if (input_ != nullptr && openDevice_ == device)
    {
        publishView();
        return;
    }
    closeDevice ("user switched input");
    openDevice (device, "user choice");
    publishView();
}

void MidiInputService::openDevice (const midi_io::MidiDeviceInfo& device, const char* reason)
{
    openFailed_ = false;
    auto input = juce::MidiInput::openDevice (juce::String (device.identifier), this);
    if (input == nullptr)
    {
        openFailed_ = true;
        openDevice_ = device;
        log_.write (LogLevel::error, "midi",
                    "Could not open MIDI input. Another program (a DAW, FL Studio, Casio app) may be using it; "
                    "close it and press Rescan.",
                    { { "device", describe (device) }, { "reason", reason } });
        return;
    }

    input->start();
    input_ = std::move (input);
    openDevice_ = device;
    deviceLost_ = false;
    remembered_ = { device.identifier, device.name };
    if (choiceListener_)
        choiceListener_ (device);
    log_.write (LogLevel::info, "midi", "MIDI input opened", { { "device", describe (device) }, { "reason", reason } });
}

void MidiInputService::closeDevice (const char* reason)
{
    if (input_ == nullptr)
        return;
    input_->stop();
    input_.reset();
    drainQueue();

    const auto released = monitor_.releaseAll();
    std::string pitches;
    for (const auto pitch : released)
        pitches += (pitches.empty() ? "" : " ") + noteName (pitch);
    log_.write (LogLevel::info, "midi", "MIDI input closed",
                { { "device", describe (openDevice_) },
                  { "reason", reason },
                  { "releasedHeldNotes", pitches.empty() ? "(none)" : pitches } });
}

void MidiInputService::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // Device thread: no allocation, locks, logging or UI calls.
    const auto timestamp = nowMicros();
    if (const auto event = midi_io::normalizeMidiMessage (message.getRawData(),
                                                         static_cast<std::size_t> (message.getRawDataSize()),
                                                         timestamp, sequence_++))
    {
        queue_.push (*event);
        lessonQueue_.push (*event);
    }
}

void MidiInputService::drainQueue()
{
    bool changed = false;
    while (const auto event = queue_.pop())
    {
        monitor_.apply (*event);
        changed = true;

        switch (event->type)
        {
            case InputEventType::noteOn:
            case InputEventType::noteOff:
                log_.write (LogLevel::debug, "input", event->type == InputEventType::noteOn ? "noteOn" : "noteOff",
                            { { "note", noteName (event->data1) },
                              { "velocity", std::to_string (event->data2) },
                              { "channel", std::to_string (event->channel + 1) },
                              { "t_us", std::to_string (event->timestampMicros) } });
                break;
            case InputEventType::sustain:
                log_.write (LogLevel::debug, "input", "sustain",
                            { { "value", std::to_string (event->data2) },
                              { "t_us", std::to_string (event->timestampMicros) } });
                break;
            case InputEventType::other:
                break;
        }
    }

    if (const auto dropped = queue_.takeDroppedCount(); dropped > 0)
        log_.write (LogLevel::error, "input", "Input queue overflow; events dropped",
                    { { "dropped", std::to_string (dropped) } });

    const auto others = monitor_.counters().otherMessages;
    const auto now = nowMicros();
    if (others != otherMessagesLogged_ && now - lastOtherLogMicros_ >= otherMessageLogIntervalMicros)
    {
        log_.write (LogLevel::debug, "input", "Other MIDI messages received (not used)",
                    { { "since_last_report", std::to_string (others - otherMessagesLogged_) } });
        otherMessagesLogged_ = others;
        lastOtherLogMicros_ = now;
    }

    if (changed)
        publishView();
}

void MidiInputService::timerCallback()
{
    drainQueue();
    if (++ticks_ % rescanEveryTicks == 0)
    {
        rescan ("poll");
        publishView(); // refresh the event-rate readout
    }
}

void MidiInputService::publishView()
{
    if (! viewListener_)
        return;

    ui::InputMonitorView view;
    for (const auto& device : devices_)
        view.devices.push_back ({ device.identifier, device.name });
    view.selectedIdentifier = (input_ != nullptr || openFailed_) ? openDevice_.identifier : remembered_.identifier;

    using Status = ui::InputMonitorView::Status;
    if (input_ != nullptr)
    {
        view.status = Status::connected;
        view.statusText = tr ("Connected: %1. Press keys to test.", openDevice_.name);
    }
    else if (openFailed_)
    {
        view.status = Status::openFailed;
        view.statusText = tr ("Could not open %1.", openDevice_.name);
        view.bannerText = tr ("Another program may be using the piano. Close it, then press Rescan.");
    }
    else if (deviceLost_)
    {
        view.status = Status::lost;
        view.statusText = tr ("Waiting for %1 to come back...", openDevice_.name);
        view.bannerText = tr ("Piano disconnected. Reconnect the USB cable; it will be picked up automatically.");
    }
    else if (devices_.empty())
    {
        view.status = Status::noDevices;
        view.statusText = tr ("Windows reports no MIDI inputs. Turn the piano on and connect USB TO HOST.");
    }
    else if (! remembered_.name.empty())
    {
        view.status = Status::waiting;
        view.statusText = tr ("Waiting for %1, or choose another input.", remembered_.name);
    }
    else
    {
        view.status = Status::notSelected;
        view.statusText = tr ("Choose your piano.");
    }

    if (const auto& last = monitor_.lastNote())
        view.lastNote = noteName (last->pitch) + "  vel " + std::to_string (last->velocity);
    view.heldPitches = monitor_.heldPitches();
    for (const auto pitch : view.heldPitches)
        view.heldNotes += (view.heldNotes.empty() ? "" : " ") + noteName (pitch);
    view.sustainDown = monitor_.sustainDown();
    view.eventsPerSecond = monitor_.eventsPerSecond (nowMicros());
    view.totalNoteOns = monitor_.counters().noteOns;

    viewListener_ (view);
}

} // namespace pianoassist::app
