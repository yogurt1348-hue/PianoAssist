#include "app/LessonController.h"

#include "core/NoteName.h"
#include "lesson_engine/Gate.h"

#include <algorithm>
#include <unordered_map>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;
using audio::PlaybackState;

constexpr int tickMs = 2;
constexpr std::int64_t wrongFlashMicros = 400'000;
constexpr double accompanimentOnlyAfterSeconds = 1.5;
constexpr std::size_t upcomingGateCount = 3;

std::vector<GateHands> splitByHand (const timeline::Timeline& timeline, const std::vector<lesson::Gate>& gates)
{
    std::unordered_map<timeline::NoteId, const timeline::Note*> byId;
    for (const auto& note : timeline.notes())
        byId.emplace (note.id, &note);

    std::vector<GateHands> hands;
    hands.reserve (gates.size());
    for (const auto& gate : gates)
    {
        GateHands h;
        for (const auto id : gate.noteIds)
        {
            const auto it = byId.find (id);
            if (it == byId.end())
                continue;
            auto& list = it->second->role == timeline::NoteRole::leftHand ? h.left : h.right;
            if (std::find (list.begin(), list.end(), it->second->pitch) == list.end())
                list.push_back (it->second->pitch);
        }
        std::sort (h.right.begin(), h.right.end());
        std::sort (h.left.begin(), h.left.end());
        hands.push_back (std::move (h));
    }
    return hands;
}

std::string names (const std::vector<std::uint8_t>& pitches)
{
    std::string text;
    for (const auto pitch : pitches)
        text += (text.empty() ? "" : " ") + noteName (pitch);
    return text;
}

std::string seconds (double value)
{
    return juce::String (value, 3).toStdString();
}
} // namespace

LessonController::LessonController (diagnostics::RotatingLog& log, AudioService& audio,
                                    MidiInputService::InputQueue& input, lesson::PracticeProfile profile,
                                    double inputOffsetMs)
    : log_ (log), audio_ (audio), input_ (input), profile_ (std::move (profile)), inputOffsetMs_ (inputOffsetMs)
{
}

LessonController::~LessonController()
{
    stopTimer();
}

void LessonController::start()
{
    log_.write (LogLevel::info, "lesson", "Lesson thread starting",
                { { "tickMs", std::to_string (tickMs) },
                  { "earlyMs", juce::String (profile_.onsetEarlyMs, 0).toStdString() },
                  { "lateMs", juce::String (profile_.onsetLateMs, 0).toStdString() },
                  { "inputOffsetMs", juce::String (inputOffsetMs_, 1).toStdString() } });
    startTimer (tickMs);
}

void LessonController::post (Intent intent)
{
    std::scoped_lock lock (intentsMutex_);
    intents_.push_back (std::move (intent));
}

void LessonController::loadSong (std::shared_ptr<const timeline::Song> song) { post (LoadSong { std::move (song) }); }
void LessonController::playPause() { post (PlayPause {}); }
void LessonController::stop() { post (Stop {}); }
void LessonController::seek (double s) { post (Seek { s }); }
void LessonController::silence() { post (Silence {}); }
void LessonController::setPractising (bool on) { post (SetPractising { on }); }
void LessonController::setMode (lesson::PracticeMode mode) { post (SetMode { mode }); }
void LessonController::setProfile (lesson::PracticeProfile profile) { post (SetProfile { std::move (profile) }); }
void LessonController::previewKey (std::uint8_t pitch, bool down) { post (PreviewKey { pitch, down }); }

PracticeSnapshot LessonController::snapshot() const
{
    std::scoped_lock lock (snapshotMutex_);
    return snapshot_;
}

void LessonController::logTransport (const char* message, const AudioService::Snapshot& audio)
{
    log_.write (LogLevel::info, "transport", message,
                { { "position", seconds (audio.positionSeconds) }, { "state", audio::toString (audio.state) },
                  { "practice", practising_ ? std::string (lesson::toString (mode_)) : "off" } });
}

void LessonController::rebuild (double position, const char* why)
{
    if (! song_)
        return;
    const auto mode = mode_;
    const bool practising = practising_;
    // The learner plays the practised hands on the piano; the app plays the rest.
    audio_.loadSong (*song_, [mode, practising] (timeline::NoteRole role) {
        return ! practising || ! lesson::isRequired (role, mode);
    });
    if (position > 0.0)
        audio_.seek (position);

    if (practising_)
    {
        session_.emplace (lesson::buildGates (song_->timeline, mode_), profile_);
        hands_ = splitByHand (song_->timeline, session_->gates());
        log_.write (LogLevel::info, "lesson", "Practice prepared",
                    { { "reason", why }, { "mode", std::string (lesson::toString (mode_)) },
                      { "gates", std::to_string (session_->gates().size()) } });
    }
    else
    {
        session_.reset();
        hands_.clear();
    }
    resetGates (position);
}

