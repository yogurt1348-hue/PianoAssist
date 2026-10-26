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
    right = track([(0, b"\xff\x03\x0bRight Hand")] + notes(scale, PPQ))
    left = track([(0, b"\xff\x03\x0aLeft Hand ")] + notes([48, 43, 48, 43], 2 * PPQ, channel=1))
    (OUT / "format1_tempo_changes.mid").write_bytes(smf(1, [conductor, right, left]))

    # 4. Truncated: valid header, track chunk cut in half.
    (OUT / "malformed_truncated_track.mid").write_bytes(f0[: len(f0) // 2])

    # 5. Format 2: must be rejected with a clear message.
    (OUT / "unsupported_format2.mid").write_bytes(smf(2, [track(notes([60], PPQ))]))

    print(f"Wrote fixtures to {OUT}")


if __name__ == "__main__":
    main()
