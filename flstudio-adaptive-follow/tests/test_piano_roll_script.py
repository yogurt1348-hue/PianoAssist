"""Contract test of the Piano Roll exporter with a fake ``flpianoroll`` module."""

import importlib.machinery
import importlib.util
import os
import sys
import types

from conftest import Note

from adaptive_follow.storage import Storage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PYSCRIPT = os.path.join(ROOT, "piano_roll_script", "Adaptive Follow - Export Reference.pyscript")


class FakeDialog(object):
    def __init__(self, title, description):
        self.inputs = {}

    def _add(self, name, value):
        self.inputs[name] = value

    def AddInputText(self, name, value, hint=""):
        self._add(name, value)

    def AddInputCombo(self, name, items, index, hint=""):
        self._add(name, index)

    def AddInputKnobInt(self, name, value, lo, hi, reserved, hint=""):
        assert lo <= value <= hi
        self._add(name, value)

    def AddInputCheckbox(self, name, value, hint=""):
        self._add(name, value)

    def GetInputValue(self, name):
        return self.inputs[name]


def load_exporter(monkeypatch, tmp_path, notes, ppq=96):
    messages = []
    score = types.SimpleNamespace(PPQ=ppq, tsnum=4, tsden=4, noteCount=len(notes), getNote=lambda i: notes[i])
    utils = types.SimpleNamespace(ShowMessage=messages.append, ProgressMsg=lambda m, p, t: None)
    flp = types.ModuleType("flpianoroll")
    flp.score, flp.Utils, flp.ScriptDialog = score, utils, FakeDialog
    monkeypatch.setitem(sys.modules, "flpianoroll", flp)
    paths = types.ModuleType("adaptive_follow_paths")
    paths.LIB_DIR, paths.DATA_DIR = os.path.join(ROOT, "shared"), str(tmp_path / "AF")
    monkeypatch.setitem(sys.modules, "adaptive_follow_paths", paths)
    loader = importlib.machinery.SourceFileLoader("af_export", PYSCRIPT)
    spec = importlib.util.spec_from_loader("af_export", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod, messages, Storage(paths.DATA_DIR)


def test_export_all_notes_without_modifying_them(monkeypatch, tmp_path):
    notes = [Note(60, 0), Note(64, 2), Note(67, 1), Note(62, 96, color=3), Note(65, 192, muted=True)]
    before = [vars(n).copy() for n in notes]
    mod, messages, storage = load_exporter(monkeypatch, tmp_path, notes)
    form = mod.createDialog()
    form.inputs["Reference name"] = "Verse 1"
    mod.apply(form)
    assert [vars(n) for n in notes] == before                  # read-only
    ref, path = storage.load_reference("Verse 1")
    assert ref.ppq == 96 and len(ref.notes) == 4 and ref.event_count == 2      # chord + one note, muted skipped
    assert ref.meta["time_signature"] == [4, 4]
    assert "exported 4 notes (2 events)" in messages[-1] and path in messages[-1]
    mod.apply(form)                                            # same result: no second popup
    assert len(messages) == 1


def test_export_one_color_and_empty_result(monkeypatch, tmp_path):
    notes = [Note(60, 0, color=0), Note(62, 96, color=3), Note(64, 192, color=3)]
    mod, messages, storage = load_exporter(monkeypatch, tmp_path, notes)
    form = mod.createDialog()
    form.inputs["Notes"] = 4                                   # "Only note color 4" == color index 3
    mod.apply(form)
    ref, _ = storage.load_reference()
    assert [n.number for n in ref.notes] == [62, 64]
    assert "Skipped by filters: 1" in messages[-1]
    empty, msgs2, storage2 = load_exporter(monkeypatch, tmp_path / "e", [])
    empty.apply(empty.createDialog())
    assert "nothing exported" in msgs2[-1] and storage2.list_references() == []
