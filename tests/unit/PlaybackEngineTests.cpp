#include "Fixtures.h"
#include "audio/PlaybackEngine.h"
#include "smf/SmfReader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using namespace pianoassist;
using namespace pianoassist::audio;
using Catch::Approx;

namespace
{
struct RecordedEvent
{
    std::int64_t frame;
    std::uint8_t status, data1, data2;
    bool operator== (const RecordedEvent&) const = default;
};

class RecordingRenderer final : public IInstrumentRenderer
{
public:
    void prepare (double, int) override {}
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override
    {
        for (const auto& e : events)
            recorded.push_back ({ frame + e.sampleOffset, e.status, e.data1, e.data2 });
        frame += block.numFrames;
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override { ++silenceCalls; }
    int reportedLatencyFrames() const noexcept override { return 0; }

    std::vector<RecordedEvent> recorded;
    std::int64_t frame = 0;
    int silenceCalls = 0;

    std::size_t count (std::uint8_t kind) const
    {
        std::size_t n = 0;
        for (const auto& e : recorded)
            if ((e.status & 0xF0) == kind)
                ++n;
        return n;
    }
};

PlaybackEventList scaleEvents()
{
    auto file = smf::readSmf (test::readFixture ("format0_c_major_scale_120bpm.mid"));
    REQUIRE (file.isOk());
    auto song = timeline::buildSong (file.value());
    REQUIRE (song.isOk());
    return buildPlaybackEvents (song.value(), 1);
}

void runFrames (PlaybackEngine& engine, RecordingRenderer& renderer, std::int64_t frames, int blockSize)
{
    for (std::int64_t done = 0; done < frames; done += blockSize)
        engine.process (renderer, { nullptr, 2, blockSize });
}

void send (PlaybackEngine& engine, RecordingRenderer& renderer, PlaybackCommand::Type type, double value = 0.0,
           const PlaybackEventList* list = nullptr)
{
    engine.handle ({ type, value, list }, renderer);
}
} // namespace

TEST_CASE ("Event list orders note-offs before note-ons at the same time")
{
    const auto list = scaleEvents();
    REQUIRE (list.events.size() == 16);
    CHECK (list.events[0].status == 0x90);
    CHECK (list.events[1].seconds == Approx (0.5));
    CHECK (list.events[1].status == 0x80); // C4 off
    CHECK (list.events[2].status == 0x90); // D4 on, same time
    CHECK (list.durationSeconds == Approx (4.0));
}

TEST_CASE ("Playback schedules events at sample-accurate frames for any buffer size")
{
    const auto list = scaleEvents();
    const double sampleRate = GENERATE (44100.0, 48000.0);
    const int blockSize = GENERATE (128, 256, 512, 1024);

    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (sampleRate);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, static_cast<std::int64_t> (5.0 * sampleRate), blockSize);

    REQUIRE (renderer.count (0x90) == 8);
    REQUIRE (renderer.count (0x80) == 8);
    for (const auto& e : renderer.recorded)
        if (e.status == 0x90 && e.data1 == 65) // F4 starts at 1.5 s
            CHECK (e.frame == static_cast<std::int64_t> (std::llround (1.5 * sampleRate)));
    CHECK (engine.state() == PlaybackState::completed);
    CHECK (engine.soundingNoteCount() == 0);
}

TEST_CASE ("Pause releases owned notes and resumes without retriggering")
{
    const auto list = scaleEvents();
    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (48000.0);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, 12000, 256); // 0.25 s: C4 sounding

    CHECK (engine.soundingNoteCount() == 1);
    send (engine, renderer, PlaybackCommand::Type::pause);
    const double pausedAt = engine.positionSeconds();
    runFrames (engine, renderer, 48000, 256); // a second of pause
    CHECK (engine.positionSeconds() == Approx (pausedAt));
    CHECK (engine.soundingNoteCount() == 0);
    CHECK (renderer.count (0x80) == 1);   // note-off for C4, not a silence
    CHECK (renderer.silenceCalls == 0);

    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, 48000 * 5, 256);
    CHECK (renderer.count (0x90) == 8);   // C4 not restarted
    CHECK (renderer.count (0x80) == 8);   // its stray original note-off is skipped
}

TEST_CASE ("Seek and stop leave no transport-owned notes")
{
    const auto list = scaleEvents();
    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (44100.0);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        send (engine, renderer, PlaybackCommand::Type::play);
        runFrames (engine, renderer, 3000 + cycle * 977, 512);
        send (engine, renderer, PlaybackCommand::Type::seek, 0.1 * cycle);
        CHECK (engine.soundingNoteCount() == 0);
        runFrames (engine, renderer, 2000, 512);
        send (engine, renderer, PlaybackCommand::Type::stop);
        CHECK (engine.soundingNoteCount() == 0);
        CHECK (engine.positionSeconds() == 0.0);
    }
    runFrames (engine, renderer, 512, 512);
    CHECK (renderer.count (0x90) == renderer.count (0x80));
}

TEST_CASE ("Silence is a separate panic action")
{
    const auto list = scaleEvents();
    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (48000.0);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, 4800, 256);
    send (engine, renderer, PlaybackCommand::Type::silence);
    CHECK (renderer.silenceCalls == 1);
    CHECK (engine.state() == PlaybackState::paused);
    CHECK (engine.soundingNoteCount() == 0);
}

