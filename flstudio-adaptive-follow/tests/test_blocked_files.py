"""FL Studio may block open() for scripts (SystemError from _io.FileIO).
Both scripts must keep working: memory mode + in-memory hand-off of the reference."""

import builtins
import importlib.machinery
import importlib.util
import os
import sys
import types

from conftest import Note
from test_piano_roll_script import FakeDialog
from test_session import FakeMsg, _fake_fl_modules

from adaptive_follow import handoff
from adaptive_follow import state as st
from adaptive_follow.storage import Storage, StorageUnavailableError

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FL_ERROR = SystemError("<class '_io.FileIO'> returned NULL without setting an exception")


def block_open(monkeypatch, allowed=()):
    real = builtins.open

    def fake_open(path, *a, **kw):
        if any(str(path).startswith(p) for p in allowed):
            return real(path, *a, **kw)
        raise FL_ERROR
    monkeypatch.setattr(builtins, "open", fake_open)


def test_storage_select_skips_blocked_folders(tmp_path, monkeypatch):
    blocked, ok = str(tmp_path / "blocked"), str(tmp_path / "ok")
    block_open(monkeypatch, allowed=(ok,))
    s = Storage.select([blocked, ok])
    assert s.writable and s.data_dir == ok
    assert "BLOCKED" in s.probes[0][1] and "returned NULL" in s.probes[0][1]
    none = Storage.select([blocked])
    assert not none.writable
    cfg = none.load_config()                       # never raises
    assert cfg.dry_run is True
    assert none.save_config(cfg) is False and none.write_default_config() == ""
    try:
        from adaptive_follow.simulation import synthetic_reference
        none.save_reference(synthetic_reference(8))
        assert False, "must raise"
    except StorageUnavailableError as exc:
        assert "does not allow writing" in str(exc)
    assert none.take_command() == ""


def _load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, path)
    mod = importlib.util.module_from_spec(importlib.util.spec_from_loader(name, loader))
    loader.exec_module(mod)
    return mod


def test_scripts_work_in_memory_when_fl_blocks_files(tmp_path, monkeypatch, capsys):
    fl = {"playing": False, "pos": 0, "speed": 1.0, "tempo": 120.0}
    for name, mod in _fake_fl_modules(fl).items():
        monkeypatch.setitem(sys.modules, name, mod)
    monkeypatch.setenv("APPDATA", str(tmp_path / "Roaming"))
    monkeypatch.delitem(sys.modules, "adaptive_follow_paths", raising=False)
    block_open(monkeypatch)

    dev = _load(os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py"),
                "device_AdaptiveFollow")
    dev.OnInit()
    out = capsys.readouterr().out
    assert "works in memory" in out and "failed to start" not in out
    session = dev._state["session"]
    assert session is not None and session.reference is None and not session.storage.writable

    # Piano Roll export in the same interpreter: no file, but handed over in memory
    notes = [Note(60 + (i % 5), i * 96) for i in range(12)]
    messages = []
    flp = types.ModuleType("flpianoroll")
    flp.score = types.SimpleNamespace(PPQ=96, tsnum=4, tsden=4, noteCount=len(notes), getNote=lambda i: notes[i])
    flp.Utils = types.SimpleNamespace(ShowMessage=messages.append, ProgressMsg=lambda *a: None)
    flp.ScriptDialog = FakeDialog
    monkeypatch.setitem(sys.modules, "flpianoroll", flp)
    exp = _load(os.path.join(ROOT, "piano_roll_script", "Adaptive Follow - Export Reference.pyscript"), "af_exp")
    form = exp.createDialog()
    exp.apply(form)
    exp.apply(form)                                   # preview: same export again
    assert "passed to the AdaptiveFollow MIDI script in memory" in messages[-1] and len(messages) == 1
    assert handoff.latest()[0] == 1

    dev.OnIdle()                                      # picks the reference up
    assert session.reference is not None and session.reference.name == "My part"
    assert "in memory" in session.reference_path
    dev.af_set("midi_channel", 3)
    assert session.config.midi_channel == 3
    assert "in memory only" in capsys.readouterr().out

    dev.af_dry_run(False)
    dev.af_arm()
    t = [0.0]
    session.clock = lambda: t[0]
    for ev in session.reference.events[:4]:
        t[0] = 1.0 + ev.tick * 60.0 / (120 * 96)
        for p in ev.pitches:
            dev.OnMidiMsg(FakeMsg(0x93, p, 100))      # channel 4: filtered out (midi_channel = 3)
            m = FakeMsg(0x92, p, 100)                 # channel 3
            dev.OnMidiMsg(m)
            assert m.handled is False
        dev.OnIdle()
    assert session.sm.state == st.TRACKING and fl["playing"] is True
    dev.af_report()                                   # cannot write: must not raise
    dev.af_probe()
    probe = capsys.readouterr().out
    assert "BLOCKED" in probe and "in-memory hand-off: reference 'My part'" in probe
    dev.OnDeInit()
    assert fl["speed"] == 1.0
