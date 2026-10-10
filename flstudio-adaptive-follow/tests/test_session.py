"""End-to-end session tests (virtual FL transport) and the FL device-script contract."""

import ast
import glob
import importlib.util
import os
import sys
import types

from adaptive_follow import state as st
from adaptive_follow.simulation import (LiveNote, perform, performer_truth, rms, run_session,
                                        synthetic_reference)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_dry_run_never_touches_transport(make_session):
    s, tr, ref = make_session({"dry_run": True})
    notes = perform(ref, 120.0, rate_at=lambda n: 0.8)
    run_session(s, tr, notes, notes[-1].t + 1.0)
    assert tr.calls == []                         # no start, no speed, nothing
    assert s.sm.state in (st.TRACKING, st.WAITING)
    with open(s.storage.log_path) as f:
        assert "[dry-run] would start" in f.read()


def test_follows_slower_performer_and_starts_on_first_note(make_session):
    s, tr, ref = make_session({"dry_run": False})
    notes = perform(ref, 120.0, rate_at=lambda n: 0.8, jitter=0.01, start=1.0)
    truth = performer_truth(ref, notes)
    trace = run_session(s, tr, notes, notes[-1].t, truth=truth)
    assert tr.calls[0][1] in ("set_speed", "play_pause")
    assert tr.count("play_pause") == 1             # started once, never paused
    errs = [r[2] - r[3] for r in trace.rows if r[3] is not None and r[0] > 3.0]
    assert rms(errs) * 60.0 / (120 * 96) < 0.02    # < 20 ms rms phase error
    assert abs(tr.speed - 0.8) < 0.05


def test_pause_and_resume_with_waiting(make_session):
    s, tr, ref = make_session({"dry_run": False})
    notes = perform(ref, 120.0, pause_after_event=20, pause_s=4.0)
    trace = run_session(s, tr, notes, notes[-1].t)
    states = [t.new for t in trace.transitions]
    assert st.WAITING in states and states[-1] == st.TRACKING
    toggles = [c for c in tr.calls if c[1] == "play_pause"]
    assert [c[2] for c in toggles] == [True, False, True]      # start, pause at the wait, resume
    hold = ref.events[21].tick
    paused_pos = [r[2] for r in trace.rows if not r[6] and r[0] > notes[0].t + 1]
    grace_ticks = s.config.pause_grace_s * 0.25 * 120 * 96 / 60.0     # crawl at FL's 0.25x minimum
    assert paused_pos and max(paused_pos) <= hold + 0.25 * 96 + grace_ticks + 1


def test_lost_glides_back_to_normal_speed(make_session):
    ref = synthetic_reference(60, seed=21)
    s, tr, _ = make_session({"dry_run": False}, reference=ref)
    clean = perform(ref, 120.0, rate_at=lambda n: 1.3)
    notes = [n for n in clean if n.true_event < 20]
    t = notes[-1].t
    notes += [LiveNote(22 + k % 4, t + 0.25 * (k + 1), -1) for k in range(12)]
    trace = run_session(s, tr, notes, notes[-1].t + 3.0)
    assert st.LOST in [x.new for x in trace.transitions]
    assert abs(tr.speed - 1.0) < 1e-9 and tr.playing


def test_user_stop_and_shutdown_restore_normal_playback(make_session):
    s, tr, ref = make_session({"dry_run": False, "follow_strategy": "hybrid", "tempo_control": True})
    notes = perform(ref, 120.0, rate_at=lambda n: 0.75)
    half = [n for n in notes if n.true_event < 25]

    def stop_midway(t):
        if t > half[-1].t and tr.playing and s.sm.active:
            tr.play_pause()       # simulates FL's own Stop/Pause button by the user
            tr.calls.pop()
    run_session(s, tr, half, half[-1].t + 1.0, on_step=stop_midway)
    assert s.sm.state == st.STOPPED
    assert tr.speed == 1.0 and tr.tempo() == 120.0

    s2, tr2, ref2 = make_session({"dry_run": False, "follow_strategy": "tempo_only", "tempo_control": True})
    notes2 = perform(ref2, 120.0, rate_at=lambda n: 0.75)
    run_session(s2, tr2, notes2[:30], notes2[29].t)
    assert tr2.tempo() != 120.0
    s2.shutdown()
    assert tr2.tempo() == 120.0 and tr2.speed == 1.0 and s2.sm.state == st.STOPPED


