#include "audio/SoundFontRenderer.h"

#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

#include <algorithm>
#include <limits>

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wconversion"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wfloat-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define TSF_IMPLEMENTATION
#include <tsf.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace pianoassist::audio
{
namespace
{
constexpr std::uintmax_t maxSoundFontBytes = 2'000'000'000ull;
constexpr int maxVoices = 256;
constexpr int drumChannel = 9; // MIDI channel 10
} // namespace

Result<std::unique_ptr<SoundFontRenderer>> SoundFontRenderer::load (const std::filesystem::path& path)
{
    using R = Result<std::unique_ptr<SoundFontRenderer>>;
    auto bytes = readBinaryFile (path, maxSoundFontBytes);
    if (! bytes)
        return R::fail (bytes.error());
    auto name = pathToUtf8 (path.stem());
    return fromMemory (bytes.value(), std::move (name));
}

Result<std::unique_ptr<SoundFontRenderer>> SoundFontRenderer::fromMemory (const std::vector<std::uint8_t>& bytes,
                                                                          std::string name)
{
    using R = Result<std::unique_ptr<SoundFontRenderer>>;
    if (bytes.size() < 12 || bytes.size() > static_cast<std::size_t> (std::numeric_limits<int>::max()))
        return R::fail ("'" + name + "' is not a SoundFont (.sf2) file");
    auto* font = tsf_load_memory (bytes.data(), static_cast<int> (bytes.size()));
    if (font == nullptr)
        return R::fail ("'" + name + "' is not a valid SoundFont 2 (.sf2) file");
    if (tsf_get_presetcount (font) <= 0)
    {
        tsf_close (font);
        return R::fail ("'" + name + "' contains no instruments");
    }
    return R::ok (std::unique_ptr<SoundFontRenderer> (new SoundFontRenderer (font, std::move (name))));
}

SoundFontRenderer::SoundFontRenderer (tsf* font, std::string name) : font_ (font), name_ (std::move (name)) {}

SoundFontRenderer::~SoundFontRenderer()
{
    tsf_close (font_);
}

int SoundFontRenderer::presetCount() const noexcept
{
    return tsf_get_presetcount (font_);
}

void SoundFontRenderer::prepare (double sampleRate, int maxBlockFrames)
{
    tsf_set_output (font_, TSF_STEREO_INTERLEAVED, static_cast<int> (sampleRate), 0.0f);
    tsf_set_max_voices (font_, maxVoices); // preallocates: note-ons never allocate
    for (int channel = 0; channel < 16; ++channel)
    {
        // Touching every channel allocates its state now, not on the audio thread.
        tsf_channel_set_presetnumber (font_, channel, 0, channel == drumChannel ? 1 : 0);
        tsf_channel_set_pitchrange (font_, channel, 2.0f);
    }
    interleaved_.assign (static_cast<std::size_t> (std::max (1, maxBlockFrames)) * 2u, 0.0f);
}

void SoundFontRenderer::handle (const ScheduledMidiEvent& e) noexcept
{
    const int channel = e.status & 0x0F;
    switch (e.status & 0xF0)
    {
        case 0x90:
            if (e.data2 > 0)
            {
                tsf_channel_note_on (font_, channel, e.data1, static_cast<float> (e.data2) / 127.0f);
                break;
            }
            [[fallthrough]];
        case 0x80:
            tsf_channel_note_off (font_, channel, e.data1);
            break;
        case 0xB0:
            tsf_channel_midi_control (font_, channel, e.data1, e.data2);
            break;
        case 0xC0:
            tsf_channel_set_presetnumber (font_, channel, e.data1, channel == drumChannel ? 1 : 0);
            break;
        default:
            break;
    }
}

void SoundFontRenderer::renderSegment (RenderBlock block, int start, int count) noexcept
{
    while (count > 0)
    {
        const int chunk = std::min (count, static_cast<int> (interleaved_.size() / 2));
        if (chunk <= 0)
            return;
        tsf_render_float (font_, interleaved_.data(), chunk, 0);
        for (int i = 0; i < chunk; ++i)
        {
            const float left = interleaved_[static_cast<std::size_t> (i) * 2u];
            const float right = interleaved_[static_cast<std::size_t> (i) * 2u + 1u];
            if (block.numChannels == 1 && block.channels[0] != nullptr)
                block.channels[0][start + i] = 0.5f * (left + right);
            for (int ch = 0; ch < block.numChannels && block.numChannels > 1; ++ch)
                if (block.channels[ch] != nullptr)
                    block.channels[ch][start + i] = (ch % 2 == 0) ? left : right;
        }
        start += chunk;
        count -= chunk;
    }
}

void SoundFontRenderer::render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept
{
    // Events arrive in time order; render up to each one, then apply it.
    int position = 0;
    for (const auto& e : events)
    {
        const int at = std::clamp (e.sampleOffset, 0, block.numFrames);
        if (at > position && block.channels != nullptr)
        {
            renderSegment (block, position, at - position);
            position = at;
        }
        handle (e);
    }
    if (block.channels != nullptr && block.numFrames > position)
        renderSegment (block, position, block.numFrames - position);
}

void SoundFontRenderer::allNotesOff() noexcept
{
    tsf_note_off_all (font_);
}

void SoundFontRenderer::silence() noexcept
{
    // tsf's "sounds off" still fades for a moment; the panic action must not.
    // Killing voices keeps channel programs and controllers intact.
    for (auto* v = font_->voices; v != font_->voices + font_->voiceNum; ++v)
        tsf_voice_kill (v);
}

int SoundFontRenderer::activeVoiceCount() const noexcept
{
    return tsf_active_voice_count (font_);
}

} // namespace pianoassist::audio
