from adaptive_follow import adaptive_transport as at
from adaptive_follow import state as st
from adaptive_follow.model import Config
from adaptive_follow.score_follower import FollowerSnapshot

PPQ = 96
BPM = 120.0
SPT = 60.0 / (BPM * PPQ)
INF = float("inf")


def snap(anchor_tick=0, anchor_live=0.0, rate=1.0, hold=INF, conf=0.9, epoch=1, started=True):
    return FollowerSnapshot(state="TRACKING", epoch=epoch, event_index=0, anchor_tick=anchor_tick,
                            anchor_live=anchor_live, hold_tick=hold, rate=rate, confidence=conf,
                            last_note_live=anchor_live, started=started)


class Plant(object):
    """Minimal FL transport model for closed-loop controller tests."""

    def __init__(self, pos=0.0, playing=True, tempo=BPM):
        self.pos = pos
        self.playing = playing
        self.speed = 1.0
        self.tempo = tempo

    def apply(self, d):
        if d.speed is not None:
            self.speed = min(4.0, max(0.25, d.speed))
        if d.tempo_bpm is not None:
            self.tempo = d.tempo_bpm
        if d.action == at.ACTION_PAUSE:
            self.playing = False
        if d.action in (at.ACTION_RESUME, at.ACTION_START):
            self.playing = True
        if d.action == at.ACTION_SEEK:
            self.pos = d.seek_tick

    def advance(self, dt):
        if self.playing:
            self.pos += dt * self.tempo / 60.0 * PPQ * self.speed


def loop(ctrl, plant, s, seconds, state=st.TRACKING, t0=0.0, dt=0.02, record=None):
    t = t0
    while t < t0 + seconds:
        d = ctrl.step(t, s, plant.pos, plant.playing, state)
        plant.apply(d)
        ctrl.note_applied(d, t)
        if record is not None:
            record.append((t, d, plant.pos, plant.speed, plant.tempo))
        plant.advance(dt)
        t += dt
    return t


def test_phase_correction_backing_ahead_slows_down_then_locks():
    cfg = Config.from_dict({})
    ctrl = at.TransportController(cfg, PPQ, BPM)
    plant = Plant(pos=0.5 / SPT)          # FL is 0.5 s (half a bar at 120) ahead
    rec = []
    loop(ctrl, plant, snap(), 10.0, record=rec)
    speeds = [r[3] for r in rec[5:]]
    assert min(speeds) >= 1.0 - 0.15 - 1e-6 and max(speeds) <= 1.0 + 1e-6
    final_err = (10.0 / SPT) - plant.pos
    assert abs(final_err * SPT) < 0.03


def test_phase_correction_backing_behind_speeds_up():
    ctrl = at.TransportController(Config.from_dict({}), PPQ, BPM)
    plant = Plant(pos=0.0)
    s = snap(anchor_tick=int(0.2 / SPT))
    rec = []
    loop(ctrl, plant, s, 8.0, record=rec)
    assert max(r[3] for r in rec) <= 1.15 + 1e-6            # small error: gentle correction
    assert abs((0.2 + 8.0) / SPT - plant.pos) * SPT < 0.03


def test_far_behind_catches_up_faster_but_bounded():
    cfg = Config.from_dict({})
    ctrl = at.TransportController(cfg, PPQ, BPM)
    plant = Plant(pos=0.0)
    rec = []
    loop(ctrl, plant, snap(anchor_tick=int(0.6 / SPT)), 6.0, record=rec)
    peak = max(r[3] for r in rec)
    assert 1.15 < peak <= 1.0 + cfg.catchup_max_correction + 1e-6
    assert abs((0.6 + 6.0) / SPT - plant.pos) * SPT < 0.03
    gentle = at.TransportController(Config.from_dict({"catchup_max_correction": 0.0}), PPQ, BPM)
    p2 = Plant(pos=0.0)
    rec2 = []
    loop(gentle, p2, snap(anchor_tick=int(0.6 / SPT)), 2.0, record=rec2)
    assert max(r[3] for r in rec2) <= 1.15 + 1e-6


def test_speed_limits_and_fl_range():
    cfg = Config.from_dict({"speed_min": 0.8, "speed_max": 1.2})
    ctrl = at.TransportController(cfg, PPQ, BPM)
    plant = Plant()
    rec = []
    loop(ctrl, plant, snap(rate=2.5), 5.0, record=rec)
    assert max(r[3] for r in rec) <= 1.2 + 1e-9
    ctrl2 = at.TransportController(Config.from_dict({"speed_min": 0.8, "speed_max": 1.2}), PPQ, BPM)
    plant2 = Plant()
    rec2 = []
    loop(ctrl2, plant2, snap(rate=0.3), 5.0, record=rec2)
    assert min(r[3] for r in rec2) >= 0.8 - 1e-9


def test_slew_and_update_rate_limited():
    ctrl = at.TransportController(Config.from_dict({"speed_slew_per_s": 0.5}), PPQ, BPM)
    plant = Plant()
    rec = []
    loop(ctrl, plant, snap(rate=1.4), 3.0, record=rec)
    sent = [(t, d.speed) for t, d, _, _, _ in rec if d.speed is not None]
    for (t0, s0), (t1, s1) in zip(sent, sent[1:]):
        assert abs(s1 - s0) <= 0.5 * (t1 - t0) + 0.05 + 1e-9
    assert len(sent) <= 3.0 / ctrl.SPEED_MIN_INTERVAL_S + 5


