#pragma once

#include "app/AudioService.h"
#include "app/MidiInputService.h"
#include "diagnostics/RotatingLog.h"
#include "lesson_engine/PracticeClock.h"
#include "lesson_engine/PracticeSession.h"
#include "timeline/Song.h"

#include <juce_core/juce_core.h>

#include <atomic>
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
    double gateOnsetSeconds = -1.0;         ///< song time of the current gate, -1 when none
    std::size_t gateNumber = 0;             ///< 1-based current gate, 0 when none
    std::size_t gateCount = 0;
    std::string lastVerdict;
    int countInBeat = 0;  ///< 1-based click of a running count-in, 0 when none
    int countInBeats = 0;

    /** Latency test: clicks to press along with, and the measured delay. */
    struct LatencyTest
    {
        bool active = false;
        bool done = false;     ///< finished with a result
        bool failed = false;   ///< finished, too few presses matched a click
        int clicks = 0;        ///< clicks played so far
        int totalClicks = 0;
        int matched = 0;       ///< presses that matched a click
        double resultMs = 0.0; ///< median press minus click
        double spreadMs = 0.0; ///< interquartile range
    } latencyTest;

    /** The player is late by about the same amount on every note: most likely
        the sound reaches them late. seq changes each time it is detected. */
    double steadyLateMs = 0.0;
    std::uint32_t steadyLateSeq = 0;
    double inputOffsetMs = 0.0;
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
    /** Presses are counted this much earlier (sound and input delay). Live. */
    void setInputOffset (double ms);
    /** Stops the song and clicks once a second; the player presses any key
        on each click. The result appears in the snapshot. */
    void startLatencyTest();
    void stopLatencyTest();

    PracticeSnapshot snapshot() const;
    /** Input events taken from the queue so far (any thread). */
    std::uint64_t inputTaken() const noexcept { return inputTaken_.load (std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t> inputTaken_ { 0 };
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
    struct SetInputOffset { double ms; };
    struct StartLatencyTest {};
    struct StopLatencyTest {};
    using Intent = std::variant<LoadSong, PlayPause, Stop, Seek, Silence, SetPractising, SetMode, SetProfile, SetAccompaniment,
                                SetMutedParts, PreviewKey, SetTempo, SetLoop, SetPartGains, SetCountIn, SetInputOffset,
                                StartLatencyTest, StopLatencyTest>;

    void post (Intent intent);
    void hiResTimerCallback() override;
    void handle (const Intent& intent);
    /** live: keep playing from the same position (only the parts that sound
        or are checked change); otherwise playback stops there. */
    void rebuild (double position, const char* why, bool live = false);
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
    /** Every few seconds while the song runs, and when it stops: audio
        timing, what each track played, and how the presses landed. */
    void logHealth (const char* why);
    void tickLatencyTest();
    void finishLatencyTest (const char* why);
    std::string channelNames (int channel) const;

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

    // Timing statistics for the log and the "steadily late" hint.
    std::deque<double> recentOffsets_;  ///< last presses that passed a note, ms
    std::vector<double> windowOffsets_; ///< since the last health entry
    std::vector<double> windowProcMs_;  ///< MIDI arrival to judged, ms
    int windowWrong_ = 0;
    int windowStalls_ = 0;
    double windowStallMs_ = 0.0;
    std::int64_t lastHealthMicros_ = 0;
    bool hintGiven_ = false;
    double steadyLateMs_ = 0.0;
    std::uint32_t steadyLateSeq_ = 0;

    struct LatencyRun
    {
        bool active = false;
        bool done = false;
        bool failed = false;
        std::int64_t startMicros = 0;
        int sent = 0;
        std::int64_t offDueMicros = -1;
        std::int64_t lastSeenPreview = 0;
        std::vector<std::int64_t> clicks; ///< when each click left for the output
        std::vector<std::int64_t> taps;
        double resultMs = 0.0;
        double spreadMs = 0.0;
        int matched = 0;
    } latency_;

    mutable std::mutex snapshotMutex_;
    PracticeSnapshot snapshot_;
};

} // namespace pianoassist::app