def test_channel_and_velocity_filters(make_session):
    s, tr, ref = make_session({"midi_channel": 2, "min_velocity": 40})
    s.arm()
    n = ref.events[0].pitches[0]
    assert s.on_note(n, 90, 1, True, 1.0) is None       # wrong channel
    assert s.on_note(n, 20, 2, True, 1.0) is None       # too soft
    assert s.on_note(n, 90, 2, False, 1.0) is None      # note off is not followed
    assert s.on_note(n, 90, 2, True, 1.0) is not None


def test_arm_without_reference_and_commands(storage):
    from adaptive_follow.session import AdaptiveFollowSession
    from adaptive_follow.simulation import VirtualTransport
    tr = VirtualTransport()
    s = AdaptiveFollowSession(tr, storage, clock=lambda: tr.now)
    assert not s.arm() and s.sm.state == st.IDLE
    storage.save_reference(synthetic_reference(8))
    s.command("reload")
    assert s.reference is not None
    s.command("arm")
    assert s.sm.state == st.ARMED
    s.command("dry_run off")
    assert s.config.dry_run is False
    s.command("disarm")
    assert s.sm.state == st.STOPPED
    path = s.command("report")
    assert path and os.path.exists(path)


def test_offset_playhead_mode(make_session):
    s, tr, ref = make_session({"offset_mode": "playhead", "dry_run": False})
    tr.position = 8 * 96          # playhead placed at bar 3 where the part starts in the playlist
    notes = perform(ref, 120.0)
    trace = run_session(s, tr, notes, notes[-1].t)
    assert s.offset_song_ticks == 8 * 96 - ref.first_tick
    assert abs(tr.speed - 1.0) < 0.05 and st.LOST not in [x.new for x in trace.transitions]


# ---------------------------------------------------------------- FL contract

def _fake_fl_modules(transport_state):
    """Fake versions of the documented FL Studio modules used by the device script."""
    midi = types.ModuleType("midi")
    midi.SONGLENGTH_ABSTICKS = 2
    midi.MIDI_NOTEON, midi.MIDI_NOTEOFF, midi.MIDI_CONTROLCHANGE = 0x90, 0x80, 0xB0
    midi.REC_Tempo, midi.REC_Control, midi.REC_UpdateControl = 0x10005, 0x1, 0x10
    midi.PL_Start = 0
    transport = types.ModuleType("transport")
    transport.isPlaying = lambda: transport_state["playing"]
    transport.start = lambda: transport_state.update(playing=not transport_state["playing"])
    transport.getSongPos = lambda mode=-1: transport_state["pos"]
    transport.setSongPos = lambda pos, mode=-1: transport_state.update(pos=pos)
    transport.setPlaybackSpeed = lambda x: transport_state.update(speed=x)
    mixer = types.ModuleType("mixer")
    mixer.getCurrentTempo = lambda asInt=False: transport_state["tempo"]
    general = types.ModuleType("general")
    general.getVersion = lambda: 37
    general.getRecPPQ = lambda: 96
    general.processRECEvent = lambda e, v, f: transport_state.update(tempo=v / 1000.0)
    ui = types.ModuleType("ui")
    ui.setHintMsg = lambda m: transport_state.setdefault("hints", []).append(m)
    return {"midi": midi, "transport": transport, "mixer": mixer, "general": general, "ui": ui}