def test_waiting_pauses_at_hold_and_resumes():
    cfg = Config.from_dict({})
    ctrl = at.TransportController(cfg, PPQ, BPM)
    plant = Plant(pos=0.0)
    hold = 2 * PPQ                         # next unplayed event at beat 2
    s = snap(anchor_tick=0, anchor_live=0.0, hold=hold)
    rec = []
    t = loop(ctrl, plant, s, 3.0, state=st.WAITING, record=rec)
    assert not plant.playing
    assert plant.pos <= hold + cfg.hold_tolerance_beats * PPQ + 1
    assert sum(1 for r in rec if r[1].action == at.ACTION_PAUSE) == 1
    # performer plays the event late: resume
    s2 = snap(anchor_tick=hold, anchor_live=t, hold=3 * PPQ)
    d = ctrl.step(t, s2, plant.pos, plant.playing, st.TRACKING)
    assert d.action == at.ACTION_RESUME


def test_wait_slow_never_pauses():
    ctrl = at.TransportController(Config.from_dict({"wait_behavior": "slow"}), PPQ, BPM)
    plant = Plant()
    rec = []
    loop(ctrl, plant, snap(hold=PPQ), 3.0, state=st.WAITING, record=rec)
    assert plant.playing and all(r[1].action is None for r in rec)
    assert plant.speed <= 0.5 + 1e-9


def test_lost_returns_speed_to_normal_and_keeps_playing():
    ctrl = at.TransportController(Config.from_dict({}), PPQ, BPM)
    plant = Plant()
    loop(ctrl, plant, snap(rate=1.4), 2.0)
    assert plant.speed > 1.2
    loop(ctrl, plant, snap(rate=1.4), 3.0, state=st.LOST, t0=2.0)
    assert abs(plant.speed - 1.0) < 1e-9 and plant.playing
    ctrl2 = at.TransportController(Config.from_dict({"lost_behavior": "pause"}), PPQ, BPM)
    p2 = Plant()
    loop(ctrl2, p2, snap(), 0.5, state=st.LOST)
    assert not p2.playing


def test_no_seek_unless_allowed_and_confirmed():
    far = snap(anchor_tick=40 * PPQ, epoch=2)
    for cfg in ({}, {"allow_seek": True}):
        ctrl = at.TransportController(Config.from_dict(cfg), PPQ, BPM)
        plant = Plant()
        ctrl.step(0.0, snap(epoch=1), plant.pos, True, st.TRACKING)
        rec = []
        loop(ctrl, plant, far, 1.0, state=st.TRACKING, t0=0.02, record=rec)   # epoch change NOT via recovering
        assert all(r[1].action != at.ACTION_SEEK for r in rec)
    ctrl = at.TransportController(Config.from_dict({"allow_seek": True}), PPQ, BPM)
    plant = Plant()
    ctrl.step(0.0, snap(epoch=1), 0.0, True, st.TRACKING)
    ctrl.step(0.02, far, 0.0, True, st.RECOVERING)              # re-acquired far away
    low = snap(anchor_tick=40 * PPQ, epoch=2, conf=0.5)
    assert ctrl.step(0.04, low, 0.0, True, st.TRACKING).action != at.ACTION_SEEK   # not confident yet
    d = ctrl.step(0.06, far, 0.0, True, st.TRACKING)
    assert d.action == at.ACTION_SEEK and abs(d.seek_tick - 40 * PPQ) < PPQ
    assert ctrl.step(0.08, far, d.seek_tick, True, st.TRACKING).action != at.ACTION_SEEK   # only once


def test_tempo_strategies_respect_tempo_control_and_limits():
    cfg = Config.from_dict({"follow_strategy": "tempo_only"})        # tempo_control false
    assert cfg.effective_strategy == "speed_only" and cfg.warnings
    ctrl = at.TransportController(Config.from_dict({"follow_strategy": "tempo_only", "tempo_control": True,
                                                    "tempo_max_bpm": 150}), PPQ, BPM)
    plant = Plant()
    rec = []
    loop(ctrl, plant, snap(rate=1.5), 3.0, record=rec)
    assert plant.speed == 1.0 and abs(plant.tempo - 150) < 1e-6
    sent = [r[1].tempo_bpm for r in rec if r[1].tempo_bpm is not None]
    assert sent and len(sent) <= 3.0 / 0.25 + 2
    hyb = at.TransportController(Config.from_dict({"follow_strategy": "hybrid", "tempo_control": True}), PPQ, BPM)
    p = Plant()
    loop(hyb, p, snap(rate=0.8), 8.0)
    assert abs(p.tempo - 96.0) < 3.0 and abs(p.speed * p.tempo / BPM - 0.8) < 0.05


def test_restore_returns_normal_playback():
    ctrl = at.TransportController(Config.from_dict({"follow_strategy": "hybrid", "tempo_control": True}), PPQ, BPM)
    plant = Plant()
    loop(ctrl, plant, snap(rate=0.7), 4.0)
    d = ctrl.restore()
    plant.apply(d)
    assert plant.speed == 1.0 and plant.tempo == BPM


def test_armed_intro_waits_at_entry():
    ctrl = at.TransportController(Config.from_dict({}), PPQ, BPM)
    plant = Plant(pos=0.0, playing=True)
    s = snap(anchor_tick=4 * PPQ, hold=4 * PPQ, started=False)
    loop(ctrl, plant, s, 5.0, state=st.ARMED)
    assert not plant.playing and plant.pos <= 4 * PPQ + 0.25 * PPQ + 1


def test_idle_and_stopped_do_nothing():
    ctrl = at.TransportController(Config.from_dict({}), PPQ, BPM)
    for state in (st.IDLE, st.STOPPED):
        assert ctrl.step(0.0, snap(), 0.0, True, state).empty()
