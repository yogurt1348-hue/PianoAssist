#include "BackingSequence.h"

#include <algorithm>

namespace cadenza::adaptive
{
namespace
{
int kindOrder(MidiEventKind k) noexcept
{
    switch (k) {
        case MidiEventKind::NoteOff: return 0;   // release before re-strike
        case MidiEventKind::NoteOn:  return 2;
        default:                     return 1;   // controls before notes
    }
}

// Controllers worth restoring after a seek (others are momentary or rare).
bool chasedController(int cc) noexcept
{
    switch (cc) {
        case 0: case 32:          // bank select
        case 1: case 7: case 10:  // modulation, volume, pan
        case 11:                  // expression
        case 64:                  // sustain
        case 71: case 72: case 73: case 74:
        case 91: case 93:         // reverb, chorus send
            return true;
        default:
            return false;
    }
}
}

std::array<std::uint8_t, 16> BackingSequence::identityChannels() noexcept
{
    std::array<std::uint8_t, 16> map {};
    for (int c = 0; c < 16; ++c) map[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(c + 1);
    return map;
}

BackingSequence BackingSequence::build(const MidiFileData& file, const std::vector<TrackSelection>& tracks,
                                       const std::array<std::uint8_t, 16>& outputChannel,
                                       TickMapper mapper, const void* ctx)
{
    BackingSequence seq;
    for (const auto& sel : tracks) {
        if (sel.track < 0 || sel.track >= static_cast<int>(file.tracks.size())) continue;
        const auto& t = file.tracks[static_cast<std::size_t>(sel.track)];
        for (const auto& e : t.events) {
            if ((sel.channelMask & (1u << e.channel)) == 0) continue;
            const auto out = outputChannel[e.channel];
            if (out < 1 || out > 16) continue;
            BackingEvent b;
            b.tick = mapper != nullptr ? mapper(static_cast<double>(e.tick), ctx) : static_cast<double>(e.tick);
            b.kind = e.kind;
            b.channel = out;
            b.data1 = e.data1;
            b.data2 = e.data2;
            b.bend = e.bend;
            b.sourceTrack = static_cast<std::uint16_t>(sel.track);
            seq.m_events.push_back(b);
            seq.m_endTick = std::max(seq.m_endTick, b.tick);
            seq.m_outputMask = static_cast<std::uint16_t>(seq.m_outputMask | (1u << (out - 1)));
        }
        const double end = mapper != nullptr ? mapper(static_cast<double>(t.endTick), ctx) : static_cast<double>(t.endTick);
        seq.m_endTick = std::max(seq.m_endTick, end);
    }
    std::stable_sort(seq.m_events.begin(), seq.m_events.end(), [](const BackingEvent& a, const BackingEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return kindOrder(a.kind) < kindOrder(b.kind);
    });
    return seq;
}

void BackingSequence::emit(const BackingEvent& e, MidiEventSink& sink) noexcept
{
    const int ch = e.channel;
    auto& count = m_sounding[static_cast<std::size_t>(ch - 1)][e.data1];
    switch (e.kind) {
        case MidiEventKind::NoteOn:
            if (count < 255) ++count;
            sink.noteOn(ch, e.data1, e.data2);
            break;
        case MidiEventKind::NoteOff:
            // Only release what we started (a seek may land between on and off).
            if (count > 0) {
                --count;
                sink.noteOff(ch, e.data1);
            }
            break;
        case MidiEventKind::ControlChange:
            sink.controlChange(ch, e.data1, e.data2);
            break;
        case MidiEventKind::ProgramChange:
            sink.programChange(ch, e.data1);
            break;
        case MidiEventKind::PitchBend:
            sink.pitchBend(ch, e.bend);
            break;
        case MidiEventKind::PolyPressure:
        case MidiEventKind::ChannelPressure:
            break;   // not supported by the output API; harmless to drop
    }
}

void BackingSequence::render(double fromTick, double toTick, bool inclusive, MidiEventSink& sink) noexcept
{
    while (m_cursor < m_events.size()) {
        const auto& e = m_events[m_cursor];
        if (e.tick > toTick) break;
        const bool inRange = inclusive ? e.tick >= fromTick : e.tick > fromTick;
        // Events before the range were skipped by a jump; never play them late.
        if (inRange) emit(e, sink);
        ++m_cursor;
    }
}

void BackingSequence::releaseAll(MidiEventSink& sink) noexcept
{
    for (int ch = 0; ch < 16; ++ch) {
        auto& notes = m_sounding[static_cast<std::size_t>(ch)];
        for (int n = 0; n < 128; ++n) {
            while (notes[static_cast<std::size_t>(n)] > 0) {
                --notes[static_cast<std::size_t>(n)];
                sink.noteOff(ch + 1, n);
            }
        }
    }
}

int BackingSequence::soundingNotes() const noexcept
{
    int total = 0;
    for (const auto& ch : m_sounding)
        for (auto c : ch) total += c;
    return total;
}

void BackingSequence::seek(double tick, MidiEventSink& sink) noexcept
{
    releaseAll(sink);

    // Chase: the latest program / controller / bend value before `tick`, per channel.
    std::array<std::int16_t, 16> program;
    std::array<std::int16_t, 16> bend;
    std::array<std::array<std::int16_t, 128>, 16> cc;
    program.fill(-1);
    bend.fill(-1);
    for (auto& c : cc) c.fill(-1);

    std::size_t i = 0;
    for (; i < m_events.size() && m_events[i].tick < tick; ++i) {
        const auto& e = m_events[i];
        const auto ch = static_cast<std::size_t>(e.channel - 1);
        if (e.kind == MidiEventKind::ProgramChange) program[ch] = e.data1;
        else if (e.kind == MidiEventKind::ControlChange && chasedController(e.data1)) cc[ch][e.data1] = e.data2;
        else if (e.kind == MidiEventKind::PitchBend) bend[ch] = static_cast<std::int16_t>(e.bend);
    }
    m_cursor = i;

    for (int ch = 0; ch < 16; ++ch) {
        const auto c = static_cast<std::size_t>(ch);
        if (cc[c][0] >= 0) sink.controlChange(ch + 1, 0, cc[c][0]);
        if (cc[c][32] >= 0) sink.controlChange(ch + 1, 32, cc[c][32]);
        if (program[c] >= 0) sink.programChange(ch + 1, program[c]);
        for (int n = 1; n < 128; ++n)
            if (n != 32 && cc[c][static_cast<std::size_t>(n)] >= 0)
                sink.controlChange(ch + 1, n, cc[c][static_cast<std::size_t>(n)]);
        if (bend[c] >= 0) sink.pitchBend(ch + 1, bend[c]);
    }
}
}
