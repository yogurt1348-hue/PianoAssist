#include "midi_io/InputMonitor.h"

namespace pianoassist::midi_io
{
namespace
{
constexpr std::size_t maxRecentTimestamps = 4096;
constexpr std::int64_t recentHistoryMicros = 5'000'000;
} // namespace

void InputMonitor::apply (const InputEvent& event)
{
    const std::pair order { event.timestampMicros, event.sequence };
    if (lastOrder_ && order < *lastOrder_)
        ++counters_.outOfOrderEvents;
    else
        lastOrder_ = order;

    recentTimestamps_.push_back (event.timestampMicros);
    while (recentTimestamps_.size() > maxRecentTimestamps
           || (! recentTimestamps_.empty() && recentTimestamps_.front() < event.timestampMicros - recentHistoryMicros))
        recentTimestamps_.pop_front();

    switch (event.type)
    {
        case InputEventType::noteOn:
            ++counters_.noteOns;
            if (held_.test (event.data1))
                ++counters_.duplicateNoteOns;
            held_.set (event.data1);
            lastNote_ = LastNote { event.data1, event.data2, event.timestampMicros };
            break;

        case InputEventType::noteOff:
            ++counters_.noteOffs;
            if (! held_.test (event.data1))
                ++counters_.orphanNoteOffs;
            held_.reset (event.data1);
            break;

        case InputEventType::sustain:
            ++counters_.sustainChanges;
            sustainDown_ = event.data2 >= 64;
            break;

        case InputEventType::other:
            ++counters_.otherMessages;
            break;
    }
}

std::vector<std::uint8_t> InputMonitor::releaseAll()
{
    auto released = heldPitches();
    held_.reset();
    sustainDown_ = false;
    lastOrder_.reset();
    return released;
}

std::vector<std::uint8_t> InputMonitor::heldPitches() const
{
    std::vector<std::uint8_t> pitches;
    for (std::size_t p = 0; p < held_.size(); ++p)
        if (held_.test (p))
            pitches.push_back (static_cast<std::uint8_t> (p));
    return pitches;
}

double InputMonitor::eventsPerSecond (std::int64_t nowMicros, std::int64_t windowMicros) const
{
    if (windowMicros <= 0)
        return 0.0;
    std::size_t count = 0;
    for (auto it = recentTimestamps_.rbegin(); it != recentTimestamps_.rend() && *it > nowMicros - windowMicros; ++it)
        if (*it <= nowMicros)
            ++count;
    return static_cast<double> (count) * 1.0e6 / static_cast<double> (windowMicros);
}

} // namespace pianoassist::midi_io
