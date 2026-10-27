#include "audio/SoundFontRenderer.h"

#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>

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
std::string lowerCase (std::string text)
{
    for (auto& c : text)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return text;
}

constexpr std::uintmax_t maxSoundFontBytes = 2'000'000'000ull;
constexpr int maxVoices = 256;
constexpr int drumChannel = 9; // MIDI channel 10
} // namespace

namespace
{
std::uint32_t readU32 (const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t> (p[0]) | static_cast<std::uint32_t> (p[1]) << 8 | static_cast<std::uint32_t> (p[2]) << 16
         | static_cast<std::uint32_t> (p[3]) << 24;
}

/** Preset names exactly as in the file's phdr records. TinySoundFont keeps
    only 19 of the 20 characters, which cuts names such as "Acoustic Grand
    Piano"; these are used for display and search instead. */
std::map<std::pair<int, int>, std::string> fullPresetNames (const std::vector<std::uint8_t>& bytes)
{
    std::map<std::pair<int, int>, std::string> names;
    const auto* data = bytes.data();
    const std::size_t size = bytes.size();
    if (size < 12 || std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "sfbk", 4) != 0)
        return names;
    // Top-level LIST chunks, then the phdr chunk inside LIST pdta.
    std::size_t pos = 12;
    while (pos + 12 <= size)
    {
        const std::size_t length = readU32 (data + pos + 4);
        if (length > size - pos - 8)
            break;
        if (std::memcmp (data + pos, "LIST", 4) == 0 && std::memcmp (data + pos + 8, "pdta", 4) == 0)
        {
            std::size_t sub = pos + 12;
            const std::size_t end = pos + 8 + length;
            while (sub + 8 <= end)
            {
                const std::size_t subLength = readU32 (data + sub + 4);
                if (subLength > end - sub - 8)
                    break;
                if (std::memcmp (data + sub, "phdr", 4) == 0)
                {
                    constexpr std::size_t record = 38;
                    const std::size_t count = subLength / record;
                    for (std::size_t i = 0; i + 1 < count; ++i) // the last record is the terminator
                    {
                        const auto* r = data + sub + 8 + i * record;
                        std::string name (reinterpret_cast<const char*> (r), 20);
                        name = name.substr (0, name.find ('\0'));
                        while (! name.empty() && name.back() == ' ')
                            name.pop_back();
                        for (auto& c : name)
                            if (static_cast<unsigned char> (c) < 32 || static_cast<unsigned char> (c) > 126)
                                c = '?'; // the format has no encoding; keep the text valid UTF-8
                        const int program = r[20] | r[21] << 8;
                        const int bank = r[22] | r[23] << 8;
                        names.emplace (std::make_pair (bank, program), std::move (name));
                    }
                    return names;
                }
                sub += 8 + subLength + (subLength & 1);
            }
        }
        pos += 8 + length + (length & 1);
    }
    return names;
}
} // namespace

struct SoundFontRenderer::Template
{
    Template (tsf* f, std::map<std::pair<int, int>, std::string> n) : font (f), names (std::move (n)) {}
    ~Template() { tsf_close (font); }
    Template (const Template&) = delete;
    Template& operator= (const Template&) = delete;
    tsf* font;
    std::map<std::pair<int, int>, std::string> names;
};

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
    auto source = std::make_shared<Template> (font, fullPresetNames (bytes));
    auto* player = tsf_copy (font);
    if (player == nullptr)
        return R::fail ("Not enough memory for '" + name + "'");
    return R::ok (std::unique_ptr<SoundFontRenderer> (new SoundFontRenderer (std::move (source), player, std::move (name))));
}

SoundFontRenderer::SoundFontRenderer (std::shared_ptr<Template> source, tsf* font, std::string name)
    : source_ (std::move (source)), font_ (font), name_ (std::move (name))
{
}

SoundFontRenderer::~SoundFontRenderer()
{
    tsf_close (font_);
}

int SoundFontRenderer::presetCount() const noexcept
{
    return tsf_get_presetcount (font_);
}

std::vector<SoundFontPreset> SoundFontRenderer::presets() const
{
    std::vector<SoundFontPreset> list;
    const auto* f = source_->font;
    for (int i = 0; i < f->presetNum; ++i)
    {
        const auto& p = f->presets[i];
        const auto full = source_->names.find ({ p.bank, p.preset });
        SoundFontPreset preset { p.bank, p.preset, full != source_->names.end() && ! full->second.empty() ? full->second : p.presetName,
                                 p.bank == 128 };
        const auto lower = lowerCase (preset.name);
        if (lower.find ("drum") != std::string::npos || lower.find ("kit") != std::string::npos
            || lower.find ("perc") != std::string::npos)
            preset.drums = preset.drums || p.bank >= 120; // named kits in the high banks some fonts use
        list.push_back (std::move (preset));
    }
    std::sort (list.begin(), list.end(), [] (const SoundFontPreset& a, const SoundFontPreset& b) {
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });
    return list;
}

bool SoundFontRenderer::hasPreset (int bank, int program) const noexcept
{
    return tsf_get_presetindex (source_->font, bank, program) >= 0;
}

std::unique_ptr<SoundFontRenderer> SoundFontRenderer::copy() const
{
    auto* player = tsf_copy (source_->font);
    if (player == nullptr)
        return nullptr;
    return std::unique_ptr<SoundFontRenderer> (new SoundFontRenderer (source_, player, name_));
}

void SoundFontRenderer::lockPreset (int bank, int program) noexcept
{
    lockedBank_ = bank;
    lockedProgram_ = program;
}

void SoundFontRenderer::applyLock() noexcept
{
    if (lockedProgram_ < 0)
        return;
    for (int channel = 0; channel < 16; ++channel)
        tsf_channel_set_bank_preset (font_, channel, lockedBank_, lockedProgram_);
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
    applyLock();
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
            if (locked() && (e.data1 == 0 || e.data1 == 32))
                break; // a chosen preset ignores the song's bank selects
            tsf_channel_midi_control (font_, channel, e.data1, e.data2);
            if (locked() && e.data1 == 121)
                tsf_channel_set_bank_preset (font_, channel, lockedBank_, lockedProgram_);
            break;
        case 0xC0:
            if (locked())
                break; // and its program changes
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
