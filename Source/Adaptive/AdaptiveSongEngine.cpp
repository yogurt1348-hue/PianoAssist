#include "AdaptiveSongEngine.h"

#include <cstdlib>

namespace cadenza::adaptive
{
AdaptiveSongEngine::AdaptiveSongEngine(cadenza::audio::AudioEngine& audio)
    : m_audio(audio), m_sink(audio)
{
#if JUCE_DEBUG
    m_logDiagnostics = true;
#endif
    if (const char* env = std::getenv("CADENZA_ADAPTIVE_DEBUG"))
        m_logDiagnostics = std::string(env) == "1";
    m_session.setDiagnosticsEnabled(m_logDiagnostics);
}

double AdaptiveSongEngine::nowSeconds() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void AdaptiveSongEngine::install()
{
    m_audio.setOnBlock([this](int numFrames, double sampleRate) {
        if (numFrames <= 0 || sampleRate <= 0.0) return;
        m_session.processBlock(nowSeconds(), static_cast<double>(numFrames) / sampleRate, m_sink);
    });
}

std::unique_ptr<MidiFileData> AdaptiveSongEngine::readMidi(const juce::File& file, juce::String& error)
{
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block)) {
        error = "Cannot read " + file.getFullPathName();
        return nullptr;
    }
    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    std::vector<std::uint8_t> bytes(data, data + block.getSize());
    auto parsed = parseMidiFile(bytes);
    if (!parsed.ok) {
        error = file.getFileName() + ": " + juce::String(parsed.error);
        return nullptr;
    }
    for (const auto& w : parsed.data.warnings)
        juce::Logger::writeToLog("[Adaptive] " + file.getFileName() + ": " + juce::String(w));
    return std::make_unique<MidiFileData>(std::move(parsed.data));
}

bool AdaptiveSongEngine::loadMidi(const juce::File& file, juce::String& error)
{
    auto midi = readMidi(file, error);
    if (!midi) return false;
    m_session.stop();
    m_midi = std::move(midi);
    m_midiName = file.getFileName();
    juce::Logger::writeToLog("[Adaptive] Loaded " + m_midiName + " (" + juce::String(static_cast<int>(m_midi->tracks.size()))
                             + " tracks, PPQ " + juce::String(m_midi->ppq) + ")");
    return true;
}

bool AdaptiveSongEngine::loadBackingMidi(const juce::File& file, juce::String& error)
{
    auto midi = readMidi(file, error);
    if (!midi) return false;
    m_session.stop();
    m_backingMidi = std::move(midi);
    m_backingName = file.getFileName();
    return true;
}

void AdaptiveSongEngine::clearBackingMidi()
{
    m_session.stop();
    m_backingMidi.reset();
    m_backingName.clear();
}

AdaptiveSongBuildResult AdaptiveSongEngine::apply(const AdaptiveSongOptions& options)
{
    AdaptiveSongBuildResult result;
    if (!m_midi) {
        result.error = "Load a MIDI file first";
        return result;
    }
    result = buildAdaptiveSong(*m_midi, m_backingMidi.get(), options);
    if (!result.ok) {
        juce::Logger::writeToLog("[Adaptive] Cannot build song: " + juce::String(result.error));
        return result;
    }
    for (const auto& w : result.song.warnings)
        juce::Logger::writeToLog("[Adaptive] warning: " + juce::String(w));
    m_session.load(result.song);
    return result;
}

void AdaptiveSongEngine::play()
{
    m_loggedState = TrackingState::Idle;
    m_session.play();
    juce::Logger::writeToLog("[Adaptive] Play: waiting for the performer");
}

void AdaptiveSongEngine::stop()
{
    m_session.stop();
}

void AdaptiveSongEngine::setDiagnosticsLogging(bool on)
{
    m_logDiagnostics = on;
    m_session.setDiagnosticsEnabled(on);
}

void AdaptiveSongEngine::timerTick()
{
    const double now = nowSeconds();
    m_session.update(now);
    if (!m_session.playRequested() && !m_logDiagnostics) return;

    // State transitions are rare: always logged (useful in the field, no spam).
    const auto st = m_session.status(now);
    if (st.state != m_loggedState) {
        juce::Logger::writeToLog(juce::String("[Adaptive] state ") + toString(m_loggedState) + " -> " + toString(st.state)
                                 + " at group " + juce::String(st.groupIndex) + ", " + juce::String(st.estimatedBpm, 1)
                                 + " BPM, confidence " + juce::String(st.confidence, 2));
        m_loggedState = st.state;
    }

    if (!m_logDiagnostics) return;
    for (const auto& d : m_session.drainDiagnostics()) {
        juce::Logger::writeToLog(juce::String::formatted(
            "[Adaptive] note=%d t=%.3f %s group=%d expectedTick=%.0f followerTick=%.0f playheadTick=%.0f "
            "phase=%+.0fms bpm(rate)=%.3f conf=%.2f margin=%.2f state=%s",
            d.note.pitch, d.note.liveSec, toString(d.note.kind), d.note.groupIndex, d.expectedTick,
            d.followerTick, d.playheadTick, d.phaseErrorMs, d.note.rate, d.note.confidence, d.note.margin,
            toString(d.note.state)));
    }
}
}
