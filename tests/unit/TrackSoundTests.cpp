// Sounds per track: lanes keep two tracks on one MIDI channel apart, the
// file's bank and program changes play at their time and come back after a
// seek, a changed sound releases only that track's notes, and a SoundFont
// preset chosen for a track ignores the file's program changes.
#include "Fixtures.h"
#include "audio/InstrumentRouter.h"
#include "audio/PlaybackEngine.h"
#include "audio/SoundFontRenderer.h"
#include "smf/SmfReader.h"
#include "timeline/Song.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace pianoassist;
using namespace pianoassist::audio;

namespace
{
class Recorder final : public IInstrumentRenderer
{
public:
    void prepare (double, int) override {}
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override
    {
        for (const auto& e : events)
            seen.push_back (e);
        if (block.channels != nullptr)
            for (int ch = 0; ch < block.numChannels; ++ch)
                if (block.channels[ch] != nullptr)
                    std::fill_n (block.channels[ch], block.numFrames, 0.0f);
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override {}
    int reportedLatencyFrames() const noexcept override { return 0; }

    std::vector<int> notesOn() const
    {
        std::vector<int> pitches;
        for (const auto& e : seen)
            if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
                pitches.push_back (e.data1);
        return pitches;
    }
    std::vector<int> notesOff() const
    {
        std::vector<int> pitches;
        for (const auto& e : seen)
            if ((e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0))
                pitches.push_back (e.data1);
        return pitches;
    }
    std::vector<ScheduledMidiEvent> seen;
};

/** What the app's audio callback does: the engine renders through a routing. */
class Routed final : public IInstrumentRenderer
{
public:
    void prepare (double, int) override {}
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override
    {
        router.render (*routing, events, block, previous);
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override {}
    int reportedLatencyFrames() const noexcept override { return 0; }

    InstrumentRouter router;
    const InstrumentRouting* routing = nullptr;
    const InstrumentRouting* previous = nullptr;
};

struct Block
{
    explicit Block (int frames) : left (static_cast<std::size_t> (frames)), right (static_cast<std::size_t> (frames)) {}
    RenderBlock block() { return { pointers.data(), 2, static_cast<int> (left.size()) }; }
    std::vector<float> left, right;
    std::array<float*, 2> pointers { left.data(), right.data() };
};

timeline::Song sharedChannelSong()
{
    auto file = smf::readSmf (test::readFixture ("shared_channel_programs.mid"));
    REQUIRE (file.isOk());
    auto song = timeline::buildSong (file.value());
    REQUIRE (song.isOk());
    return std::move (song).value();
}

void playFor (PlaybackEngine& engine, IInstrumentRenderer& renderer, double seconds)
{
    Block b (480);
    for (int i = 0; i < static_cast<int> (seconds * 100.0); ++i)
        engine.process (renderer, b.block());
}
} // namespace

TEST_CASE ("Bank select and program changes are kept per track with their times")
{
    const auto song = sharedChannelSong();
    REQUIRE (song.parts.size() == 2);
    CHECK (song.parts[0].trackName == "Bass");
    CHECK (song.parts[0].program == 33);
    CHECK (song.parts[0].bank == 0);
    CHECK (song.parts[1].program == 0);
    CHECK (song.parts[1].bank == -1);
    REQUIRE (song.programChanges.size() == 3);
    CHECK (song.programChanges.back().bank == 128);
    CHECK (std::abs (song.programChanges.back().seconds - 1.0) < 1.0e-6);
}

TEST_CASE ("Two tracks on one MIDI channel reach their own instruments")
{
    const auto song = sharedChannelSong();
    const auto list = buildPlaybackEvents (song, 1);
    Recorder main, bass, lead;
    InstrumentRouting routing;
    routing.main = &main;
    routing.parts[0] = &bass;
    routing.parts[1] = &lead;
    Routed routed;
    routed.routing = &routing;

    PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ PlaybackCommand::Type::setEvents, 0.0, &list }, routed);
    engine.handle ({ PlaybackCommand::Type::play }, routed);
    playFor (engine, routed, 2.5);

    CHECK (bass.notesOn() == std::vector<int> { 36, 36 });
    CHECK (lead.notesOn() == std::vector<int> { 64, 64 });
    CHECK (main.notesOn().empty());
    // Each track got its own program changes, not the other's.
    for (const auto& e : bass.seen)
        if ((e.status & 0xF0) == 0xC0)
            CHECK (e.data1 == 33);
    for (const auto& e : lead.seen)
        if ((e.status & 0xF0) == 0xC0)
            CHECK (e.data1 == 0);
}

