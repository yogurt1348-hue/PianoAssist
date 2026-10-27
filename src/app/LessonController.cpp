#include "app/LessonController.h"

#include "core/NoteName.h"
#include "lesson_engine/Gate.h"
#include "lesson_engine/Latency.h"
#include "lesson_engine/PracticeClock.h"

#include <algorithm>
#include <cmath>
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
constexpr std::int64_t healthEveryMicros = 10'000'000;
constexpr std::size_t recentOffsetCount = 24;
constexpr int latencyClicks = 14;
constexpr std::size_t latencyWarmUpClicks = 2;
constexpr std::int64_t latencyIntervalMicros = 1'000'000;
constexpr std::int64_t latencyClickLengthMicros = 80'000;
constexpr std::uint8_t latencyClickPitch = 84; // C6: short and clear on any piano sound

std::string ms (double value, int decimals = 1)
{
    return juce::String (value, decimals).toStdString();
}

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

void LessonController::loadSong (std::shared_ptr<const timeline::Song> song, bool keepPosition)
{
    post (LoadSong { std::move (song), keepPosition });
}
void LessonController::setAccompaniment (lesson::Accompaniment a) { post (SetAccompaniment { a }); }
void LessonController::setMutedParts (std::vector<bool> muted) { post (SetMutedParts { std::move (muted) }); }
void LessonController::setTempo (double rate) { post (SetTempo { rate }); }
void LessonController::setLoop (bool enabled, double start, double end) { post (SetLoop { enabled, start, end }); }
void LessonController::setPartGains (std::vector<double> gains) { post (SetPartGains { std::move (gains) }); }
void LessonController::playPause() { post (PlayPause {}); }
void LessonController::stop() { post (Stop {}); }
void LessonController::seek (double s) { post (Seek { s }); }
void LessonController::silence() { post (Silence {}); }
void LessonController::setPractising (bool on) { post (SetPractising { on }); }
void LessonController::setMode (lesson::PracticeMode mode) { post (SetMode { mode }); }
void LessonController::setProfile (lesson::PracticeProfile profile) { post (SetProfile { std::move (profile) }); }
void LessonController::previewKey (std::uint8_t pitch, bool down) { post (PreviewKey { pitch, down }); }
void LessonController::setCountIn (bool on) { post (SetCountIn { on }); }
void LessonController::setInputOffset (double ms) { post (SetInputOffset { ms }); }
void LessonController::startLatencyTest() { post (StartLatencyTest {}); }
void LessonController::stopLatencyTest() { post (StopLatencyTest {}); }

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

