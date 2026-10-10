#include "AdaptiveSongSession.h"

#include <algorithm>
#include <cmath>

namespace cadenza::adaptive
{
namespace
{
constexpr std::size_t kDiagnosticCapacity = 256;
}

AdaptiveSongSession::AdaptiveSongSession()
{
    m_diag.resize(kDiagnosticCapacity);
}

void AdaptiveSongSession::setConfig(const AdaptiveSessionConfig& config)
{
    m_config = config;
    m_stopAtEnd.store(config.stopAtEnd);
    {
        std::lock_guard<std::mutex> lk(m_followerMutex);
        m_follower.setConfig(config.follower);
        publishLocked();
    }
    std::lock_guard<std::mutex> lk(m_handoffMutex);
    m_pendingTransportConfig = config.transport;
}

void AdaptiveSongSession::load(const AdaptiveSong& song)
{
    stop();
    m_song = song;

    auto playable = std::make_shared<Playable>();
    playable->reference = song.reference;
    playable->backing = song.backing ? std::make_shared<BackingSequence>(*song.backing)
                                     : std::make_shared<BackingSequence>();
    playable->startNominal = song.startNominalSec;
    playable->endNominal = song.endNominalSec;

    {
        std::lock_guard<std::mutex> lk(m_followerMutex);
        m_follower.setReference(song.reference);
        m_diagCount = 0;
        publishLocked();
    }
    std::lock_guard<std::mutex> lk(m_handoffMutex);
    m_pending = std::move(playable);
    m_hasPending = true;
}

void AdaptiveSongSession::unload()
{
    load(AdaptiveSong {});
}

void AdaptiveSongSession::setStartGroup(int group)
{
    std::lock_guard<std::mutex> lk(m_followerMutex);
    auto cfg = m_follower.config();
    cfg.startGroup = std::max(0, group);
    m_follower.setConfig(cfg);
    m_config.follower.startGroup = cfg.startGroup;
    publishLocked();
}

void AdaptiveSongSession::play()
{
    if (!loaded()) return;
    {
        std::lock_guard<std::mutex> lk(m_followerMutex);
        m_follower.reset();   // IDLE at the start group, new epoch
        m_diagCount = 0;
        publishLocked();
    }
    {
        std::lock_guard<std::mutex> lk(m_handoffMutex);
        m_pendingTransportConfig = m_config.transport;
    }
    m_statusFinished.store(false);
    m_playGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_wantPlaying.store(true, std::memory_order_release);
}

void AdaptiveSongSession::stop()
{
    m_wantPlaying.store(false, std::memory_order_release);
}

void AdaptiveSongSession::publishLocked() noexcept
{
    m_snapshot.write(m_follower.snapshot());
}

void AdaptiveSongSession::onLiveNote(int pitch, int velocity, bool isOn, double timeSec)
{
    if (!isOn || velocity <= 0) return;           // the follower aligns onsets only
    if (!m_wantPlaying.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lk(m_followerMutex);
    const auto result = m_follower.onNoteOn(pitch, velocity, timeSec);
    publishLocked();

    if (m_diagEnabled.load(std::memory_order_relaxed)) {
        AdaptiveDiagnostic d;
        d.note = result;
        d.expectedTick = result.groupTick;
        d.followerTick = m_follower.scorePositionTicks(timeSec);
        d.playheadTick = m_statusPlayheadTick.load(std::memory_order_relaxed);
        d.phaseErrorMs = m_statusPhaseErrorSec.load(std::memory_order_relaxed) * 1000.0;
        m_diag[m_diagHead] = d;
        m_diagHead = (m_diagHead + 1) % m_diag.size();
        m_diagCount = std::min(m_diagCount + 1, m_diag.size());
    }
}

void AdaptiveSongSession::update(double nowSec)
{
    {
        std::lock_guard<std::mutex> lk(m_followerMutex);
        if (m_wantPlaying.load()) {
            const auto before = m_follower.state();
            m_follower.update(nowSec);
            if (m_follower.state() != before) publishLocked();
        }
    }
    if (m_statusFinished.load() && m_wantPlaying.load())
        m_wantPlaying.store(false);

    // Free retired song data here, never on the audio thread.
    std::shared_ptr<Playable> retired;
    {
        std::unique_lock<std::mutex> lk(m_handoffMutex, std::try_to_lock);
        if (lk.owns_lock()) retired = std::move(m_retired);
    }
}

AdaptiveSongStatus AdaptiveSongSession::status(double nowSec) const
{
    AdaptiveSongStatus s;
    s.loaded = loaded();
    s.playing = m_statusPlaying.load();
    s.finished = m_statusFinished.load();
    s.playheadTick = m_statusPlayheadTick.load();
    s.phaseErrorMs = m_statusPhaseErrorSec.load() * 1000.0;
    {
        std::lock_guard<std::mutex> lk(m_followerMutex);
        s.state = m_follower.state();
        s.followerTick = m_follower.scorePositionTicks(nowSec);
        s.estimatedBpm = m_follower.estimatedBpm();
        s.confidence = m_follower.confidence();
        s.groupIndex = m_follower.currentGroup();
        s.matchedNotes = m_follower.matchedNotes();
        s.extraNotes = m_follower.extraNotes();
        if (const auto& ref = m_follower.reference()) {
            s.groupCount = ref->groupCount();
            s.playheadBarBeat = ref->tempoMap().barBeatAt(s.playheadTick);
            s.playbackBpm = m_statusRate.load() * ref->tempoMap().bpmAt(s.playheadTick);
        }
    }
    return s;
}

std::vector<AdaptiveDiagnostic> AdaptiveSongSession::drainDiagnostics()
{
    std::lock_guard<std::mutex> lk(m_followerMutex);
    std::vector<AdaptiveDiagnostic> out;
    out.reserve(m_diagCount);
    for (std::size_t i = 0; i < m_diagCount; ++i)
        out.push_back(m_diag[(m_diagHead + m_diag.size() - m_diagCount + i) % m_diag.size()]);
    m_diagCount = 0;
    return out;
}

void AdaptiveSongSession::processBlock(double nowSec, double blockSeconds, MidiEventSink& sink) noexcept
{
    const auto generation = m_playGeneration.load(std::memory_order_acquire);
    const bool want = m_wantPlaying.load(std::memory_order_acquire);

    // Stop (or restart) requested.
    if (m_audioPlaying && (!want || generation != m_audioGeneration)) {
        if (m_active && m_active->backing) m_active->backing->releaseAll(sink);
        m_transport.stop();
        m_audioPlaying = false;
        m_statusPlaying.store(false, std::memory_order_relaxed);
        m_statusRate.store(0.0, std::memory_order_relaxed);
    }

    // Take a newly loaded song / config, only while stopped.
    if (!m_audioPlaying) {
        std::unique_lock<std::mutex> lk(m_handoffMutex, std::try_to_lock);
        if (lk.owns_lock()) {
            // Swap only once the previous retiree was freed (by update()), so the
            // audio thread never drops the last reference to song data.
            if (m_hasPending && !m_retired) {
                m_retired = std::move(m_active);
                m_active = std::move(m_pending);
                m_hasPending = false;
            }
            m_transport.setConfig(m_pendingTransportConfig);
        }
    }

    // Start.
    if (!m_audioPlaying && want && generation != m_audioGeneration) {
        m_audioGeneration = generation;
        if (m_active && m_active->reference && !m_active->reference->empty()) {
            const auto& snap = m_snapshot.read();
            const auto& map = m_active->reference->tempoMap();
            const bool freeRun = m_transport.config().startMode == AdaptiveStartMode::FreeRunIntro;
            const bool fromTop = snap.groupIndex < 0;   // start group 0
            const double startNominal = (freeRun && fromTop) ? std::min(0.0, snap.anchorNominalSec) : snap.anchorNominalSec;
            m_transport.setTempoMap(&map);
            m_transport.reset(startNominal);
            m_transport.start();
            m_active->backing->seek(map.secondsToTicks(startNominal), sink);   // chase programs / controllers
            m_audioPlaying = true;
            m_statusPlaying.store(true, std::memory_order_relaxed);
            m_statusFinished.store(false, std::memory_order_relaxed);
        }
    }

    if (!m_audioPlaying || !m_active) return;

    const auto& snapshot = m_snapshot.read();
    const auto step = m_transport.process(snapshot, nowSec, blockSeconds);
    auto& backing = *m_active->backing;
    if (step.seeked) backing.seek(step.fromTick, sink);   // releases + chases
    else if (step.releaseNotes) backing.releaseAll(sink);
    if (step.active) backing.render(step.fromTick, step.toTick, step.fromInclusive, sink);

    m_statusPlayheadTick.store(m_transport.positionTicks(), std::memory_order_relaxed);
    m_statusRate.store(step.playbackRate, std::memory_order_relaxed);
    m_statusPhaseErrorSec.store(step.phaseErrorSec, std::memory_order_relaxed);

    if (m_stopAtEnd.load(std::memory_order_relaxed) && m_transport.positionNominal() >= m_active->endNominal
        && snapshot.state != TrackingState::Idle) {
        backing.releaseAll(sink);
        m_transport.stop();
        m_audioPlaying = false;
        m_statusPlaying.store(false, std::memory_order_relaxed);
        m_statusRate.store(0.0, std::memory_order_relaxed);
        m_statusFinished.store(true, std::memory_order_relaxed);
    }
}
}
