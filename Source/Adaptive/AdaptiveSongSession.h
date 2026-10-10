// AdaptiveSongSession — runs Adaptive Song Mode across Cadenza's threads.
//
//   message thread : load(), play(), stop(), update(), status(), diagnostics
//   MIDI thread    : onLiveNote()  (follower DP; short mutex, never the audio thread)
//   audio thread   : processBlock() (wait-free snapshot read, transport, backing render)
//
// The follower lives behind a mutex shared by the MIDI and message threads;
// its state reaches the audio thread through a TripleBuffer. A newly loaded
// song is handed to the audio thread with a try_lock handoff (like
// StyleEngine's style queue) and only while playback is stopped; the old one
// is retired back to the message thread so nothing is freed on the audio thread.
//
// Pure C++ (no JUCE) so the whole threading contract is unit-testable.

#pragma once

#include "AdaptiveSong.h"
#include "AdaptiveTransport.h"
#include "BackingSequence.h"
#include "ScoreFollower.h"
#include "TripleBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace cadenza::adaptive
{
struct AdaptiveSongStatus
{
    bool loaded = false;
    bool playing = false;              // audio side running
    bool finished = false;             // reached the end of the song (auto-stopped)
    TrackingState state = TrackingState::Idle;
    double followerTick = 0.0;         // estimated performer position (reference ticks)
    double playheadTick = 0.0;         // backing playhead (reference ticks)
    BarBeat playheadBarBeat;
    double estimatedBpm = 0.0;         // performer tempo (written BPM x rate)
    double playbackBpm = 0.0;          // backing tempo this block
    double confidence = 0.0;
    double phaseErrorMs = 0.0;         // backing behind (+) / ahead (-) of the performer, nominal ms
    int groupIndex = -1;
    int groupCount = 0;
    int matchedNotes = 0;
    int extraNotes = 0;
};

// One line of development diagnostics per live note.
struct AdaptiveDiagnostic
{
    FollowerNoteResult note;
    double expectedTick = 0.0;     // reference tick of the matched group
    double followerTick = 0.0;     // follower position estimate at the note time
    double playheadTick = 0.0;     // backing playhead when the note arrived
    double phaseErrorMs = 0.0;
};

struct AdaptiveSessionConfig
{
    ScoreFollowerConfig follower;
    AdaptiveTransportConfig transport;
    bool stopAtEnd = true;
};

class AdaptiveSongSession
{
public:
    AdaptiveSongSession();

    // ---- message thread ----
    void setConfig(const AdaptiveSessionConfig& config);   // applies on the next play()
    const AdaptiveSessionConfig& config() const noexcept { return m_config; }
    void load(const AdaptiveSong& song);                    // stops playback first
    void unload();
    bool loaded() const noexcept { return m_song.reference != nullptr; }
    const AdaptiveSong& song() const noexcept { return m_song; }
    void play();                                            // (re)start: wait for the performer at the start
    void stop();
    bool playRequested() const noexcept { return m_wantPlaying.load(); }
    void setStartGroup(int group);                          // practice from a later point (applies on play)
    void update(double nowSec);                             // WAITING detection, retire old data
    AdaptiveSongStatus status(double nowSec) const;
    void setDiagnosticsEnabled(bool on) noexcept { m_diagEnabled.store(on); }
    std::vector<AdaptiveDiagnostic> drainDiagnostics();

    // ---- MIDI thread ----
    void onLiveNote(int pitch, int velocity, bool isOn, double timeSec);

    // ---- audio thread ----
    void processBlock(double nowSec, double blockSeconds, MidiEventSink& sink) noexcept;

private:
    struct Playable
    {
        std::shared_ptr<const ReferencePerformance> reference;
        std::shared_ptr<BackingSequence> backing;   // mutable playback cursor: audio thread only
        double startNominal = 0.0;
        double endNominal = 0.0;
    };

    void publishLocked() noexcept;   // follower mutex held

    AdaptiveSessionConfig m_config;
    AdaptiveSong m_song;                    // message thread copy (UI info)

    // follower (MIDI + message threads)
    mutable std::mutex m_followerMutex;
    ScoreFollower m_follower;
    TripleBuffer<FollowerSnapshot> m_snapshot;
    std::vector<AdaptiveDiagnostic> m_diag;   // ring under m_followerMutex
    std::size_t m_diagHead = 0;
    std::size_t m_diagCount = 0;
    std::atomic<bool> m_diagEnabled { false };

    // handoff message -> audio
    std::mutex m_handoffMutex;
    std::shared_ptr<Playable> m_pending;    // guarded by m_handoffMutex
    bool m_hasPending = false;              // guarded by m_handoffMutex
    std::shared_ptr<Playable> m_retired;    // guarded by m_handoffMutex (freed on the message thread)
    std::atomic<std::uint32_t> m_playGeneration { 0 };   // bumped by play()
    std::atomic<bool> m_wantPlaying { false };
    AdaptiveTransportConfig m_pendingTransportConfig;    // guarded by m_handoffMutex

    // audio thread state
    std::shared_ptr<Playable> m_active;
    AdaptiveTransport m_transport;
    std::uint32_t m_audioGeneration = 0;
    bool m_audioPlaying = false;

    // audio -> message status
    std::atomic<bool> m_stopAtEnd { true };
    std::atomic<bool> m_statusPlaying { false };
    std::atomic<bool> m_statusFinished { false };
    std::atomic<double> m_statusPlayheadTick { 0.0 };
    std::atomic<double> m_statusRate { 0.0 };
    std::atomic<double> m_statusPhaseErrorSec { 0.0 };
};
}
