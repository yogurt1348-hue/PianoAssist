from adaptive_follow import score_follower as sf
from adaptive_follow.reference import Reference, ReferenceNote
from adaptive_follow.simulation import LiveNote, perform, synthetic_reference

BPM = 120.0


def make(ref, **cfg):
    return sf.ScoreFollower(ref, 60.0 / (BPM * ref.ppq), sf.FollowerConfig(**cfg))


def run(f, notes):
    correct = scored = 0
    results = []
    t_next = notes[0].t if notes else 0.0
    for n in notes:
        while t_next < n.t:
            f.update(t_next)
            t_next += 0.02
        r = f.on_note_on(n.pitch, n.t)
        results.append(r)
        if n.true_event >= 0:
            scored += 1
            correct += r.event_index == n.true_event
    return (correct / float(scored) if scored else 0.0), results


def melody(pitches, step=96, ppq=96):
    return Reference("m", ppq, [ReferenceNote(i * step, step - 4, p) for i, p in enumerate(pitches)], 6)


def test_single_notes_exact():
    ref = synthetic_reference(48, chord_every=0)
    f = make(ref)
    acc, _ = run(f, perform(ref, BPM))
    assert acc == 1.0
    assert f.position == ref.event_count - 1 and f.state == sf.F_TRACKING
    assert abs(f.tempo.rate - 1.0) < 0.02


def test_chords_with_spread_and_arpeggio():
    ref = synthetic_reference(48, chord_every=2)
    f = make(ref)
    acc, _ = run(f, perform(ref, BPM, chord_spread=0.08, jitter=0.01))
    assert acc >= 0.97 and f.position == ref.event_count - 1


def test_small_timing_errors_and_tempo_estimation():
    ref = synthetic_reference(64)
    for rate in (0.7, 1.25):
        f = make(ref)
        acc, _ = run(f, perform(ref, BPM, rate_at=lambda n, r=rate: r, jitter=0.03, seed=5))
        assert acc >= 0.97
        assert abs(f.tempo.rate - rate) < 0.06 * rate
        assert abs(f.estimated_bpm - BPM * rate) < 8


def test_gradual_tempo_change():
    ref = synthetic_reference(96)
    total = ref.events[-1].tick * 60.0 / (BPM * ref.ppq)
    f = make(ref)
    acc, _ = run(f, perform(ref, BPM, rate_at=lambda n: 1.0 + 0.35 * min(1.0, n / total), jitter=0.01))
    assert acc >= 0.97 and abs(f.tempo.rate - 1.35) < 0.1


def test_missing_note():
    ref = synthetic_reference(40, chord_every=0)
    notes = perform(ref, BPM)
    # a note whose neighbours have other pitches (inside a run of identical
    # pitches a missing note is genuinely ambiguous - see test_repeated_identical_notes)
    i = next(k for k in range(12, len(notes) - 1)
             if notes[k - 1].pitch != notes[k].pitch != notes[k + 1].pitch)
    del notes[i]
    f = make(ref)
    acc, _ = run(f, notes)
    assert acc >= 0.97 and f.position == ref.event_count - 1
    assert f.state != sf.F_LOST


def test_extra_and_wrong_notes():
    ref = synthetic_reference(40, chord_every=0)
    notes = perform(ref, BPM)
    notes.insert(11, LiveNote(110, notes[10].t + 0.1, -1))       # extra
    notes[20] = LiveNote(notes[20].pitch + 1, notes[20].t, -1)   # wrong
    f = make(ref)
    acc, results = run(f, notes)
    assert acc >= 0.97 and f.position == ref.event_count - 1
    assert any(r.kind == sf.K_EXTRA for r in results)
    assert all(r.state != sf.F_LOST for r in results)


def test_repeated_identical_notes():
    ref = melody([60] * 8 + [62, 64] * 4 + [60] * 8, step=48)
    f = make(ref)
    acc, _ = run(f, perform(ref, BPM, jitter=0.015))
    assert acc >= 0.95 and f.position == ref.event_count - 1
    slow = make(ref)
    acc, _ = run(slow, perform(ref, BPM, rate_at=lambda n: 0.8, jitter=0.015))
    assert acc >= 0.93


