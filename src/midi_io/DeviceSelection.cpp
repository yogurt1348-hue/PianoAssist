#include "midi_io/DeviceSelection.h"

#include <algorithm>

namespace pianoassist::midi_io
{
namespace
{
bool contains (const std::vector<MidiDeviceInfo>& list, const MidiDeviceInfo& device)
{
    return std::find (list.begin(), list.end(), device) != list.end();
}
} // namespace

DeviceListChange diffDevices (const std::vector<MidiDeviceInfo>& before, const std::vector<MidiDeviceInfo>& after)
{
    DeviceListChange change;
    for (const auto& device : after)
        if (! contains (before, device))
            change.added.push_back (device);
    for (const auto& device : before)
        if (! contains (after, device))
            change.removed.push_back (device);
    return change;
}

std::optional<DeviceChoice> chooseInputDevice (const std::vector<MidiDeviceInfo>& available,
                                               const std::string& rememberedIdentifier,
                                               const std::string& rememberedName)
{
    if (! rememberedIdentifier.empty())
        for (const auto& device : available)
            if (device.identifier == rememberedIdentifier)
                return DeviceChoice { device, SelectionReason::rememberedIdentifier };

    if (! rememberedName.empty())
        for (const auto& device : available)
            if (device.name == rememberedName)
                return DeviceChoice { device, SelectionReason::rememberedName };

    if (rememberedIdentifier.empty() && rememberedName.empty() && ! available.empty())
        return DeviceChoice { available.front(), SelectionReason::onlyChoice };

    return std::nullopt;
}

const char* toString (SelectionReason reason) noexcept
{
    switch (reason)
    {
        case SelectionReason::rememberedIdentifier: return "remembered identifier";
        case SelectionReason::rememberedName:       return "remembered name";
        case SelectionReason::onlyChoice:           return "first available device";
    }
    return "unknown";
}

} // namespace pianoassist::midi_io
