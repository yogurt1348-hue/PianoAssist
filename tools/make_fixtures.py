#!/usr/bin/env python3
"""Regenerates the tiny MIDI fixtures in tests/fixtures.

The fixtures are generated here rather than downloaded, so they are original
works and freely redistributable. Run from the repository root:

    python tools/make_fixtures.py
"""
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parent.parent / "tests" / "fixtures"
PPQ = 480


def vlq(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def track(events) -> bytes:
    """events: list of (delta_ticks, raw_bytes). End-of-track is appended."""
    body = b"".join(vlq(d) + e for d, e in events) + vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def smf(fmt: int, tracks) -> bytes:
    return b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), PPQ) + b"".join(tracks)


def tempo(us_per_quarter: int) -> bytes:
    return b"\xff\x51\x03" + us_per_quarter.to_bytes(3, "big")


def name(text: str) -> bytes:
    data = text.encode("utf-8")
    return b"\xff\x03" + vlq(len(data)) + data


def notes(pitches, length, channel=0, velocity=90):
    events = []
    for p in pitches:
        events.append((0, bytes([0x90 | channel, p, velocity])))
        # Zero-velocity note-on as note-off, as many keyboards and files do.
        events.append((length, bytes([0x90 | channel, p, 0])))
    return events


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    scale = [60, 62, 64, 65, 67, 69, 71, 72]

    # 1. Format 0, monophonic C major scale, fixed 120 BPM.
    f0 = smf(0, [track([(0, tempo(500000))] + notes(scale, PPQ))])
    (OUT / "format0_c_major_scale_120bpm.mid").write_bytes(f0)

    # 2. Same content under a non-ASCII file name (UTF-8 path handling).
    (OUT / "гамма_до_мажор.mid").write_bytes(f0)

    # 3. Format 1: conductor track with tempo changes 120 -> 60 -> 90 BPM at
    #    bar starts (4/4), then two piano tracks.
    conductor = track([
        (0, b"\xff\x58\x04\x04\x02\x18\x08"),
        (0, tempo(500000)),
        (4 * PPQ, tempo(1000000)),
        (4 * PPQ, tempo(666667)),
    ])
    right = track([(0, name("Right Hand"))] + notes(scale, PPQ))
    left = track([(0, name("Left Hand"))] + notes([48, 43, 48, 43], 2 * PPQ, channel=1))
    (OUT / "format1_tempo_changes.mid").write_bytes(smf(1, [conductor, right, left]))

    # 4. Truncated: valid header, track chunk cut in half.
    (OUT / "malformed_truncated_track.mid").write_bytes(f0[: len(f0) // 2])

    # 5. Format 2: must be rejected with a clear message.
    (OUT / "unsupported_format2.mid").write_bytes(smf(2, [track(notes([60], PPQ))]))

    # 6. Format 1 band: one unnamed piano part with both hands, drums on
    #    channel 10, a track named "Bass", and unnamed strings (program 49).
    piano = track([(0, name("Piano"))] + [
        (0, bytes([0x90, 48, 80])), (0, bytes([0x90, 64, 80])),
        (PPQ, bytes([0x80, 48, 64])), (0, bytes([0x80, 64, 64])),
    ])
    drums = track([(0, bytes([0x99, 36, 100])), (PPQ // 2, bytes([0x89, 36, 0]))])
    bass = track([(0, name("Bass")), (0, bytes([0xC1, 33])),
                  (0, bytes([0x91, 36, 90])), (PPQ, bytes([0x81, 36, 0]))])
    strings = track([(0, bytes([0xC2, 48])), (0, bytes([0x92, 72, 70])), (2 * PPQ, bytes([0x82, 72, 0]))])
    (OUT / "format1_band_roles.mid").write_bytes(
        smf(1, [track([(0, tempo(500000))]), piano, drums, bass, strings]))

    # 7. Format 0 parser edge cases, in tick order:
    #    0    note-on 60 (status 0x90), then running status note-on 60 again
    #    0    sysex (cancels running status)
    #    240  note-off 60 (first of the two)
    #    480  zero-velocity note-on 60 (second off), sustain down
    #    480  note-on 64 that never gets a note-off
    #    960  sustain up, end of track -> 64 closed at 960 and reported
    edge = track([
        (0, bytes([0x90, 60, 100])),
        (0, bytes([60, 90])),
        (0, b"\xf0\x05\x7e\x7f\x09\x01\xf7"),
        (240, bytes([0x80, 60, 64])),
        (240, bytes([0x90, 60, 0])),
        (0, bytes([0xB0, 64, 127])),
        (0, bytes([0x90, 64, 80])),
        (480, bytes([0xB0, 64, 0])),
    ])
    (OUT / "format0_parser_edge_cases.mid").write_bytes(smf(0, [edge]))

    print(f"Wrote fixtures to {OUT}")


if __name__ == "__main__":
    main()
