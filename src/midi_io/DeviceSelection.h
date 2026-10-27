#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pianoassist::midi_io
{

struct MidiDeviceInfo
{
    std::string identifier; ///< OS identifier; stable across reconnects on the same port
    std::string name;       ///< display name; never assumed to contain "Casio"

    friend bool operator== (const MidiDeviceInfo&, const MidiDeviceInfo&) = default;
};

struct DeviceListChange
{
    std::vector<MidiDeviceInfo> added;
    std::vector<MidiDeviceInfo> removed;

    bool empty() const noexcept { return added.empty() && removed.empty(); }
};

DeviceListChange diffDevices (const std::vector<MidiDeviceInfo>& before, const std::vector<MidiDeviceInfo>& after);

enum class SelectionReason
{
    rememberedIdentifier, ///< same device as last time
    rememberedName,       ///< identifier changed (e.g. other USB port), same name
    onlyChoice,           ///< nothing remembered, first available device
};

struct DeviceChoice
{
    MidiDeviceInfo device;
    SelectionReason reason;
};

/** Picks the input to open automatically. A remembered device that is
    missing is not replaced by a different one: the user chose it, so we wait
    for it to come back unless they pick another. */
std::optional<DeviceChoice> chooseInputDevice (const std::vector<MidiDeviceInfo>& available,
                                               const std::string& rememberedIdentifier,
                                               const std::string& rememberedName);

const char* toString (SelectionReason reason) noexcept;

} // namespace pianoassist::midi_io
