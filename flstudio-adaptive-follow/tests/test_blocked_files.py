"""FL Studio sandboxes its Python scripts (observed in FL Studio, MIDI scripting API 40):

* open() outside an allowed folder raises
  SystemError("<class '_io.FileIO'> returned NULL without setting an exception");
* the MIDI device script may read and write ...\\Settings\\Hardware\\AdaptiveFollow\\data;
* the Piano Roll script and the device script run in separate interpreters.

Both scripts must keep working; references reach the device script as files written
by tools/midi_to_reference.py (or by the Piano Roll script where FL allows it)."""

import builtins
import importlib.machinery
import importlib.util
import os
import sys
import types

from conftest import Note
from test_midi_to_reference import BASS, CONDUCTOR, PIANO, run_tool, smf
from test_piano_roll_script import FakeDialog
from test_session import FakeMsg, _fake_fl_modules

from adaptive_follow import state as st
from adaptive_follow import storage as storage_mod
from adaptive_follow.simulation import synthetic_reference
from adaptive_follow.storage import Storage, StorageUnavailableError

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FL_ERROR = SystemError("<class '_io.FileIO'> returned NULL without setting an exception")


def sandbox(monkeypatch, allowed=()):
    """Like FL Studio: open() works only below the ``allowed`` folders."""
    real = builtins.open

    def fake_open(path, *a, **kw):
        if any(str(path).startswith(str(p)) for p in allowed):
            return real(path, *a, **kw)
        raise FL_ERROR
    monkeypatch.setattr(builtins, "open", fake_open)


def _load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, path)
    mod = importlib.util.module_from_spec(importlib.util.spec_from_loader(name, loader))
    loader.exec_module(mod)
    return mod


def _fl(monkeypatch, tmp_path):
    fl = {"playing": False, "pos": 0, "speed": 1.0, "tempo": 120.0}
    for name, mod in _fake_fl_modules(fl).items():
        monkeypatch.setitem(sys.modules, name, mod)
    monkeypatch.setenv("APPDATA", str(tmp_path / "Roaming"))
    monkeypatch.delitem(sys.modules, "adaptive_follow_paths", raising=False)
    return fl


def test_storage_select_skips_blocked_folders(tmp_path, monkeypatch):
    blocked, ok = str(tmp_path / "blocked"), str(tmp_path / "ok")
    sandbox(monkeypatch, allowed=(ok,))
    s = Storage.select([blocked, ok])
    assert s.writable and s.data_dir == ok
    assert "BLOCKED" in s.probes[0][1] and "returned NULL" in s.probes[0][1]
    none = Storage.select([blocked])
    assert not none.writable
    cfg = none.load_config()                       # never raises
    assert cfg.dry_run is True
    assert none.save_config(cfg) is False and none.write_default_config() == ""
    try:
        none.save_reference(synthetic_reference(8))
        assert False, "must raise"
    except StorageUnavailableError as exc:
        assert "does not allow writing" in str(exc)
    assert none.take_command() == ""


def test_device_script_uses_its_own_data_folder_and_converter_files(tmp_path, monkeypatch, capsys):
    fl = _fl(monkeypatch, tmp_path)
    settings = tmp_path / "Image-Line" / "FL Studio" / "Settings"
    hw_data = settings / "Hardware" / "AdaptiveFollow" / "data"
    hw_data.mkdir(parents=True)
    # Outside FL Studio: convert a MIDI export into the device script's data folder.
    mid = tmp_path / "part.mid"
    mid.write_bytes(smf([CONDUCTOR, PIANO, BASS]))
    r = run_tool([str(mid), "--track", "2", "--name", "Verse", "--fl-settings", str(settings)], tmp_path)
    assert r.returncode == 0, r.stderr

    # Inside FL Studio: APPDATA blocked, only the device folder allowed.
    monkeypatch.setattr(storage_mod, "candidate_data_dirs",
                        lambda preferred="", fl_settings="": [str(hw_data), str(tmp_path / "Roaming" / "AdaptiveFollow")])
    sandbox(monkeypatch, allowed=(hw_data,))
    dev = _load(os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py"),
                "device_AdaptiveFollow")
    dev.OnInit()
    out = capsys.readouterr().out
    assert "ready. Reference: Verse" in out and "works in memory" not in out
    session = dev._state["session"]
    assert session.storage.writable and session.storage.data_dir == str(hw_data)
    assert (hw_data / "config.json").exists()       # defaults written where FL allows it

    dev.af_dry_run(False)
    dev.af_arm()
    t = [0.0]
    session.clock = lambda: t[0]
    for ev in session.reference.events:
        t[0] = 1.0 + ev.tick * 60.0 / (120 * 96)
        for p in ev.pitches:
            m = FakeMsg(0x90, p, 100)
            dev.OnMidiMsg(m)
            assert m.handled is False
        dev.OnIdle()
    assert session.sm.state == st.TRACKING and fl["playing"] is True
    assert dev.af_report() is None and (hw_data / "reports" / "report-latest.txt").exists()
    dev.OnDeInit()
    assert fl["speed"] == 1.0


def test_everything_blocked_device_runs_in_memory_and_exporter_explains(tmp_path, monkeypatch, capsys):
    _fl(monkeypatch, tmp_path)
    sandbox(monkeypatch)
    dev = _load(os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py"),
                "device_AdaptiveFollow")
    dev.OnInit()
    out = capsys.readouterr().out
    assert "works in memory" in out and "failed to start" not in out
    session = dev._state["session"]
    assert session is not None and session.reference is None and not session.storage.writable
    dev.af_set("midi_channel", 3)
    assert session.config.midi_channel == 3 and "in memory only" in capsys.readouterr().out
    dev.af_report()                                   # cannot write: must not raise
    dev.af_probe()
    assert "BLOCKED" in capsys.readouterr().out

    notes = [Note(60 + (i % 5), i * 96) for i in range(12)]
    messages = []
    flp = types.ModuleType("flpianoroll")
    flp.score = types.SimpleNamespace(PPQ=96, tsnum=4, tsden=4, noteCount=len(notes), getNote=lambda i: notes[i])
    flp.Utils = types.SimpleNamespace(ShowMessage=messages.append, ProgressMsg=lambda *a: None)
    flp.ScriptDialog = FakeDialog
    monkeypatch.setitem(sys.modules, "flpianoroll", flp)
    exp = _load(os.path.join(ROOT, "piano_roll_script", "Adaptive Follow - Export Reference.pyscript"), "af_exp")
    exp.apply(exp.createDialog())
    assert "midi_to_reference.bat" in messages[-1] and "Export as MIDI file" in messages[-1]
