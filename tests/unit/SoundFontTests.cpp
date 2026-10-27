// SoundFont (.sf2) playback: loading, sample-accurate notes, programs,
// release vs. silence, and safe failure on bad files.
#include "Fixtures.h"
#include "audio/PlaybackEngine.h"
#include "audio/SoundFontRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace pianoassist;
using namespace pianoassist::audio;

namespace
{
struct Stereo
{
    explicit Stereo (int frames) : left (static_cast<std::size_t> (frames)), right (static_cast<std::size_t> (frames)) {}
    RenderBlock block() { return { pointers.data(), 2, static_cast<int> (left.size()) }; }
    float peak (int from = 0) const
    {
        float p = 0.0f;
        for (std::size_t i = static_cast<std::size_t> (from); i < left.size(); ++i)
            p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });
        return p;
    }
    std::vector<float> left, right;
    std::array<float*, 2> pointers { left.data(), right.data() };
};

std::unique_ptr<SoundFontRenderer> tinyFont()
{
    auto font = SoundFontRenderer::fromMemory (test::readFixture ("tiny_sine.sf2"), "tiny_sine");
    REQUIRE (font);
    auto renderer = std::move (font).value();
    renderer->prepare (48000.0, 512);
    return renderer;
}
} // namespace

TEST_CASE ("A SoundFont loads and sounds from the exact sample of the note-on")
{
    auto font = tinyFont();
    CHECK (font->presetCount() == 1);

    Stereo out (512);
    const ScheduledMidiEvent on[] { { 200, 0x90, 69, 100 } };
    font->render (on, out.block());
    for (std::size_t i = 0; i < 200; ++i)
        CHECK (out.left[i] == 0.0f);
    CHECK (out.peak (200) > 0.01f);
    CHECK (font->activeVoiceCount() > 0);
}

TEST_CASE ("Silence stops SoundFont voices at once")
{
    auto font = tinyFont();
    Stereo out (512);
    const ScheduledMidiEvent on[] { { 0, 0x90, 60, 100 } };
    font->render (on, out.block());

    font->silence();
    Stereo after (512);
    font->render ({}, after.block());
    CHECK (after.peak() == 0.0f);
    CHECK (font->activeVoiceCount() == 0);
}

TEST_CASE ("Programs and drums never crash a bank that lacks them")
{
    auto font = tinyFont();
    Stereo out (256);
    const ScheduledMidiEvent events[] { { 0, 0xC1, 33, 0 }, { 0, 0x91, 40, 90 }, { 10, 0x99, 36, 100 },
                                        { 20, 0xB0, 64, 127 }, { 30, 0x80, 60, 64 }, { 40, 0xB0, 123, 0 } };
    font->render (events, out.block());
    font->allNotesOff();
    SUCCEED ("missing presets are skipped");
}

TEST_CASE ("Bad SoundFont files fail with a message, not a crash")
{
    CHECK_FALSE (SoundFontRenderer::fromMemory ({}, "empty"));
    auto midi = SoundFontRenderer::fromMemory (test::readFixture ("format0_c_major_scale_120bpm.mid"), "song");
    REQUIRE_FALSE (midi);
    CHECK (midi.error().find ("SoundFont") != std::string::npos);
    CHECK_FALSE (SoundFontRenderer::load (test::fixturePath ("missing.sf2")));
}

TEST_CASE ("The engine sends each channel's program when a song is loaded")
{
    struct Recorder final : IInstrumentRenderer
    {
        void prepare (double, int) override {}
        void render (std::span<const ScheduledMidiEvent> events, RenderBlock) noexcept override
        {
            seen.insert (seen.end(), events.begin(), events.end());
        }
        void allNotesOff() noexcept override {}
        void silence() noexcept override {}
        int reportedLatencyFrames() const noexcept override { return 0; }
        std::vector<ScheduledMidiEvent> seen;
    } renderer;

    PlaybackEventList list;
    list.programs[1] = 33; // bass
    list.programs[2] = 48; // strings
    PlaybackEngine engine;
    engine.prepare (48000.0);
    engine.handle ({ PlaybackCommand::Type::setEvents, 0.0, &list }, renderer);
    engine.process (renderer, { nullptr, 2, 64 });

    std::vector<std::pair<int, int>> programs;
    for (const auto& e : renderer.seen)
        if ((e.status & 0xF0) == 0xC0)
            programs.push_back ({ e.status & 0x0F, e.data1 });
    CHECK (programs == std::vector<std::pair<int, int>> { { 1, 33 }, { 2, 48 } });
}
