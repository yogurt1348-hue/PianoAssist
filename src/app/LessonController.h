#pragma once

#include "app/AudioService.h"
#include "app/MidiInputService.h"
#include "diagnostics/RotatingLog.h"
#include "lesson_engine/PracticeSession.h"
#include "timeline/Song.h"

#include <juce_core/juce_core.h>

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace pianoassist::app
{

/** What the practice part of the window shows. */
struct PracticeSnapshot
{
    bool practising = false;
    lesson::PracticeMode mode = lesson::PracticeMode::bothHands;
    bool waiting = false;                   ///< transport held at a gate
    bool accompanimentOnly = false;         ///< playing, next gate far away
    std::vector<std::uint8_t> missing;      ///< current gate, not yet played
    std::vector<std::uint8_t> matched;      ///< current gate, already played
    std::vector<std::uint8_t> recentWrong;  ///< wrong presses in the last moment
    std::size_t gateNumber = 0;             ///< 1-based current gate, 0 when none
    std::size_t gateCount = 0;
    std::string lastVerdict;
};

/** The control thread. Owns transport transitions and gate decisions: user
    intents and timestamped piano input arrive here, commands leave for the
    audio thread. Runs on its own high-resolution timer so gate decisions do
    not depend on UI frame rate. */
class LessonController final : private juce::HighResolutionTimer
{
public:
    LessonController (diagnostics::RotatingLog& log, AudioService& audio, MidiInputService::InputQueue& input,
                      lesson::PracticeProfile profile, double inputOffsetMs);
    ~LessonController() override;

    void start();

    // Thread-safe intents from the UI thread.
    void loadSong (std::shared_ptr<const timeline::Song> song);
    void playPause();
    void stop();
    void seek (double seconds);
    void silence();
    void setPractising (bool practising);
    void setMode (lesson::PracticeMode mode);

    PracticeSnapshot snapshot() const;

private:
    struct LoadSong { std::shared_ptr<const timeline::Song> song; };
    struct PlayPause {};
    struct Stop {};
    struct Seek { double seconds; };
    struct Silence {};
    struct SetPractising { bool on; };
    struct SetMode { lesson::PracticeMode mode; };
    using Intent = std::variant<LoadSong, PlayPause, Stop, Seek, Silence, SetPractising, SetMode>;

    void post (Intent intent);
    void hiResTimerCallback() override;
    void handle (const Intent& intent);
    void rebuild (double position, const char* why);
    void resetGates (double position);
    void sendGate();
    void handleInput (const midi_io::InputEvent& event, const AudioService::Snapshot& audio);
    void publish (const AudioService::Snapshot& audio);
    void logTransport (const char* message, const AudioService::Snapshot& audio);

    diagnostics::RotatingLog& log_;
    AudioService& audio_;
    MidiInputService::InputQueue& input_;
    lesson::PracticeProfile profile_;
    double inputOffsetMs_;

    std::mutex intentsMutex_;
    std::deque<Intent> intents_;

    // Control thread only.
    std::shared_ptr<const timeline::Song> song_;
    bool practising_ = false;
    lesson::PracticeMode mode_ = lesson::PracticeMode::bothHands;
    std::optional<lesson::PracticeSession> session_;
    std::uint64_t seenArrivals_ = 0;
    std::int64_t waitStartedMicros_ = 0;
    audio::PlaybackState lastState_ = audio::PlaybackState::stopped;
    std::deque<std::pair<std::uint8_t, std::int64_t>> wrong_;
    std::string lastVerdict_;

    mutable std::mutex snapshotMutex_;
    PracticeSnapshot snapshot_;
};

} // namespace pianoassist::app
