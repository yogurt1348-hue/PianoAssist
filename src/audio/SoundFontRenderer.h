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

/** Plays a SoundFont 2 (.sf2) bank through TinySoundFont: General MIDI
    programs per channel, drums on channel 10, sustain and other controllers.
    Loading happens off the audio thread; rendering never allocates (voices
    and channels are preallocated in prepare). */
class SoundFontRenderer final : public IInstrumentRenderer
{
public:
    /** Reads the file (UTF-8 path, bounded size) and parses it. */
    static Result<std::unique_ptr<SoundFontRenderer>> load (const std::filesystem::path& path);
    static Result<std::unique_ptr<SoundFontRenderer>> fromMemory (const std::vector<std::uint8_t>& bytes, std::string name);

    ~SoundFontRenderer() override;

    const std::string& name() const noexcept { return name_; }
    int presetCount() const noexcept;

    void prepare (double sampleRate, int maxBlockFrames) override;
    void render (std::span<const ScheduledMidiEvent> events, RenderBlock block) noexcept override;
    void allNotesOff() noexcept override;
    void silence() noexcept override;
    int reportedLatencyFrames() const noexcept override { return 0; }
    int activeVoiceCount() const noexcept override;

private:
    SoundFontRenderer (tsf* font, std::string name);
    void handle (const ScheduledMidiEvent& event) noexcept;
    void renderSegment (RenderBlock block, int start, int count) noexcept;

    tsf* font_ = nullptr;
    std::string name_;
    std::vector<float> interleaved_;
};

} // namespace pianoassist::audio
