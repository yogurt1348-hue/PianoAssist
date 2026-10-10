import pytest

from adaptive_follow import compat
from adaptive_follow.reference import (FILTER_ALL, FILTER_COLOR, SCHEMA, Reference, ReferenceFormatError,
                                       ReferenceNote, build_reference_from_notes)
from conftest import Note


def test_single_notes_and_chords_are_grouped():
    notes = [Note(60, 0), Note(64, 0), Note(67, 0), Note(62, 96), Note(64, 192)]
    ref, report = build_reference_from_notes(notes, 96, name="t")
    assert report["exported"] == 5
    assert ref.event_count == 3
    assert ref.events[0].pitches == [60, 64, 67]
    assert ref.events[1].pitches == [62]
    assert ref.first_tick == 0 and ref.end_tick == 192 + 96


def test_arpeggio_within_window_is_one_event_but_runs_do_not_chain():
    # rolled chord: 0, 2, 4 ticks; then a run of 16ths (24 ticks apart at 96 PPQ)
    notes = [Note(48, 0), Note(52, 2), Note(55, 4)] + [Note(60 + i, 96 + 24 * i) for i in range(4)]
    ref, _ = build_reference_from_notes(notes, 96, chord_window_ticks=6)
    assert [e.size for e in ref.events] == [3, 1, 1, 1, 1]
    assert ref.events[0].tick == 0 and ref.events[0].last_tick == 4
    wide = ref.regrouped(30)   # wide window measured from the FIRST onset: pairs, not one blob
    assert wide.event_count == 3


def test_same_tick_duplicates_are_merged():
    notes = [Note(60, 0), Note(60, 0), Note(64, 0)]
    ref, report = build_reference_from_notes(notes, 96)
    assert report["duplicates"] == 1
    assert ref.events[0].pitches == [60, 64] and ref.events[0].note_count == 2


def test_color_filter_min_velocity_and_muted():
    notes = [Note(60, 0, color=2), Note(48, 0, color=5), Note(62, 96, color=2, velocity=0.1),
             Note(64, 192, color=2, muted=True), Note(65, 288, color=2)]
    ref, report = build_reference_from_notes(notes, 96, filter_mode=FILTER_COLOR, color=2, min_velocity=0.2)
    assert [e.pitches for e in ref.events] == [[60], [65]]
    assert report["skipped_filter"] == 1 and report["skipped_velocity"] == 1 and report["skipped_muted"] == 1
    assert ref.meta["filter"] == {"mode": "color", "color": 2, "min_velocity": 0.2, "include_muted": False}
    all_ref, _ = build_reference_from_notes(notes, 96, filter_mode=FILTER_ALL, include_muted=True)
    assert len(all_ref.notes) == 5


def test_invalid_and_empty_input():
    notes = [Note(60, -5), Note(200, 0), Note(61, 10)]
    ref, report = build_reference_from_notes(notes, 96)
    assert report["skipped_invalid"] == 2 and len(ref.notes) == 1
    assert any("invalid" in w for w in report["warnings"])
    ref, report = build_reference_from_notes([], 96)
    assert ref is None and "no notes" in report["errors"][0]
    ref, report = build_reference_from_notes([Note(60, 0)], 0)
    assert ref is None and "PPQ" in report["errors"][0]

    class Broken(object):
        number = "x"
        time = 0
    ref, report = build_reference_from_notes([Broken(), Note(60, 0)], 96)
    assert report["skipped_invalid"] == 1 and len(ref.notes) == 1


def test_export_does_not_modify_notes():
    notes = [Note(60, 0, velocity=1.5), Note(62, 96)]
    before = [(n.number, n.time, n.length, n.velocity, n.color) for n in notes]
    build_reference_from_notes(notes, 96)
    assert [(n.number, n.time, n.length, n.velocity, n.color) for n in notes] == before


def test_json_round_trip_and_schema():
    ref, _ = build_reference_from_notes([Note(60, 0), Note(64, 0), Note(62, 96, length=0)], 96, name="Ré part")
    data = compat.json_loads(ref.to_json())
    assert data["schema"] == SCHEMA and data["schema_version"] == 1 and data["ppq"] == 96
    assert data["stats"]["note_count"] == 3 and data["stats"]["event_count"] == 2
    back = Reference.from_json(ref.to_json())
    assert back.name == "Ré part" and back.event_count == 2 and back.notes[2].length >= 1


@pytest.mark.parametrize("mutate, message", [
    (lambda d: d.update(schema="something"), "not an Adaptive Follow reference"),
    (lambda d: d.update(schema_version=99), "newer"),
    (lambda d: d.update(schema_version="1"), "schema_version"),
    (lambda d: d.update(ppq=0), "ppq"),
    (lambda d: d.update(notes="nope"), "'notes' must be a list"),
    (lambda d: d.update(notes=[]), "no valid notes"),
    (lambda d: d.update(chord_window_ticks=-1), "chord_window"),
])
def test_validation_errors(mutate, message):
    ref = Reference("x", 96, [ReferenceNote(0, 96, 60)], 6)
    data = ref.to_dict()
    mutate(data)
    with pytest.raises(ReferenceFormatError) as exc:
        Reference.from_dict(data)
    assert message in str(exc.value)


def test_bad_notes_are_skipped_with_warnings():
    data = Reference("x", 96, [ReferenceNote(0, 96, 60)], 6).to_dict()
    data["notes"] += [{"tick": -1, "number": 60}, {"tick": 0, "number": 300}, "junk",
                      {"tick": 0, "number": 61, "velocity": 2.0}, {"tick": 5, "number": 62, "color": 3}]
    ref = Reference.from_dict(data)
    assert len(ref.notes) == 2 and len(ref.warnings) == 4


def test_corrupted_json():
    with pytest.raises(ReferenceFormatError) as exc:
        Reference.from_json('{"schema": "adaptive-follow.reference", "notes": [')
    assert "corrupted" in str(exc.value)


def test_event_lookup():
    ref, _ = build_reference_from_notes([Note(60, 0), Note(62, 96), Note(64, 192)], 96)
    assert ref.event_at_or_after(1) == 1 and ref.event_at_or_before(100) == 1
    assert ref.event_at_or_after(10 ** 6) == 3 and ref.event_at_or_before(-1) == -1
    assert ref.pitch_index()[62] == [1]


def test_fallback_json_codec_matches_stdlib():
    value = {"a": [1, 2.5, -3, True, False, None, "q\"uote\\né\n"], "b": {}, "c": [], "d": 1e-7}
    text = compat._MiniJson.dumps(value, 2, 0)
    assert compat._MiniJson.loads(text) == value
    import json
    assert compat._MiniJson.loads(json.dumps(value)) == value
    with pytest.raises(compat.JsonError):
        compat._MiniJson.loads('{"a": [1, 2')
    with pytest.raises(compat.JsonError):
        compat._MiniJson.loads('{"a": tru}')
    ref = Reference("x", 96, [ReferenceNote(0, 96, 60), ReferenceNote(0, 96, 64)], 6)
    assert Reference.from_dict(compat._MiniJson.loads(compat._MiniJson.dumps(ref.to_dict(), 1, 0))).event_count == 1
