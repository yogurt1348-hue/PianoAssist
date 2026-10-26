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
};

} // namespace pianoassist::ui