TEST_CASE ("A seek restores the program and bank each track has at that point")
{
    const auto song = sharedChannelSong();
    const auto list = buildPlaybackEvents (song, 1);
    Recorder main, bass, lead;
    InstrumentRouting routing;
    routing.main = &main;
    routing.parts[0] = &bass;
    routing.parts[1] = &lead;
    Routed routed;
    routed.routing = &routing;

    PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ PlaybackCommand::Type::setEvents, 0.0, &list }, routed);
    playFor (engine, routed, 0.01);
    lead.seen.clear();
    bass.seen.clear();

    engine.handle ({ PlaybackCommand::Type::seek, 1.25 }, routed); // after the lead's change to bank 128
    playFor (engine, routed, 0.01);
    int leadBankMsb = -1;
    bool leadProgram = false;
    for (const auto& e : lead.seen)
    {
        if ((e.status & 0xF0) == 0xB0 && e.data1 == 0)
            leadBankMsb = e.data2;
        if ((e.status & 0xF0) == 0xC0)
            leadProgram = e.data1 == 0;
    }
    CHECK (leadBankMsb == 1); // bank 128 = MSB 1, LSB 0
    CHECK (leadProgram);
    bool bassProgram = false;
    for (const auto& e : bass.seen)
        if ((e.status & 0xF0) == 0xC0)
            bassProgram = e.data1 == 33;
    CHECK (bassProgram);

    // Back before the change: the lead has no bank select again, only its first program.
    lead.seen.clear();
    engine.handle ({ PlaybackCommand::Type::seek, 0.25 }, routed);
    playFor (engine, routed, 0.01);
    for (const auto& e : lead.seen)
        CHECK_FALSE (((e.status & 0xF0) == 0xB0 && e.data1 == 0));
}

TEST_CASE ("Changing one track's instrument releases only its notes, on the old instrument")
{
    const auto song = sharedChannelSong();
    const auto list = buildPlaybackEvents (song, 1);
    Recorder main, bass, lead, newBass;
    InstrumentRouting before;
    before.main = &main;
    before.parts[1] = &lead; // the bass follows its channel: the main instrument
    InstrumentRouting after = before;
    after.parts[0] = &newBass;
    Routed routed;
    routed.routing = &before;

    PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ PlaybackCommand::Type::setEvents, 0.0, &list }, routed);
    engine.handle ({ PlaybackCommand::Type::play }, routed);
    playFor (engine, routed, 0.2); // C2 (bass) and E4 (lead) sound
    REQUIRE (main.notesOn() == std::vector<int> { 36 });
    main.seen.clear();
    lead.seen.clear();

    engine.releaseRerouted (before, after);
    routed.previous = &before;
    routed.routing = &after;
    Block b (480);
    engine.process (routed, b.block());
    routed.previous = nullptr;

    CHECK (main.notesOff() == std::vector<int> { 36 }); // released where it was playing
    CHECK (newBass.notesOff().empty());
    CHECK (lead.notesOff().empty());                     // the other track keeps its note

    playFor (engine, routed, 1.0);                       // the bass's next note plays on its new instrument
    CHECK (newBass.notesOn() == std::vector<int> { 36 });
    CHECK (main.notesOn().empty());
}

namespace
{
std::unique_ptr<SoundFontRenderer> bankFont()
{
    auto font = SoundFontRenderer::fromMemory (test::readFixture ("tiny_bank.sf2"), "tiny_bank");
    REQUIRE (font);
    return std::move (font).value();
}

float peakOf (SoundFontRenderer& player, std::vector<ScheduledMidiEvent> events)
{
    player.prepare (48000.0, 512);
    Block b (512);
    float peak = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        player.render (i == 0 ? std::span<const ScheduledMidiEvent> (events) : std::span<const ScheduledMidiEvent>(), b.block());
        for (const auto v : b.left)
            peak = std::max (peak, std::abs (v));
    }
    return peak;
}
} // namespace

TEST_CASE ("A SoundFont lists its presets, drum kits included")
{
    const auto font = bankFont();
    const auto presets = font->presets();
    REQUIRE (presets.size() == 3);
    CHECK (presets[0].name == "Sine");
    CHECK (presets[1].program == 33);
    CHECK (presets[1].name == "Very Quiet Bass Tone"); // all 20 characters, not 19
    CHECK (presets[2].bank == 128);
    CHECK (presets[2].drums);
    CHECK_FALSE (presets[0].drums);
    CHECK (font->hasPreset (128, 0));
    CHECK_FALSE (font->hasPreset (0, 5));
}

TEST_CASE ("A preset chosen for a track ignores the song's program and bank changes")
{
    const auto font = bankFont();
    auto loud = font->copy();
    const float full = peakOf (*loud, { { 0, 0x91, 69, 127 } });
    REQUIRE (full > 0.05f);

    auto quiet = font->copy();
    quiet->lockPreset (0, 33);
    const float locked = peakOf (*quiet, { { 0, 0xB1, 0, 0 }, { 0, 0xC1, 0, 0 }, { 0, 0x91, 69, 127 } });
    CHECK (locked < full * 0.5f); // still the quiet preset despite program 0

    // The file's own instrument: the program change is followed.
    auto follows = font->copy();
    const float followed = peakOf (*follows, { { 0, 0xC1, 33, 0 }, { 0, 0x91, 69, 127 } });
    CHECK (followed < full * 0.5f);

    // A drum kit on any channel, not only channel 10.
    auto kit = font->copy();
    kit->lockPreset (128, 0);
    const float drums = peakOf (*kit, { { 0, 0x92, 69, 127 } });
    CHECK (drums > full * 0.55f);
    CHECK (drums < full * 0.9f); // the kit, not the melodic preset
}