void LessonController::resetGates (double position)
{
    wrong_.clear();
    if (session_)
        session_->resetTo (position);
    sendGate();
}

void LessonController::sendGate()
{
    const auto next = session_ ? session_->nextBoundarySeconds() : std::nullopt;
    audio_.setGate (next ? *next : -1.0);
}

void LessonController::handle (const Intent& intent)
{
    const auto audio = audio_.snapshot();
    std::visit (
        [&] (const auto& i) {
            using T = std::decay_t<decltype (i)>;
            if constexpr (std::is_same_v<T, LoadSong>)
            {
                song_ = i.song;
                rebuild (0.0, "song loaded");
            }
            else if constexpr (std::is_same_v<T, PlayPause>)
            {
                if (! song_)
                    return;
                if (audio.state == PlaybackState::playing || audio.state == PlaybackState::waitingForGate)
                {
                    logTransport ("User pressed Pause", audio);
                    audio_.pause();
                }
                else
                {
                    logTransport ("User pressed Play", audio);
                    if (audio.state == PlaybackState::completed)
                        resetGates (0.0);
                    audio_.play();
                }
            }
            else if constexpr (std::is_same_v<T, Stop>)
            {
                logTransport ("User pressed Stop", audio);
                audio_.stop();
                resetGates (0.0);
            }
            else if constexpr (std::is_same_v<T, Seek>)
            {
                log_.write (LogLevel::info, "transport", "User seeked", { { "to", seconds (i.seconds) } });
                audio_.seek (i.seconds);
                resetGates (i.seconds);
            }
            else if constexpr (std::is_same_v<T, Silence>)
            {
                logTransport ("User pressed Silence", audio);
                audio_.silence();
            }
            else if constexpr (std::is_same_v<T, SetPractising>)
            {
                if (practising_ == i.on)
                    return;
                practising_ = i.on;
                log_.write (LogLevel::info, "lesson", i.on ? "Practice turned on" : "Practice turned off");
                audio_.stop();
                rebuild (audio.positionSeconds, i.on ? "practice on" : "practice off");
            }
            else if constexpr (std::is_same_v<T, SetMode>)
            {
                if (mode_ == i.mode)
                    return;
                mode_ = i.mode;
                log_.write (LogLevel::info, "lesson", "Practice mode changed", { { "mode", std::string (lesson::toString (mode_)) } });
                audio_.stop();
                rebuild (audio.positionSeconds, "mode changed");
            }
            else if constexpr (std::is_same_v<T, SetProfile>)
            {
                if (profile_ == i.profile)
                    return;
                profile_ = i.profile;
                log_.write (LogLevel::info, "lesson", "Tolerance changed",
                            { { "name", profile_.name },
                              { "earlyMs", juce::String (profile_.onsetEarlyMs, 0).toStdString() },
                              { "lateMs", juce::String (profile_.onsetLateMs, 0).toStdString() } });
                if (session_)
                {
                    // Keep playing: only the windows change, from the current note on.
                    const auto position = audio.positionSeconds;
                    session_.emplace (lesson::buildGates (song_->timeline, mode_), profile_);
                    resetGates (position);
                }
            }
            else if constexpr (std::is_same_v<T, PreviewKey>)
            {
                log_.write (LogLevel::debug, "ui", i.down ? "On-screen key down" : "On-screen key up",
                            { { "note", noteName (i.pitch) } });
                audio_.previewKey (i.pitch, i.down);
            }
        },
        intent);
}

