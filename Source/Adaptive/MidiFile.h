// MidiFile — minimal, dependency-free Standard MIDI File (SMF) reader/writer.
//
// Reads format 0 and format 1 files into a generic multitrack model: every
// channel event of every track (absolute ticks), paired notes, the tempo map
// and time signatures. Unlike StyParser (which is style-oriented and keeps only
// the first tempo), nothing is interpreted here — this is the input for
// Adaptive Song Mode's reference and backing models.
//
// Pure C++ (no JUCE). Never throws on malformed input: problems are reported in
// MidiFileParseResult::error (fatal) or MidiFileData::warnings (recoverable).
//
// The writer exists for synthetic test fixtures and round-trip tests.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cadenza::adaptive
{
enum class MidiEventKind : std::uint8_t
{
    NoteOff,
    NoteOn,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
};

// One channel-voice event. A Note On with velocity 0 is stored as NoteOff.
struct MidiChannelEvent
{
    std::int64_t  tick = 0;
    MidiEventKind kind = MidiEventKind::NoteOn;
    std::uint8_t  channel = 0;   // 0..15
    std::uint8_t  data1 = 0;     // note / controller / program / pressure
    std::uint8_t  data2 = 0;     // velocity / controller value
    int           bend = 8192;   // PitchBend only: 0..16383, 8192 = centre

    bool operator==(const MidiChannelEvent&) const = default;
};

// A Note On paired with its Note Off (FIFO per channel+pitch).
struct MidiNote
{
    std::int64_t onTick = 0;
    std::int64_t offTick = 0;
    std::uint8_t channel = 0;    // 0..15
    std::uint8_t pitch = 60;
    std::uint8_t velocity = 100;
};

struct TempoEvent
{
    std::int64_t  tick = 0;
    std::uint32_t microsPerQuarter = 500000;   // 120 BPM

    double bpm() const noexcept { return microsPerQuarter > 0 ? 60'000'000.0 / microsPerQuarter : 120.0; }
};

struct TimeSignatureEvent
{
    std::int64_t tick = 0;
    int numerator = 4;
    int denominator = 4;
};

struct MidiTrackData
{
    std::string name;                         // from the first track-name meta event
    std::vector<MidiChannelEvent> events;     // tick-ordered (file order kept for ties)
    std::vector<MidiNote> notes;              // sorted by onTick, then pitch
    std::int64_t endTick = 0;                 // tick of End-of-Track (or last event)

    std::uint16_t channelMask() const noexcept;   // bit c set = channel c used by a note
    bool usesChannel(int channel0to15) const noexcept;
    // Program of the first Program Change on `channel`, if any.
    std::optional<int> firstProgram(int channel0to15) const;
};

struct MidiFileData
{
    int format = 1;
    int ppq = 480;
    std::vector<MidiTrackData> tracks;
    std::vector<TempoEvent> tempos;                 // merged from all tracks, tick-sorted
    std::vector<TimeSignatureEvent> timeSignatures; // merged from all tracks, tick-sorted
    std::vector<std::string> warnings;

    std::int64_t lengthTicks() const noexcept;      // max track endTick
    std::size_t  noteCount() const noexcept;
};

struct MidiFileParseResult
{
    bool ok = false;
    std::string error;
    MidiFileData data;
};

MidiFileParseResult parseMidiFile(const std::vector<std::uint8_t>& bytes);
MidiFileParseResult parseMidiFileFromPath(const std::string& path);

// Serialise to an SMF (format 1 unless data.format == 0 with one track).
// Tempo and time-signature events are written into the first track.
std::vector<std::uint8_t> writeMidiFile(const MidiFileData& data);

// Fixture helper: append a note (Note On + Note Off events) to a track.
void addNote(MidiTrackData& track, std::int64_t onTick, std::int64_t lengthTicks,
             int pitch, int velocity = 100, int channel0to15 = 0);
// Stable-sort a track's events by tick (NoteOff before NoteOn on ties).
void sortTrackEvents(MidiTrackData& track);
// For tracks built in code: sort events, rebuild `notes` from them and make
// `endTick` cover every event. (parseMidiFile does this itself.)
void finalizeTrack(MidiTrackData& track);
}
