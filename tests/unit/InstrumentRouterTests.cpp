#include "audio/InstrumentRouter.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace pianoassist::audio;

namespace
{
/** Writes a constant level and records the channels of the events it got. */
class LevelRenderer final : public IInstrumentRenderer
{
public:
    explicit LevelRenderer (float level) : level_ (level) {}
    void prepare (double, int) override {}
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override
    {
        for (const auto& e : events)
            channels.push_back (e.status & 0x0F);
        for (int ch = 0; ch < block.numChannels; ++ch)
            std::fill_n (block.channels[ch], block.numFrames, level_);
        ++calls;
    }
    void allNotesOff() noexcept override {}
    void silence() noexcept override { ++silenced; }
    int reportedLatencyFrames() const noexcept override { return 0; }
    int activeVoiceCount() const noexcept override { return 2; }

    std::vector<int> channels;
    int calls = 0;
    int silenced = 0;

private:
    float level_;
};

struct Buffer
{
    explicit Buffer (int frames) : left (static_cast<std::size_t> (frames), -1.0f), right (static_cast<std::size_t> (frames), -1.0f) {}
    RenderBlock block() { return { pointers.data(), 2, static_cast<int> (left.size()) }; }
    std::vector<float> left, right;
    std::array<float*, 2> pointers { left.data(), right.data() };
};
} // namespace

TEST_CASE ("One instrument renders every channel directly")
{
    InstrumentRouter router;
    LevelRenderer main (0.25f);
    InstrumentRouting routing;
    routing.main = &main;
    Buffer buffer (256);
    const std::vector<ScheduledMidiEvent> events { { 0, 0x90, 60, 90 }, { 10, 0x99, 36, 90 } };
    router.render (routing, events, buffer.block());
    CHECK (main.channels == std::vector<int> { 0, 9 });
    CHECK (buffer.left[100] == 0.25f);
}

TEST_CASE ("Channels routed to their own instrument are split and mixed")
{
    InstrumentRouter router;
    LevelRenderer main (0.25f), bass (0.5f);
    InstrumentRouting routing;
    routing.main = &main;
    routing.channels[1] = &bass;
    Buffer buffer (512);
    const std::vector<ScheduledMidiEvent> events { { 0, 0x90, 60, 90 }, { 5, 0x91, 40, 90 }, { 7, 0xC1, 33, 0 } };
    router.render (routing, events, buffer.block());
    CHECK (main.channels == std::vector<int> { 0 });
    CHECK (bass.channels == std::vector<int> { 1, 1 });
    CHECK (buffer.left[0] == 0.75f);
    CHECK (buffer.right[511] == 0.75f);
    CHECK (InstrumentRouter::activeVoices (routing) == 4);
}

TEST_CASE ("Large device blocks are rendered in chunks with shifted offsets")
{
    InstrumentRouter router;
    LevelRenderer main (0.1f), strings (0.2f);
    InstrumentRouting routing;
    routing.main = &main;
    routing.channels[2] = &strings;
    Buffer buffer (InstrumentRouter::chunkFrames + 100);
    const std::vector<ScheduledMidiEvent> events { { InstrumentRouter::chunkFrames + 50, 0x92, 70, 90 } };
    router.render (routing, events, buffer.block());
    CHECK (main.calls == 2);
    CHECK (strings.channels == std::vector<int> { 2 });
    CHECK (buffer.left.back() == 0.3f);
}

TEST_CASE ("Instruments dropped from the routing are silenced, kept ones are not")
{
    LevelRenderer main (0.0f), bass (0.0f), drums (0.0f);
    InstrumentRouting before, after;
    before.main = &main;
    before.channels[1] = &bass;
    before.channels[9] = &drums;
    after.main = &main;
    after.channels[9] = &drums;
    InstrumentRouter::silenceDropped (before, after);
    CHECK (bass.silenced == 1);
    CHECK (main.silenced == 0);
    CHECK (drums.silenced == 0);
}
