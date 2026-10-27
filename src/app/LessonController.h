#pragma once

#include "app/AudioService.h"
#include "app/MidiInputService.h"
#include "diagnostics/RotatingLog.h"
#include "lesson_engine/PracticeClock.h"
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

/** The pitches of one gate, split by the hand that plays them. */
struct GateHands
{
    std::vector<std::uint8_t> right;
    std::vector<std::uint8_t> left;
};

/** What the practice part of the window shows. */
struct PracticeSnapshot
{
    lesson::Tolerance tolerance = lesson::Tolerance::normal;
    lesson::Accompaniment accompaniment;
    GateHands current;                      ///< the gate to play now
    std::vector<GateHands> upcoming;        ///< the next few gates after it
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
    int countInBeat = 0;  ///< 1-based click of a running count-in, 0 when none
    int countInBeats = 0;
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
    /** A new song starts at 0; a remapped song (same file, parts changed)
        keeps the position. Either way playback stops. */
    void loadSong (std::shared_ptr<const timeline::Song> song, bool keepPosition = false);
    void playPause();
    void stop();
    void seek (double seconds);
    void silence();
    void setPractising (bool practising);
    void setMode (lesson::PracticeMode mode);
    void setProfile (lesson::PracticeProfile profile);
    void setAccompaniment (lesson::Accompaniment accompaniment);
    /** Per song part: true silences it (the "Other tracks" tab). */
    void setMutedParts (std::vector<bool> muted);
    /** Tempo as song seconds per real second (0.25-2.0); keeps the position. */
    void setTempo (double rate);
    /** Practise a range again and again; disabled = play through. */
    void setLoop (bool enabled, double startSeconds, double endSeconds);
    /** Per-part volume 0-1 (scales velocities); 0 silences the part. */
    void setPartGains (std::vector<double> gains);
    void previewKey (std::uint8_t pitch, bool down); ///< a click on the on-screen keyboard
    /** One bar of clicks before Play and before each loop repeat. */
    void setCountIn (bool on);

    PracticeSnapshot snapshot() const;

private:
    struct LoadSong { std::shared_ptr<const timeline::Song> song; bool keepPosition; };
    struct PlayPause {};
    struct Stop {};
    struct Seek { double seconds; };
    struct Silence {};
    struct SetPractising { bool on; };
    struct SetMode { lesson::PracticeMode mode; };
    struct SetProfile { lesson::PracticeProfile profile; };
    struct SetAccompaniment { lesson::Accompaniment accompaniment; };
    struct SetMutedParts { std::vector<bool> muted; };
    struct PreviewKey { std::uint8_t pitch; bool down; };
    struct SetTempo { double rate; };
    struct SetLoop { bool enabled; double start; double end; };
    struct SetPartGains { std::vector<double> gains; };
    struct SetCountIn { bool on; };
    using Intent = std::variant<LoadSong, PlayPause, Stop, Seek, Silence, SetPractising, SetMode, SetProfile, SetAccompaniment,
                                SetMutedParts, PreviewKey, SetTempo, SetLoop, SetPartGains, SetCountIn>;

    void post (Intent intent);
    void hiResTimerCallback() override;
    void handle (const Intent& intent);
    void rebuild (double position, const char* why);
    void checkLoop (const AudioService::Snapshot& audio);
    /** Plays now, or after a count-in when it is on. */
    void startPlaying (double position);
    void tickCountIn();
    void cancelCountIn();
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
    lesson::Accompaniment accompaniment_;
    std::vector<bool> mutedParts_;
    std::vector<double> partGains_;
    SetLoop loop_ { false, 0.0, 0.0 };
    bool loopJumping_ = false;
    std::int64_t loopJumpMicros_ = 0;
    bool countInEnabled_ = false;
    struct CountInRun
    {
        bool active = false;
        lesson::CountIn plan;
        std::int64_t startMicros = 0;
        int clicked = 0;
        std::int64_t offDueMicros = -1;
        std::uint8_t lastPitch = 96;
    } countIn_;
    std::optional<lesson::PracticeSession> session_;
    std::vector<GateHands> hands_; ///< parallel to the session's gates
    std::uint64_t seenArrivals_ = 0;
    std::int64_t waitStartedMicros_ = 0;
    audio::PlaybackState lastState_ = audio::PlaybackState::stopped;
    std::deque<std::pair<std::uint8_t, std::int64_t>> wrong_;
    std::string lastVerdict_;

    mutable std::mutex snapshotMutex_;
    PracticeSnapshot snapshot_;
};

} // namespace pianoassist::app