class FakeMsg(object):
    def __init__(self, status, data1, data2):
        self.status, self.data1, self.data2 = status, data1, data2
        self.handled = False


def test_device_script_contract(tmp_path, monkeypatch):
    fl = {"playing": False, "pos": 0, "speed": 1.0, "tempo": 120.0}
    for name, mod in _fake_fl_modules(fl).items():
        monkeypatch.setitem(sys.modules, name, mod)
    data = tmp_path / "AdaptiveFollow"
    paths = types.ModuleType("adaptive_follow_paths")
    paths.LIB_DIR = os.path.join(ROOT, "shared")
    paths.DATA_DIR = str(data)
    monkeypatch.setitem(sys.modules, "adaptive_follow_paths", paths)
    from adaptive_follow.storage import Storage
    store = Storage(str(data))
    store.save_reference(synthetic_reference(16))
    store.write_default_config()
    cfg = store.load_config(create_if_missing=False)
    cfg.dry_run = False
    cfg.control_cc_arm_toggle = 102
    store.save_config(cfg)

    path = os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py")
    spec = importlib.util.spec_from_file_location("device_AdaptiveFollow", path)
    dev = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(dev)
    dev.OnInit()
    session = dev._state["session"]
    assert session is not None and session.reference is not None

    cc = FakeMsg(0xB0, 102, 127)
    dev.OnMidiMsg(cc)
    assert cc.handled is True and session.sm.state == st.ARMED      # configured control CC is consumed
    other_cc = FakeMsg(0xB0, 7, 100)
    dev.OnMidiMsg(other_cc)
    assert other_cc.handled is False

    ref = session.reference
    t = [0.0]
    session.clock = lambda: t[0]
    for ev in ref.events[:6]:
        t[0] = 1.0 + ev.tick * 60.0 / (120 * 96)
        for p in ev.pitches:
            msg = FakeMsg(0x90, p, 100)
            dev.OnMidiMsg(msg)
            assert msg.handled is False                               # notes are never eaten
        off = FakeMsg(0x80, ev.pitches[0], 0)
        dev.OnMidiMsg(off)
        assert off.handled is False
        dev.OnIdle()
    assert fl["playing"] is True and session.sm.state == st.TRACKING
    store.write_command("disarm")
    t[0] += 1.0
    dev.OnIdle()
    assert session.sm.state == st.STOPPED and fl["speed"] == 1.0
    dev.OnDeInit()
    for name in ("af_status", "af_arm", "af_disarm", "af_dry_run", "af_reload", "af_report", "af_set", "af_help"):
        assert callable(getattr(dev, name))


def test_fl_side_files_use_python36_syntax_only():
    """FL Studio's embedded interpreter may be old: keep the FL-side code 3.6-compatible."""
    files = glob.glob(os.path.join(ROOT, "shared", "adaptive_follow", "*.py"))
    files += [os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py")]
    files += glob.glob(os.path.join(ROOT, "piano_roll_script", "*.pyscript"))
    assert len(files) >= 12
    for path in files:
        with open(path, encoding="utf-8") as f:
            ast.parse(f.read(), filename=path, feature_version=(3, 6))


def test_no_hardcoded_songs_devices_or_users():
    banned = ["casio", "cdp-s110", "twenty one pilots", "morph", "holding on to you", "gurti", "c:\\users\\"]
    files = glob.glob(os.path.join(ROOT, "shared", "adaptive_follow", "*.py"))
    files += glob.glob(os.path.join(ROOT, "midi_device_script", "*", "*.py"))
    files += glob.glob(os.path.join(ROOT, "piano_roll_script", "*.pyscript"))
    files += [p for p in glob.glob(os.path.join(ROOT, "tools", "*")) if os.path.isfile(p)]
    for path in files:
        with open(path, encoding="utf-8", errors="ignore") as f:
            text = f.read().lower()
        for word in banned:
            assert word not in text, "%s mentions %r" % (path, word)
