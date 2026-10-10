import os

import pytest

from adaptive_follow import compat
from adaptive_follow.diagnostics import Diagnostics
from adaptive_follow.model import DEFAULTS, Config
from adaptive_follow.reference import ReferenceFormatError
from adaptive_follow.simulation import synthetic_reference
from adaptive_follow.storage import ReferenceMissingError, Storage, default_data_dir


def test_missing_reference_has_helpful_message(storage):
    with pytest.raises(ReferenceMissingError) as exc:
        storage.load_reference()
    assert "export one from the Piano Roll" in str(exc.value)
    with pytest.raises(ReferenceMissingError):
        storage.load_reference("does not exist")


def test_corrupted_reference_file(storage):
    path = storage.reference_path("broken")
    with open(path, "w") as f:
        f.write('{"schema": "adaptive-follow.reference", "notes": [1, 2')
    with pytest.raises(ReferenceFormatError) as exc:
        storage.load_reference("broken")
    assert "corrupted" in str(exc.value) and "broken.json" in str(exc.value)


def test_save_load_last_and_list(storage):
    a = synthetic_reference(8, name="Part A")
    b = synthetic_reference(8, name="Part B", seed=3)
    pa = storage.save_reference(a)
    os.utime(pa, (1, 1))
    storage.save_reference(b)
    assert storage.list_references() == ["Part B", "Part A"]
    ref, path = storage.load_reference()          # last exported
    assert ref.name == "Part B" and path.endswith("Part B.json")
    ref, _ = storage.load_reference("Part A")
    assert ref.name == "Part A"
    assert not os.path.exists(pa + ".tmp")        # atomic write leaves no temp file


@pytest.mark.parametrize("name, expected", [
    ("My Song: Verse/1", "My Song_ Verse_1"), ("CON", "_CON"), ("  ..  ", "reference"),
    ("lpt1.txt", "_lpt1.txt"), ("ok-name_1", "ok-name_1"),
])
def test_safe_names_follow_windows_rules(name, expected):
    assert Storage.safe_name(name) == expected


def test_config_defaults_created_and_validated(storage):
    cfg = storage.load_config()
    assert cfg.to_dict() == DEFAULTS
    assert os.path.exists(storage.path("config.json"))
    with open(storage.path("config.json"), "w") as f:
        f.write('{"speed_min": 9, "follow_strategy": "warp", "midi_channel": 3, "bogus": 1, "dry_run": "yes"}')
    cfg = storage.load_config()
    assert cfg.midi_channel == 3 and cfg.speed_min == DEFAULTS["speed_min"]
    assert cfg.follow_strategy == "speed_only" and cfg.dry_run is True
    assert len(cfg.warnings) == 4
    with open(storage.path("config.json"), "w") as f:
        f.write("{not json")
    cfg = storage.load_config()
    assert cfg.to_dict() == DEFAULTS and "corrupted" in cfg.warnings[0]


def test_config_cross_checks():
    cfg = Config.from_dict({"tempo_min_bpm": 200, "tempo_max_bpm": 100, "speed_min": 1.5, "speed_max": 0.9})
    assert cfg.tempo_min_bpm == 100 and cfg.speed_max == 1.5 and len(cfg.warnings) == 2
    assert Config.from_dict([1, 2]).warnings
    cfg = Config.from_dict({})
    with pytest.raises(ValueError):
        cfg.speed_max = 10
    with pytest.raises(AttributeError):
        cfg.nonexistent = 1


def test_command_file_is_consumed_once(storage):
    assert storage.take_command() == ""
    storage.write_command("arm")
    assert storage.take_command() == "arm"
    assert storage.take_command() == ""


def test_log_is_size_limited_and_levelled(storage):
    diag = Diagnostics(storage.log_path, "info", max_kb=16)
    for i in range(2000):
        diag.info("line %d %s" % (i, "x" * 40))
        diag.debug("hidden")
    assert compat.file_size(storage.log_path) <= 17 * 1024
    assert os.path.exists(storage.log_path + ".1")
    with open(storage.log_path) as f:
        assert "hidden" not in f.read()
    off = Diagnostics(storage.path("logs", "off.log"), "off")
    off.error("x")
    assert not os.path.exists(storage.path("logs", "off.log"))
    report = diag.report(["header"])
    assert "header" in report and "line 1999" in report


def test_default_data_dir_is_user_scoped(monkeypatch):
    monkeypatch.delenv("ADAPTIVE_FOLLOW_HOME", raising=False)
    monkeypatch.setenv("APPDATA", r"C:\Users\someone\AppData\Roaming")
    assert default_data_dir().endswith("AdaptiveFollow") and "Roaming" in default_data_dir()
    monkeypatch.setenv("ADAPTIVE_FOLLOW_HOME", "/custom")
    assert default_data_dir() == "/custom"


def test_example_config_matches_defaults():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "config", "config.example.json"), encoding="utf-8") as f:
        cfg = Config.from_dict(compat.json_loads(f.read()))
    assert cfg.warnings == [] and cfg.to_dict() == DEFAULTS


def test_config_saved_with_bom_by_notepad_still_loads(storage):
    with open(storage.path("config.json"), "w", encoding="utf-8-sig") as f:
        f.write('{"midi_channel": 5}')
    cfg = storage.load_config()
    assert cfg.midi_channel == 5 and cfg.warnings == []