TEST_CASE ("Thirty minutes of playback does not drift")
{
    PlaybackEventList list;
    list.id = 7;
    list.durationSeconds = 1900.0;
    for (int i = 0; i < 3600; ++i)
    {
        list.events.push_back ({ i * 0.5, 0x90, 60, 80 });
        list.events.push_back ({ i * 0.5 + 0.25, 0x80, 60, 64 });
    }

    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (44100.0);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, static_cast<std::int64_t> (1800.0 * 44100.0), 441);

    CHECK (engine.positionSeconds() == Approx (1800.0).margin (1e-9));
    const auto& last = renderer.recorded.back();
    CHECK (last.frame == static_cast<std::int64_t> (std::llround ((3599 * 0.5 + 0.25) * 44100.0)));
}

TEST_CASE ("Commands before a song is loaded are ignored")
{
    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (48000.0);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, 4800, 480);
    CHECK (engine.state() == PlaybackState::stopped);
    CHECK (renderer.recorded.empty());
}

TEST_CASE ("Previewed keys sound on their own channel, with or without a song")
{
    struct Recorder final : audio::IInstrumentRenderer
    {
        void prepare (double, int) override {}
        void render (std::span<const audio::ScheduledMidiEvent> events, audio::RenderBlock) noexcept override
        {
            seen.insert (seen.end(), events.begin(), events.end());
        }
        void allNotesOff() noexcept override {}
        void silence() noexcept override {}
        int reportedLatencyFrames() const noexcept override { return 0; }
        std::vector<audio::ScheduledMidiEvent> seen;
    } renderer;

    audio::PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ audio::PlaybackCommand::Type::preview, 61.0 }, renderer); // C4 down
    engine.process (renderer, { nullptr, 2, 256 });
    engine.handle ({ audio::PlaybackCommand::Type::preview, -61.0 }, renderer); // C4 up
    engine.process (renderer, { nullptr, 2, 256 });

    REQUIRE (renderer.seen.size() == 2);
    CHECK (renderer.seen[0].status == (0x90 | audio::PlaybackCommand::previewChannel));
    CHECK (renderer.seen[0].data1 == 60);
    CHECK (renderer.seen[1].status == (0x80 | audio::PlaybackCommand::previewChannel));
    CHECK (renderer.seen[1].data1 == 60);
    CHECK (engine.state() == audio::PlaybackState::stopped);
}

TEST_CASE ("A gate boundary behind the playhead holds in place, never jumps back")
{
    struct Null final : audio::IInstrumentRenderer
    {
        void prepare (double, int) override {}
        void render (std::span<const audio::ScheduledMidiEvent>, audio::RenderBlock) noexcept override {}
        void allNotesOff() noexcept override {}
        void silence() noexcept override {}
        int reportedLatencyFrames() const noexcept override { return 0; }
    } renderer;

    audio::PlaybackEventList list;
    list.durationSeconds = 10.0;
    list.events.push_back ({ 5.0, 0x90, 60, 90 });
    list.events.push_back ({ 6.0, 0x80, 60, 64 });
    audio::PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ audio::PlaybackCommand::Type::setEvents, 0.0, &list }, renderer);
    engine.handle ({ audio::PlaybackCommand::Type::play }, renderer);
    for (int i = 0; i < 100; ++i) // about 1.07 s
        engine.process (renderer, { nullptr, 2, 512 });
    const auto before = engine.positionSeconds();

    engine.handle ({ audio::PlaybackCommand::Type::setGate, 0.5 }, renderer); // already passed
    engine.process (renderer, { nullptr, 2, 512 });
    CHECK (engine.state() == audio::PlaybackState::waitingForGate);
    CHECK (engine.positionSeconds() == Approx (before));

    engine.handle ({ audio::PlaybackCommand::Type::setGate, 3.0 }, renderer);
    CHECK (engine.state() == audio::PlaybackState::playing);
}

TEST_CASE ("Tempo slows the song without moving the played position")
{
    const auto list = scaleEvents();
    PlaybackEngine engine;
    RecordingRenderer renderer;
    engine.prepare (48000.0);
    send (engine, renderer, PlaybackCommand::Type::setEvents, 0.0, &list);
    send (engine, renderer, PlaybackCommand::Type::play);
    runFrames (engine, renderer, 24000, 256); // 0.5 s of song at 100%
    const auto before = engine.positionSeconds();

    send (engine, renderer, PlaybackCommand::Type::setRate, 0.5);
    CHECK (engine.rate() == Approx (0.5));
    CHECK (engine.positionSeconds() == Approx (before)); // no jump

    // F4 starts at song 1.5 s: 1.0 s of song after `before` takes 2 s at half speed.
    runFrames (engine, renderer, 48000 * 3, 256);
    const auto playedFrames = static_cast<std::int64_t> (std::ceil (24000.0 / 256.0)) * 256;
    for (const auto& e : renderer.recorded)
        if (e.status == 0x90 && e.data1 == 65)
            CHECK (e.frame == playedFrames + std::llround ((1.5 - before) * 2.0 * 48000.0));

    send (engine, renderer, PlaybackCommand::Type::setRate, 100.0);
    CHECK (engine.rate() == Approx (PlaybackEngine::maxRate));
    send (engine, renderer, PlaybackCommand::Type::setRate, 0.0);
    CHECK (engine.rate() == Approx (PlaybackEngine::minRate));
}
