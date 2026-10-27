#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** Everything the input monitor draws. Built by the app from the input
    service; the UI never reads engine or device objects directly. */
struct InputMonitorView
{
    struct Device
    {
        std::string identifier;
        std::string name;
    };

    enum class Status
    {
        noDevices,     ///< Windows reports no MIDI inputs at all
        notSelected,   ///< devices exist, none chosen
        connected,
        waiting,       ///< remembered device is not present yet
        lost,          ///< the open device disappeared
        openFailed,    ///< the device exists but could not be opened
    };

    std::vector<Device> devices;
    std::string selectedIdentifier;
    Status status = Status::noDevices;
    std::string statusText;
    std::string bannerText; ///< empty when no banner is shown

    std::string lastNote;   ///< e.g. "C4  velocity 87"
    std::string heldNotes;  ///< e.g. "C4 E4 G4"
    std::vector<std::uint8_t> heldPitches;
    bool sustainDown = false;
    double eventsPerSecond = 0.0;
    unsigned long long totalNoteOns = 0;

    // Input path, step by step: each counter is where an event got to.
    std::string inputName;          ///< the input chosen or remembered
    bool found = false;             ///< Windows lists it now
    bool opened = false;            ///< PianoAssist has it open and started
    bool receiving = false;         ///< events arrived in the last few seconds
    unsigned long long callbacks = 0;      ///< messages the device callback got (all kinds)
    unsigned long long queued = 0;         ///< note, pedal and other events queued
    unsigned long long processed = 0;      ///< taken from the queue by the window (keyboard, monitor)
    unsigned long long lessonProcessed = 0; ///< taken from the queue by practice (gates, timing)
    unsigned long long overflows = 0;      ///< events lost because a queue was full
    std::string lastEvent;          ///< raw bytes of the last message and how long ago
    std::string openError;          ///< why opening failed, empty when it did not
};

} // namespace pianoassist::ui