def test_pause_waiting_and_resume():
    ref = synthetic_reference(40)
    notes = perform(ref, BPM, pause_after_event=20, pause_s=5.0)
    f = make(ref)
    saw_waiting = False
    t = notes[0].t
    for n in notes:
        while t < n.t:
            f.update(t)
            saw_waiting |= f.state == sf.F_WAITING
            if f.state == sf.F_WAITING:
                assert f.position_ticks(t) <= f.hold_tick()
            t += 0.02
        f.on_note_on(n.pitch, n.t)
    assert saw_waiting
    assert f.state == sf.F_TRACKING and f.position == ref.event_count - 1
    assert abs(f.tempo.rate - 1.0) < 0.05     # the pause was not read as a slow tempo


def test_loss_and_reacquisition():
    ref = synthetic_reference(120, seed=11)
    clean = perform(ref, BPM)
    notes = [n for n in clean if n.true_event <= 30]
    t = notes[-1].t
    for k in range(10):                       # garbage, pitches not in the part
        t += 0.2
        notes.append(LiveNote(20 + k % 5, t, -1))
    shift = t + 0.5 - [n.t for n in clean if n.true_event == 80][0]
    notes += [LiveNote(n.pitch, n.t + shift, n.true_event) for n in clean if n.true_event >= 80]
    f = make(ref)
    states = []
    kinds = []
    epoch_before = f.epoch
    for n in notes:
        r = f.on_note_on(n.pitch, n.t)
        states.append(r.state)
        kinds.append(r.kind)
    assert sf.F_LOST in states
    assert sf.K_REACQUIRED in kinds and f.epoch > epoch_before
    assert f.position == ref.event_count - 1 and f.state == sf.F_TRACKING


def test_reacquisition_stays_near_the_playhead():
    """Without seeking FL cannot jump, so a lost performer is looked for only near FL's
    playhead: with a repeated passage the copy next to the playhead must win."""
    phrase = [60, 64, 67, 72, 71, 67, 64, 62, 65, 69, 72, 74]
    filler = [48 + (i * 5) % 11 for i in range(40)]
    ref = melody(phrase + filler + phrase + filler)          # the phrase at events 0.. and 52..
    second = len(phrase) + len(filler)
    spt = 60.0 / (BPM * ref.ppq)
    for centre, expect_from in ((ref.events[second + 3].tick, second), (ref.events[3].tick, 0)):
        f = make(ref, start_event=len(phrase))
        f.search_radius_ticks = 8 * ref.ppq
        f.search_center_tick = centre
        t = 0.0
        for i in range(6):                                   # track a few filler notes first
            t = i * 96 * spt
            f.on_note_on(filler[i], t)
        assert f.state == sf.F_TRACKING
        for k in range(10):                                  # lost: pitches not in the part
            t += 0.25
            f.on_note_on(20 + k % 4, t)
        assert f.state == sf.F_LOST
        for i, p in enumerate(phrase[:8]):
            r = f.on_note_on(p, t + 0.5 + i * 96 * spt)
        assert expect_from <= f.position < expect_from + len(phrase), (centre, f.position)


def test_start_anywhere():
    ref = synthetic_reference(60, seed=13)
    notes = [n for n in perform(ref, BPM) if n.true_event >= 30]
    f = make(ref, start_anywhere=True)
    run(f, notes)
    assert f.position == ref.event_count - 1


def test_ignored_input_and_empty_reference():
    ref = synthetic_reference(8)
    f = make(ref)
    assert f.on_note_on(-1, 1.0).kind == sf.K_IGNORED
    assert f.on_note_on(200, 1.0).kind == sf.K_IGNORED
    f.on_note_on(30, 1.1)                    # stray note before the start: stays idle
    assert f.state == sf.F_IDLE and not f.started


def test_snapshot_contract():
    ref = synthetic_reference(16)
    f = make(ref)
    s0 = f.snapshot()
    assert s0.state == sf.F_IDLE and s0.hold_tick == ref.events[0].tick and not s0.started
    notes = perform(ref, BPM)
    first = [n for n in notes if n.true_event == 0]
    for n in first:
        f.on_note_on(n.pitch, n.t)
    s1 = f.snapshot()
    assert s1.started and s1.event_index == 0 and s1.hold_tick == ref.events[1].tick