void LessonController::rebuild (double position, const char* why, bool live)
{
    if (! song_)
    {
        // The song was closed: nothing may keep sounding or waiting.
        audio_.stop();
        audio_.setGate (-1.0);
        session_.reset();
        return;
    }
    const auto mode = mode_;
    const bool practising = practising_;
    const auto accompaniment = accompaniment_;
    // The learner plays the practised hands on the piano; the app plays the
    // rest, as far as the accompaniment choices allow.
    const auto song = song_;
    const auto muted = mutedParts_;
    const auto gains = partGains_;
    audio_.loadSong (
        *song_,
        [song, mode, practising, accompaniment, muted] (const timeline::Note& note) {
            const auto part = song->partIndexOf (note);
            if (part >= 0 && static_cast<std::size_t> (part) < muted.size() && muted[static_cast<std::size_t> (part)])
                return false;
            if (part >= 0 && song->parts[static_cast<std::size_t> (part)].assignment == timeline::Part::Assignment::excluded)
                return false; // excluded: neither checked nor played
            return lesson::isPlayedByApp (note.role, practising, mode, accompaniment);
        },
        [song, gains] (const timeline::Note& note) {
            const auto part = song->partIndexOf (note);
            return part >= 0 && static_cast<std::size_t> (part) < gains.size() ? gains[static_cast<std::size_t> (part)] : 1.0;
        },
        live);
    if (position > 0.0 && ! live)
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
                if (! i.keepPosition || ! song_ || ! i.song || song_->parts.size() != i.song->parts.size())
                {
                    mutedParts_.clear();
                    partGains_.clear();
                    loop_ = { false, 0.0, 0.0 };
                }
                cancelCountIn();
                song_ = i.song;
                if (i.keepPosition)
                    audio_.stop();
                rebuild (i.keepPosition ? audio.positionSeconds : 0.0, i.keepPosition ? "parts changed" : "song loaded");
            }
            else if constexpr (std::is_same_v<T, PlayPause>)
            {
                if (! song_)
                    return;
                if (countIn_.active)
                {
                    logTransport ("User cancelled the count-in", audio);
                    cancelCountIn();
                }
                else if (audio.state == PlaybackState::playing || audio.state == PlaybackState::waitingForGate)
                {
                    logTransport ("User pressed Pause", audio);
                    audio_.pause();
                }
                else
                {
                    logTransport ("User pressed Play", audio);
                    double from = audio.positionSeconds;
                    if (audio.state == PlaybackState::completed)
                    {
                        // Start again: from the loop start when a loop is set.
                        from = loop_.enabled && loop_.end > loop_.start ? loop_.start : 0.0;
                        audio_.seek (from);
                        resetGates (from);
                    }
                    startPlaying (from);
                }
            }
            else if constexpr (std::is_same_v<T, SetCountIn>)
            {
                countInEnabled_ = i.on;
                if (! i.on)
                    cancelCountIn();
                log_.write (LogLevel::info, "lesson", i.on ? "Count-in on" : "Count-in off");
            }
            else if constexpr (std::is_same_v<T, Stop>)
            {
                cancelCountIn();
                logTransport ("User pressed Stop", audio);
                audio_.stop();
                resetGates (0.0);
            }
            else if constexpr (std::is_same_v<T, Seek>)
            {
                cancelCountIn();
                log_.write (LogLevel::info, "transport", "User seeked", { { "to", seconds (i.seconds) } });
                audio_.seek (i.seconds);
                resetGates (i.seconds);
            }
            else if constexpr (std::is_same_v<T, Silence>)
            {
                cancelCountIn();
                logTransport ("User pressed Silence", audio);
                audio_.silence();
            }
            else if constexpr (std::is_same_v<T, SetPractising>)
            {
                if (practising_ == i.on)
                    return;
                practising_ = i.on;
                log_.write (LogLevel::info, "lesson", i.on ? "Practice turned on" : "Practice turned off");
                // Goes on from where it is: no jump, nothing replayed.
                rebuild (audio.positionSeconds, i.on ? "practice on" : "practice off", true);
            }
            else if constexpr (std::is_same_v<T, SetMode>)
            {
                if (mode_ == i.mode)
                    return;
                mode_ = i.mode;
                log_.write (LogLevel::info, "lesson", "Practice mode changed", { { "mode", std::string (lesson::toString (mode_)) } });
                rebuild (audio.positionSeconds, "mode changed", true);
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
            else if constexpr (std::is_same_v<T, SetAccompaniment>)
            {
                if (accompaniment_ == i.accompaniment)
                    return;
                accompaniment_ = i.accompaniment;
                log_.write (LogLevel::info, "lesson", "Accompaniment changed",
                            { { "playOtherPianoPart", accompaniment_.playOtherPianoPart ? "yes" : "no" },
                              { "playBacking", accompaniment_.playBacking ? "yes" : "no" } });
                rebuild (audio.positionSeconds, "accompaniment changed", true);
            }
            else if constexpr (std::is_same_v<T, SetMutedParts>)
            {
                if (mutedParts_ == i.muted)
                    return;
                mutedParts_ = i.muted;
                log_.write (LogLevel::info, "lesson", "Track mutes changed",
                            { { "muted", std::to_string (std::count (mutedParts_.begin(), mutedParts_.end(), true)) } });
                // Keeps playing from where it was; only the event list changes.
                rebuild (audio.positionSeconds, "track mutes changed", true);
            }
            else if constexpr (std::is_same_v<T, SetTempo>)
            {
                log_.write (LogLevel::info, "lesson", "Tempo changed",
                            { { "percent", std::to_string (static_cast<int> (std::lround (i.rate * 100.0))) } });
                audio_.setRate (i.rate);
            }
            else if constexpr (std::is_same_v<T, SetLoop>)
            {
                loop_ = i;
                loopJumping_ = false;
                log_.write (LogLevel::info, "lesson", i.enabled ? "Loop set" : "Loop off",
                            { { "from", seconds (i.start) }, { "to", seconds (i.end) } });
            }
            else if constexpr (std::is_same_v<T, SetPartGains>)
            {
                if (partGains_ == i.gains)
                    return;
                partGains_ = i.gains;
                rebuild (audio.positionSeconds, "track volumes changed", true);
            }
            else if constexpr (std::is_same_v<T, SetInputOffset>)
            {
                inputOffsetMs_ = i.ms;
                recentOffsets_.clear(); // old presses were judged with the old delay
                hintGiven_ = false;
                log_.write (LogLevel::info, "lesson", "Input delay changed", { { "ms", ms (i.ms) } });
            }
            else if constexpr (std::is_same_v<T, StartLatencyTest>)
            {
                cancelCountIn();
                if (audio.state == PlaybackState::playing || audio.state == PlaybackState::waitingForGate)
                {
                    logTransport ("Paused for the latency test", audio);
                    audio_.pause();
                }
                latency_ = {};
                latency_.active = true;
                latency_.startMicros = AudioService::nowMicros() + 500'000;
                latency_.lastSeenPreview = audio_.lastPreviewMicros();
                log_.write (LogLevel::info, "latency", "Latency test started",
                            { { "clicks", std::to_string (latencyClicks) },
                              { "intervalMs", std::to_string (latencyIntervalMicros / 1000) },
                              { "reportedOutputMs", ms (audio_.reportedOutputLatencyMs()) },
                              { "device", audio_.deviceDescription() } });
            }
            else if constexpr (std::is_same_v<T, StopLatencyTest>)
            {
                if (latency_.active)
                    finishLatencyTest ("stopped by the user");
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
    if (event.type == midi_io::InputEventType::noteOn && latency_.active)
    {
        latency_.taps.push_back (event.timestampMicros);
        return; // during the test presses only measure the delay
    }
    if (event.type != midi_io::InputEventType::noteOn || ! session_)
        return;
    if (audio.state != PlaybackState::playing && audio.state != PlaybackState::waitingForGate)
        return; // gates only judge presses while the song runs or waits

    // Map the press to song time. While held, song time is frozen where the
    // transport stopped (the gate boundary, or later if the boundary was
    // already behind it); the wall time since arrival measures lateness.
    // Wall time becomes song time at the current tempo.
    lesson::TransportClock clock;
    clock.waiting = audio.state == PlaybackState::waitingForGate;
    clock.positionSeconds = audio.positionSeconds;
    clock.arrivalMicros = audio.arrivalMicros;
    clock.blockStartPosition = audio.blockStartPosition;
    clock.blockStartMicros = audio.blockStartMicros;
    clock.rate = audio.rate;
    const double songTime = lesson::songTimeOfPress (clock, event.timestampMicros, inputOffsetMs_);

    const auto result = session_->press (event.data1, songTime);
    lastVerdict_ = noteName (event.data1) + ": " + lesson::toString (result.verdict);
    if (result.verdict == lesson::PressVerdict::wrongPitch || result.verdict == lesson::PressVerdict::tooEarly)
        wrong_.push_back ({ event.data1, event.timestampMicros });

    // How long the press took from the MIDI driver to this verdict.
    const double procMs = static_cast<double> (AudioService::nowMicros() - event.timestampMicros) / 1000.0;
    windowProcMs_.push_back (procMs);
    log_.write (LogLevel::debug, "lesson", "Press judged",
                { { "note", noteName (event.data1) },
                  { "verdict", lesson::toString (result.verdict) },
                  { "gate", std::to_string (result.gateIndex + 1) },
                  { "offsetMs", ms (result.offsetMs) },
                  { "procMs", ms (procMs, 2) },
                  { "velocity", std::to_string (event.data2) },
                  { "position", seconds (audio.positionSeconds) },
                  { "state", audio::toString (audio.state) } });

    using V = lesson::PressVerdict;
    if (result.verdict == V::wrongPitch || result.verdict == V::tooEarly)
        ++windowWrong_;
    const bool counted = result.verdict == V::acceptedOnTime || result.verdict == V::acceptedEarly
                         || result.verdict == V::acceptedLate;
    // Presses after a long wait say nothing about timing; leave them out.
    if (counted && std::abs (result.offsetMs) < 1000.0)
    {
        windowOffsets_.push_back (result.offsetMs);
        recentOffsets_.push_back (result.offsetMs);
        while (recentOffsets_.size() > recentOffsetCount)
            recentOffsets_.pop_front();
        if (! hintGiven_)
            if (const auto late = lesson::steadyLateness ({ recentOffsets_.begin(), recentOffsets_.end() }))
            {
                hintGiven_ = true;
                steadyLateMs_ = *late;
                ++steadyLateSeq_;
                log_.write (LogLevel::warning, "latency", "Steadily late: the sound probably reaches the player late",
                            { { "medianMs", ms (*late, 0) },
                              { "presses", std::to_string (recentOffsets_.size()) },
                              { "inputOffsetMs", ms (inputOffsetMs_) },
                              { "reportedOutputMs", ms (audio_.reportedOutputLatencyMs()) },
                              { "device", audio_.deviceDescription() } });
            }
    }

    if (result.gatePassed)
    {
        const bool wasWaiting = audio.state == PlaybackState::waitingForGate;
        const auto waitedMs = wasWaiting ? static_cast<double> (event.timestampMicros - waitStartedMicros_) / 1000.0 : 0.0;
        if (wasWaiting)
        {
            ++windowStalls_;
            windowStallMs_ += std::max (0.0, waitedMs);
        }
        log_.write (LogLevel::info, "lesson", "Gate passed",
                    { { "gate", std::to_string (result.gateIndex + 1) },
                      { "held", wasWaiting ? "yes" : "no" },
                      { "waitMs", wasWaiting ? std::to_string (static_cast<std::int64_t> (waitedMs)) : "0" },
                      { "offsetMs", ms (result.offsetMs) } });
        sendGate();
    }
}

std::string LessonController::channelNames (int channel) const
{
    std::string text;
    if (song_)
        for (std::size_t p = 0; p < song_->parts.size(); ++p)
        {
            const auto& part = song_->parts[p];
            if (part.channel != channel)
                continue;
            const bool muted = p < mutedParts_.size() && mutedParts_[p];
            text += (text.empty() ? "" : "+") + (part.trackName.empty() ? "track " + std::to_string (part.track + 1) : part.trackName)
                    + (muted ? " (muted)" : "");
        }
    return text.empty() ? (channel == audio::PlaybackCommand::previewChannel ? "preview/clicks" : "?") : text;
}

void LessonController::logHealth (const char* why)
{
    lastHealthMicros_ = AudioService::nowMicros();
    const auto h = audio_.takeHealth();
    log_.write (h.overBudget > 0 || h.maxGapMs > 3.0 * h.budgetMs ? LogLevel::warning : LogLevel::info, "audio", "Audio health",
                { { "why", why },
                  { "callbacks", std::to_string (h.callbacks) },
                  { "avgMs", ms (h.avgCallbackMs, 2) },
                  { "maxMs", ms (h.maxCallbackMs, 2) },
                  { "budgetMs", ms (h.budgetMs, 2) },
                  { "overBudget", std::to_string (h.overBudget) },
                  { "maxGapMs", ms (h.maxGapMs, 1) },
                  { "xruns", std::to_string (h.xruns) },
                  { "reportedOutputMs", ms (audio_.reportedOutputLatencyMs()) } });

    std::string activity;
    for (int ch = 0; ch < 16; ++ch)
        if (h.channelNotes[static_cast<std::size_t> (ch)] > 0)
            activity += (activity.empty() ? "" : "; ") + std::string ("ch") + std::to_string (ch + 1) + " " + channelNames (ch)
                        + ": " + std::to_string (h.channelNotes[static_cast<std::size_t> (ch)]);
    if (! activity.empty())
        log_.write (LogLevel::info, "audio", "Track activity", { { "notesStarted", activity } });

    if (! windowOffsets_.empty() || windowWrong_ > 0 || windowStalls_ > 0)
    {
        const auto t = lesson::timingStats (windowOffsets_);
        const auto proc = lesson::timingStats (windowProcMs_);
        log_.write (LogLevel::info, "lesson", "Practice timing",
                    { { "why", why },
                      { "presses", std::to_string (t.count) },
                      { "medianMs", ms (t.median, 0) },
                      { "p10Ms", ms (t.p10, 0) },
                      { "p90Ms", ms (t.p90, 0) },
                      { "spreadMs", ms (t.spread(), 0) },
                      { "wrong", std::to_string (windowWrong_) },
                      { "stalls", std::to_string (windowStalls_) },
                      { "stallMs", ms (windowStallMs_, 0) },
                      { "inputMedianMs", ms (proc.median, 2) },
                      { "inputMaxMs", ms (proc.count > 0 ? *std::max_element (windowProcMs_.begin(), windowProcMs_.end()) : 0.0, 2) },
                      { "inputOffsetMs", ms (inputOffsetMs_) },
                      { "lateWindowMs", ms (profile_.onsetLateMs, 0) } });
    }
    windowOffsets_.clear();
    windowProcMs_.clear();
    windowWrong_ = 0;
    windowStalls_ = 0;
    windowStallMs_ = 0.0;
}

void LessonController::tickLatencyTest()
{
    if (! latency_.active)
        return;
    const auto now = AudioService::nowMicros();
    if (latency_.offDueMicros >= 0 && now >= latency_.offDueMicros)
    {
        audio_.previewKey (latencyClickPitch, false);
        latency_.offDueMicros = -1;
    }
    // The audio thread notes when the click's block started; that is the
    // same reference the song notes use when presses are judged.
    if (const auto seen = audio_.lastPreviewMicros(); seen != latency_.lastSeenPreview)
    {
        latency_.lastSeenPreview = seen;
        latency_.clicks.push_back (seen);
    }
    if (latency_.sent < latencyClicks && now >= latency_.startMicros + latency_.sent * latencyIntervalMicros)
    {
        audio_.previewKey (latencyClickPitch, true);
        latency_.offDueMicros = now + latencyClickLengthMicros;
        ++latency_.sent;
    }
    if (latency_.sent >= latencyClicks && now >= latency_.startMicros + latencyClicks * latencyIntervalMicros)
        finishLatencyTest ("done");
}

void LessonController::finishLatencyTest (const char* why)
{
    if (latency_.offDueMicros >= 0)
        audio_.previewKey (latencyClickPitch, false);
    latency_.offDueMicros = -1;
    latency_.active = false;
    const auto diffs = lesson::matchTapsToClicks (latency_.clicks, latency_.taps, latencyWarmUpClicks, latencyIntervalMicros);
    const auto s = lesson::timingStats (diffs);
    latency_.matched = static_cast<int> (s.count);
    latency_.resultMs = s.median;
    latency_.spreadMs = s.spread();
    const bool enough = s.count >= 6 && std::string (why) == "done";
    latency_.done = enough;
    latency_.failed = ! enough;
    std::string all;
    for (const auto d : diffs)
        all += (all.empty() ? "" : " ") + ms (d, 0);
    log_.write (LogLevel::info, "latency", enough ? "Latency test result" : "Latency test without a result",
                { { "why", why },
                  { "clicks", std::to_string (latency_.clicks.size()) },
                  { "presses", std::to_string (latency_.taps.size()) },
                  { "matched", std::to_string (s.count) },
                  { "medianMs", ms (s.median, 0) },
                  { "spreadMs", ms (s.spread(), 0) },
                  { "diffsMs", all },
                  { "reportedOutputMs", ms (audio_.reportedOutputLatencyMs()) },
                  { "device", audio_.deviceDescription() } });
}

void LessonController::publish (const AudioService::Snapshot& audio)
{
    const auto now = AudioService::nowMicros();
    while (! wrong_.empty() && now - wrong_.front().second > wrongFlashMicros)
        wrong_.pop_front();

    PracticeSnapshot snap;
    snap.practising = practising_;
    snap.tolerance = lesson::toleranceOf (profile_);
    snap.accompaniment = accompaniment_;
    snap.mode = mode_;
    snap.waiting = audio.state == PlaybackState::waitingForGate;
    snap.lastVerdict = lastVerdict_;
    snap.latencyTest.active = latency_.active;
    snap.latencyTest.done = latency_.done;
    snap.latencyTest.failed = latency_.failed;
    snap.latencyTest.clicks = latency_.sent;
    snap.latencyTest.totalClicks = latencyClicks;
    snap.latencyTest.matched = latency_.matched;
    snap.latencyTest.resultMs = latency_.resultMs;
    snap.latencyTest.spreadMs = latency_.spreadMs;
    snap.steadyLateMs = steadyLateMs_;
    snap.steadyLateSeq = steadyLateSeq_;
    snap.inputOffsetMs = inputOffsetMs_;
    if (countIn_.active)
    {
        snap.countInBeat = std::max (1, countIn_.clicked);
        snap.countInBeats = countIn_.plan.beats;
    }
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

void LessonController::checkLoop (const AudioService::Snapshot& audio)
{
    if (! song_ || countIn_.active)
        return;
    const lesson::LoopRange range { loop_.enabled, loop_.start, loop_.end };
    const auto now = AudioService::nowMicros();
    if (loopJumping_)
    {
        // The seek reaches the audio thread a block later; wait for it.
        if (audio.positionSeconds < loop_.end - 0.01 || now - loopJumpMicros_ > 250'000)
            loopJumping_ = false;
        return;
    }
    const bool running = audio.state == PlaybackState::playing || audio.state == PlaybackState::waitingForGate;
    const bool finished = audio.state == PlaybackState::completed && lastState_ != PlaybackState::completed;
    std::optional<double> pending;
    if (session_)
        if (const auto* gate = session_->currentGate())
            pending = gate->onsetSeconds;
    if (! lesson::loopShouldJump (range, running, finished, audio.positionSeconds, song_->durationSeconds, pending))
        return;
    log_.write (LogLevel::info, "lesson", "Loop: back to the start", { { "to", seconds (loop_.start) } });
    // Stop releases every note the song started and the pedal; the rebuild
    // resets the matcher and the scheduled events to the start of the range.
    audio_.stop();
    rebuild (loop_.start, "loop");
    startPlaying (loop_.start);
    loopJumping_ = true;
    loopJumpMicros_ = now;
}

void LessonController::startPlaying (double position)
{
    if (! countInEnabled_ || ! song_)
    {
        audio_.play();
        return;
    }
    const auto tick = song_->tempoMap.secondsToTick (position);
    int numerator = 4, denominator = 4;
    for (const auto& ts : song_->timeSignatures)
        if (ts.seconds <= position + 1.0e-6)
        {
            numerator = ts.numerator;
            denominator = ts.denominator;
        }
    countIn_.plan = lesson::makeCountIn (song_->tempoMap.tempoBpmAt (tick), numerator, denominator, audio_.snapshot().rate);
    countIn_.active = true;
    countIn_.startMicros = AudioService::nowMicros();
    countIn_.clicked = 0;
    countIn_.offDueMicros = -1;
    log_.write (LogLevel::info, "lesson", "Count-in",
                { { "beats", std::to_string (countIn_.plan.beats) }, { "beatMs", std::to_string (static_cast<int> (countIn_.plan.beatSeconds * 1000.0)) } });
}

void LessonController::cancelCountIn()
{
    if (! countIn_.active)
        return;
    countIn_.active = false;
    if (countIn_.offDueMicros >= 0)
        audio_.previewKey (countIn_.lastPitch, false);
    countIn_.offDueMicros = -1;
}

void LessonController::tickCountIn()
{
    if (! countIn_.active)
        return;
    const auto now = AudioService::nowMicros();
    if (countIn_.offDueMicros >= 0 && now >= countIn_.offDueMicros)
    {
        audio_.previewKey (countIn_.lastPitch, false);
        countIn_.offDueMicros = -1;
    }
    const auto beatMicros = static_cast<std::int64_t> (countIn_.plan.beatSeconds * 1.0e6);
    if (countIn_.clicked < countIn_.plan.beats && now >= countIn_.startMicros + countIn_.clicked * beatMicros)
    {
        // Clicks on the preview channel: a high note, the first one higher.
        countIn_.lastPitch = static_cast<std::uint8_t> (countIn_.clicked == 0 ? 96 : 91);
        audio_.previewKey (countIn_.lastPitch, true);
        countIn_.offDueMicros = now + 60'000;
        ++countIn_.clicked;
    }
    else if (countIn_.clicked >= countIn_.plan.beats && now >= countIn_.startMicros + countIn_.plan.beats * beatMicros)
    {
        if (countIn_.offDueMicros >= 0)
            audio_.previewKey (countIn_.lastPitch, false);
        countIn_.active = false;
        countIn_.offDueMicros = -1;
        audio_.play();
    }
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
    tickCountIn();
    tickLatencyTest();
    checkLoop (audio);
    const bool running = audio.state == PlaybackState::playing || audio.state == PlaybackState::waitingForGate;
    const bool wasRunning = lastState_ == PlaybackState::playing || lastState_ == PlaybackState::waitingForGate;
    if (running && AudioService::nowMicros() - lastHealthMicros_ >= healthEveryMicros)
        logHealth ("playing");
    else if (wasRunning && ! running)
        logHealth ("stopped");
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
