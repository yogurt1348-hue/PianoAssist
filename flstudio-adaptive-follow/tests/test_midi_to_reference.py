"""MIDI file -> reference converter (the path that works despite FL Studio's sandbox)."""

import os
import subprocess
import sys

import pytest

from adaptive_follow.midifile import MidiFormatError, read_midi
from adaptive_follow.storage import Storage, candidate_data_dirs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "midi_to_reference.py")


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def track(events, name=None):
    body = b""
    if name:
        body += vlq(0) + b"\xff\x03" + vlq(len(name)) + name.encode()
    for delta, raw in events:
        body += vlq(delta) + raw
    body += vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + len(body).to_bytes(4, "big") + body


def smf(tracks, ppq=96, fmt=1):
    return b"MThd" + (6).to_bytes(4, "big") + fmt.to_bytes(2, "big") + len(tracks).to_bytes(2, "big") \
        + ppq.to_bytes(2, "big") + b"".join(tracks)


# conductor track + piano (chord C-E-G, then D with running status and vel-0 note off) + bass
PIANO = track([(0, b"\x90\x3c\x64"), (0, b"\x40\x64"), (2, b"\x43\x64"),       # running status, chord
               (94, b"\x80\x3c\x00"), (0, b"\x80\x40\x00"), (0, b"\x80\x43\x00"),
               (0, b"\x90\x3e\x50"), (48, b"\x3e\x00"),                         # vel-0 = note off
               (0, b"\xf0\x03\x01\x02\xf7"),                                     # sysex skipped
               (48, b"\x90\x41\x7f")], name="Piano")                            # never released
BASS = track([(0, b"\x91\x24\x64"), (192, b"\x81\x24\x00")], name="Bass")
CONDUCTOR = track([(0, b"\xff\x51\x03\x07\xa1\x20")], name="Tempo")


def test_parser_pairs_notes_running_status_and_meta():
    midi = read_midi(smf([CONDUCTOR, PIANO, BASS]))
    assert midi.ppq == 96 and len(midi.tracks) == 3 and [t.name for t in midi.note_tracks] == ["Piano", "Bass"]
    piano = midi.tracks[1]
    assert [(n.number, n.time, n.length) for n in piano.notes] == [
        (60, 0, 96), (64, 0, 96), (67, 2, 94), (62, 96, 48), (65, 192, 1)]
    assert abs(piano.notes[0].velocity - 100 / 127.0) < 1e-9 and piano.channels == [1]
    assert midi.tracks[2].channels == [2]


@pytest.mark.parametrize("data, message", [
    (b"RIFF....", "not a Standard MIDI File"),
    (smf([PIANO], fmt=2), "format 2"),
    (b"MThd" + (6).to_bytes(4, "big") + b"\x00\x01\x00\x01\xe7\x28", "SMPTE"),
])
def test_bad_midi_files(data, message):
    with pytest.raises(MidiFormatError) as exc:
        read_midi(data)
    assert message in str(exc.value)


def run_tool(args, tmp_path):
    return subprocess.run([sys.executable, TOOL] + args, capture_output=True, text=True, cwd=str(tmp_path),
                          stdin=subprocess.DEVNULL)


def test_converter_writes_into_the_device_script_data_folder(tmp_path):
    settings = tmp_path / "Image-Line" / "FL Studio" / "Settings"
    (settings / "Hardware" / "AdaptiveFollow").mkdir(parents=True)
    mid = tmp_path / "song.mid"
    mid.write_bytes(smf([CONDUCTOR, PIANO, BASS]))

    r = run_tool([str(mid), "--fl-settings", str(settings)], tmp_path)
    assert r.returncode != 0 and "--track" in (r.stdout + r.stderr)      # two note tracks: must choose

    r = run_tool([str(mid), "--list"], tmp_path)
    assert "track 2 'Piano': 5 notes" in r.stdout

    r = run_tool([str(mid), "--track", "2", "--name", "Verse", "--fl-settings", str(settings)], tmp_path)
    assert r.returncode == 0, r.stderr
    data = settings / "Hardware" / "AdaptiveFollow" / "data"
    assert (data / "references" / "Verse.json").exists() and "af_reload()" in r.stdout

    # the device script finds it through its normal search (first candidate folder)
    store = Storage.select(candidate_data_dirs("", str(settings)))
    assert store.data_dir == str(data)
    ref, path = store.load_reference()
    assert ref.name == "Verse" and ref.ppq == 96 and ref.event_count == 3
    assert ref.events[0].pitches == [60, 64, 67]                         # chord within the window
    assert ref.meta["track_names"] == ["Piano"]


def test_converter_requires_the_installed_device_script(tmp_path):
    mid = tmp_path / "part.mid"
    mid.write_bytes(smf([PIANO]))
    r = run_tool([str(mid), "--fl-settings", str(tmp_path)], tmp_path)
    assert r.returncode != 0 and "not installed" in (r.stdout + r.stderr)
    r = run_tool([str(mid), "--out-dir", str(tmp_path / "out")], tmp_path)
    assert r.returncode == 0 and (tmp_path / "out" / "references" / "part.json").exists()