void LessonController::handleInput (const midi_io::InputEvent& event, const AudioService::Snapshot& audio)
{
    if (event.type != midi_io::InputEventType::noteOn || ! session_)
        return;
    if (audio.state != PlaybackState::playing && audio.state != PlaybackState::waitingForGate)
        return; // gates only judge presses while the song runs or waits

    // Map the press to song time. While held at a gate, song time is frozen
    // at the boundary; the wall time since arrival measures lateness.
    const auto offset = inputOffsetMs_ / 1000.0;
    const auto boundary = session_->nextBoundarySeconds();
    double songTime = 0.0;
    if (audio.state == PlaybackState::waitingForGate && boundary)
        songTime = *boundary + static_cast<double> (event.timestampMicros - audio.arrivalMicros) / 1.0e6 - offset;
    else
        songTime = audio.blockStartPosition + static_cast<double> (event.timestampMicros - audio.blockStartMicros) / 1.0e6 - offset;

    const auto result = session_->press (event.data1, songTime);
    lastVerdict_ = noteName (event.data1) + ": " + lesson::toString (result.verdict);
    if (result.verdict == lesson::PressVerdict::wrongPitch || result.verdict == lesson::PressVerdict::tooEarly)
        wrong_.push_back ({ event.data1, event.timestampMicros });

    log_.write (LogLevel::debug, "lesson", "Press judged",
                { { "note", noteName (event.data1) },
                  { "verdict", lesson::toString (result.verdict) },
                  { "gate", std::to_string (result.gateIndex + 1) },
                  { "offsetMs", juce::String (result.offsetMs, 1).toStdString() } });

    if (result.gatePassed)
    {
        const bool wasWaiting = audio.state == PlaybackState::waitingForGate;
        log_.write (LogLevel::info, "lesson", "Gate passed",
                    { { "gate", std::to_string (result.gateIndex + 1) },
                      { "held", wasWaiting ? "yes" : "no" },
                      { "waitMs", wasWaiting ? std::to_string ((event.timestampMicros - waitStartedMicros_) / 1000) : "0" } });
        sendGate();
    }
}

void LessonController::publish (const AudioService::Snapshot& audio)
{
    const auto now = AudioService::nowMicros();
    while (! wrong_.empty() && now - wrong_.front().second > wrongFlashMicros)
        wrong_.pop_front();

    PracticeSnapshot snap;
    snap.practising = practising_;
    snap.tolerance = lesson::toleranceOf (profile_);
    snap.mode = mode_;
    snap.waiting = audio.state == PlaybackState::waitingForGate;
    snap.lastVerdict = lastVerdict_;
    for (const auto& [pitch, when] : wrong_)
        snap.recentWrong.push_back (pitch);
    if (session_)
    {
        snap.gateCount = session_->gates().size();
        if (const auto* gate = session_->currentGate())
        {
            snap.gateNumber = gate->index + 1;
            snap.missing = session_->missingPitches();
            if (gate->index < hands_.size())
            {
                snap.current = hands_[gate->index];
                for (std::size_t k = gate->index + 1; k < hands_.size() && snap.upcoming.size() < upcomingGateCount; ++k)
                    snap.upcoming.push_back (hands_[k]);
            }
            snap.matched = session_->matchedPitches();
            snap.accompanimentOnly = audio.state == PlaybackState::playing
                                     && gate->onsetSeconds - audio.positionSeconds > accompanimentOnlyAfterSeconds;
        }
    }
    std::scoped_lock lock (snapshotMutex_);
    snapshot_ = std::move (snap);
}

void LessonController::hiResTimerCallback()
{
    std::deque<Intent> pending;
    {
        std::scoped_lock lock (intentsMutex_);
        pending.swap (intents_);
    }
    for (const auto& intent : pending)
        handle (intent);

    const auto audio = audio_.snapshot();
    if (audio.gateArrivals != seenArrivals_)
    {
        seenArrivals_ = audio.gateArrivals;
        waitStartedMicros_ = audio.arrivalMicros;
        if (session_ && session_->currentGate() != nullptr)
            log_.write (LogLevel::info, "lesson", "Waiting at gate",
                        { { "gate", std::to_string (session_->currentGate()->index + 1) },
                          { "at", seconds (session_->currentGate()->onsetSeconds) },
                          { "missing", names (session_->missingPitches()) } });
    }
    if (audio.state != lastState_)
    {
        log_.write (LogLevel::info, "transport", "Playback state changed",
                    { { "from", audio::toString (lastState_) }, { "to", audio::toString (audio.state) },
                      { "position", seconds (audio.positionSeconds) } });
        if (audio.state == PlaybackState::completed && session_)
            log_.write (LogLevel::info, "lesson", "Song finished in practice",
                        { { "gatesPassed", std::to_string (session_->passedGateCount()) },
                          { "gates", std::to_string (session_->gates().size()) } });
        lastState_ = audio.state;
    }

    while (const auto event = input_.pop())
        handleInput (*event, audio);
    if (const auto dropped = input_.takeDroppedCount(); dropped > 0)
        log_.write (LogLevel::error, "lesson", "Lesson input queue overflow", { { "dropped", std::to_string (dropped) } });

    publish (audio);
}

} // namespace pianoassist::app
