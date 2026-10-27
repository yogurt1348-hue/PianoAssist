#pragma once

#include "audio/IInstrumentRenderer.h"
#include "core/Result.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct tsf;

namespace pianoassist::audio
{

/** One preset of a SoundFont: bank and program as MIDI selects them. */
struct SoundFontPreset
{
    int bank = 0;      ///< 128 is the percussion bank
    int program = 0;
    std::string name;
    bool drums = false; ///< a drum kit: percussion bank, or named so
};

/** Plays a SoundFont 2 (.sf2) bank through TinySoundFont: General MIDI
    programs per channel, drums on channel 10, sustain and other controllers.
    Loading happens off the audio thread; rendering never allocates (voices
    and channels are preallocated in prepare).

    The file is parsed once; copy() makes another player that shares its
    samples, so each track can have its own instance and preset cheaply. */
class SoundFontRenderer final : public IInstrumentRenderer
{
public:
    /** Reads the file (UTF-8 path, bounded size) and parses it. */
    static Result<std::unique_ptr<SoundFontRenderer>> load (const std::filesystem::path& path);
    static Result<std::unique_ptr<SoundFontRenderer>> fromMemory (const std::vector<std::uint8_t>& bytes, std::string name);

    ~SoundFontRenderer() override;

    const std::string& name() const noexcept { return name_; }
    int presetCount() const noexcept;
    /** Every preset in the file, sorted by bank and program. */
    std::vector<SoundFontPreset> presets() const;
    bool hasPreset (int bank, int program) const noexcept;

    /** Another player of the same file (message thread; shares the samples). */
    std::unique_ptr<SoundFontRenderer> copy() const;
    /** Plays this preset on every channel and ignores the song's program and
        bank changes. Call before the copy is handed to the audio thread. */
    void lockPreset (int bank, int program) noexcept;
    bool locked() const noexcept { return lockedProgram_ >= 0; }

    void prepare (double sampleRate, int maxBlockFrames) override;
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override;
    void allNotesOff() noexcept override;
    void silence() noexcept override;
    int reportedLatencyFrames() const noexcept override { return 0; }
    int activeVoiceCount() const noexcept override;

private:
    struct Template;
    SoundFontRenderer (std::shared_ptr<Template> source, tsf* font, std::string name);
    void applyLock() noexcept;
    void handle (const ScheduledMidiEvent& event) noexcept;
    void renderSegment (RenderBlock block, int start, int count) noexcept;

    std::shared_ptr<Template> source_; ///< the parsed file, never played itself
    tsf* font_ = nullptr;
    std::string name_;
    int lockedBank_ = -1, lockedProgram_ = -1;
    std::vector<float> interleaved_;
};

} // namespace pianoassist::audio
