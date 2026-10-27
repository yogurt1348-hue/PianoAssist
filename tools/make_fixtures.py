#!/usr/bin/env python3
"""Regenerates the tiny MIDI fixtures in tests/fixtures.

The fixtures are generated here rather than downloaded, so they are original
works and freely redistributable. Run from the repository root:

    python tools/make_fixtures.py
"""
from pathlib import Path
import math
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


def chunk(cid: bytes, data: bytes) -> bytes:
    pad = b"\0" if len(data) % 2 else b""
    return cid + struct.pack("<I", len(data)) + data + pad


def riff_list(kind: bytes, *chunks) -> bytes:
    body = kind + b"".join(chunks)
    return b"LIST" + struct.pack("<I", len(body)) + body


def tiny_soundfont() -> bytes:
    """Smallest useful SoundFont 2: one looping sine sample, one instrument,
    one preset (bank 0, program 0) covering every key."""
    rate, length = 22050, 2205  # 0.1 s; a whole number of 441 Hz cycles
    samples = [int(12000 * math.sin(2 * math.pi * 441 * i / rate)) for i in range(length)]
    smpl = struct.pack(f"<{length}h", *samples) + b"\0\0" * 46  # spec: 46 zero samples after each sample

    def name20(text):
        return text.encode("ascii")[:20].ljust(20, b"\0")

    phdr = (name20("Sine") + struct.pack("<HHHIII", 0, 0, 0, 0, 0, 0)
            + name20("EOP") + struct.pack("<HHHIII", 0, 0, 1, 0, 0, 0))
    pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)
    pmod = b"\0" * 10
    pgen = struct.pack("<HH", 41, 0) + struct.pack("<HH", 0, 0)            # instrument 0, terminal
    inst = name20("Sine") + struct.pack("<H", 0) + name20("EOI") + struct.pack("<H", 1)
    ibag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 2, 0)
    imod = b"\0" * 10
    igen = (struct.pack("<HH", 54, 1)                                     # sampleModes: loop
            + struct.pack("<HH", 53, 0)                                   # sampleID 0 (last)
            + struct.pack("<HH", 0, 0))                                   # terminal
    shdr = (name20("Sine") + struct.pack("<IIIIIBbHH", 0, length, 0, length, rate, 69, 0, 0, 1)
            + name20("EOS") + struct.pack("<IIIIIBbHH", 0, 0, 0, 0, 0, 0, 0, 0, 0))

    info = riff_list(b"INFO", chunk(b"ifil", struct.pack("<HH", 2, 1)), chunk(b"isng", b"EMU8000\0"),
                     chunk(b"INAM", b"Tiny sine\0"))
    sdta = riff_list(b"sdta", chunk(b"smpl", smpl))
    pdta = riff_list(b"pdta", chunk(b"phdr", phdr), chunk(b"pbag", pbag), chunk(b"pmod", pmod),
                     chunk(b"pgen", pgen), chunk(b"inst", inst), chunk(b"ibag", ibag), chunk(b"imod", imod),
                     chunk(b"igen", igen), chunk(b"shdr", shdr))
    body = b"sfbk" + info + sdta + pdta
    return b"RIFF" + struct.pack("<I", len(body)) + body


def tiny_bank_soundfont() -> bytes:
    """The tiny sine three times, told apart by loudness: "Sine" (bank 0,
    program 0, full level), "Very Quiet Bass Tone" (bank 0, program 33, attenuated most;
    a full 20-character name) and
    "Tiny Kit" (bank 128, program 0, attenuated a little), for per-track preset tests."""
    whole = tiny_soundfont()
    # Reuse the sample data and instrument; replace the preset chunks.
    def name20(text):
        return text.encode("ascii")[:20].ljust(20, b"\0")
    phdr = (name20("Sine") + struct.pack("<HHHIII", 0, 0, 0, 0, 0, 0)
            + name20("Very Quiet Bass Tone") + struct.pack("<HHHIII", 33, 0, 1, 0, 0, 0)
            + name20("Tiny Kit") + struct.pack("<HHHIII", 0, 128, 2, 0, 0, 0)
            + name20("EOP") + struct.pack("<HHHIII", 0, 0, 3, 0, 0, 0))
    pbag = struct.pack("<HHHHHHHH", 0, 0, 1, 0, 3, 0, 5, 0)
    pgen = (struct.pack("<HH", 41, 0)
            + struct.pack("<HH", 48, 1000) + struct.pack("<HH", 41, 0)
            + struct.pack("<HH", 48, 300) + struct.pack("<HH", 41, 0)
            + struct.pack("<HH", 0, 0))
    start = whole.index(b"LIST", whole.index(b"sdta"))  # the pdta list
    pdta = whole[start + 12:]
    rest = {}
    pos = 0
    while pos < len(pdta):
        cid = pdta[pos:pos + 4]
        size = struct.unpack("<I", pdta[pos + 4:pos + 8])[0]
        rest[cid] = pdta[pos + 8:pos + 8 + size]
        pos += 8 + size + (size % 2)
    new_pdta = riff_list(b"pdta", chunk(b"phdr", phdr), chunk(b"pbag", pbag), chunk(b"pmod", rest[b"pmod"]),
                         chunk(b"pgen", pgen), chunk(b"inst", rest[b"inst"]), chunk(b"ibag", rest[b"ibag"]),
                         chunk(b"imod", rest[b"imod"]), chunk(b"igen", rest[b"igen"]), chunk(b"shdr", rest[b"shdr"]))
    body = whole[8:start] + new_pdta
    return b"RIFF" + struct.pack("<I", len(body)) + body


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

    # Phase 4 practice fixtures, 120 BPM, one quarter = 0.5 s.
    def seq(steps, channel=0):
        """steps: list of (pitches, quarters); consecutive chords."""
        events = []
        for pitches, quarters in steps:
            for i, p in enumerate(pitches):
                events.append((0, bytes([0x90 | channel, p, 90])))
            for i, p in enumerate(pitches):
                events.append((quarters * PPQ if i == 0 else 0, bytes([0x80 | channel, p, 64])))
        return events

    conductor120 = track([(0, b"\xff\x58\x04\x04\x02\x18\x08"), (0, tempo(500000))])

    # 8. Split hands: two unnamed piano tracks, told apart only by pitch.
    high = track(seq([([72], 1), ([74], 1), ([76], 1), ([77], 1)]))
    low = track(seq([([48, 55], 2), ([43, 50], 2)], channel=1))
    (OUT / "practice_split_hands.mid").write_bytes(smf(1, [conductor120, high, low]))

    # 9. Crossed hands: named tracks; on beat 3 the right hand plays below
    #    the left (D3 under D4), so a pitch split would be wrong.
    rh = track([(0, name("RH"))] + seq([([67], 1), ([65], 1), ([50], 1), ([64], 1)]))
    lh = track([(0, name("LH"))] + seq([([48], 1), ([53], 1), ([62], 1), ([48], 1)], channel=1))
    (OUT / "practice_crossed_hands.mid").write_bytes(smf(1, [conductor120, rh, lh]))

    # 10. Backing-only intro: two bars of bass alone, then one piano part
    #     with both hands for one bar while the bass continues.
    bass_intro = track([(0, name("Bass")), (0, bytes([0xC1, 33]))]
                       + seq([([36], 2), ([43], 2), ([36], 2), ([43], 2), ([36], 4)], channel=1))
    piano_late = track([(8 * PPQ, bytes([0x90, 48, 80])), (0, bytes([0x90, 64, 80])),
                        (2 * PPQ, bytes([0x80, 48, 64])), (0, bytes([0x80, 64, 64])),
                        (0, bytes([0x90, 55, 80])), (0, bytes([0x90, 67, 80])),
                        (2 * PPQ, bytes([0x80, 55, 64])), (0, bytes([0x80, 67, 64]))])
    (OUT / "practice_backing_intro.mid").write_bytes(smf(1, [conductor120, bass_intro, piano_late]))

    # 11. No backing: format 0, one channel, both hands together.
    solo = track([(0, tempo(500000))] + seq([([48, 64], 1), ([52, 67], 1), ([55, 72], 2)]))
    (OUT / "practice_no_backing.mid").write_bytes(smf(0, [solo]))

    # 12. Tiny SoundFont for the .sf2 renderer tests (original, generated).
    (OUT / "tiny_sine.sf2").write_bytes(tiny_soundfont())
    (OUT / "tiny_bank.sf2").write_bytes(tiny_bank_soundfont())

    # 13. Two tracks on one MIDI channel with different programs; the second
    #     selects bank 128 and changes program halfway (at 1 s, 120 BPM).
    #     Track 2: "Bass" ch 1, bank 0 program 33, C2 at 0 s and 1 s.
    #     Track 3: "Lead" ch 1, program 0, E4 at 0 s; bank 128 program 0 at 1 s, E4 again.
    bass = track([(0, name("Bass")), (0, bytes([0xB0, 0, 0])), (0, bytes([0xB0, 32, 0])), (0, bytes([0xC0, 33])),
                  (0, bytes([0x90, 36, 90])), (PPQ, bytes([0x80, 36, 0])),
                  (PPQ, bytes([0x90, 36, 90])), (PPQ, bytes([0x80, 36, 0]))])
    lead = track([(0, name("Lead")), (0, bytes([0xC0, 0])),
                   (0, bytes([0x90, 64, 90])), (PPQ, bytes([0x80, 64, 0])),
                   (PPQ, bytes([0xB0, 0, 1])), (0, bytes([0xB0, 32, 0])), (0, bytes([0xC0, 0])),
                   (0, bytes([0x90, 64, 90])), (PPQ, bytes([0x80, 64, 0]))])
    (OUT / "shared_channel_programs.mid").write_bytes(smf(1, [track([(0, tempo(500000))]), bass, lead]))

    print(f"Wrote fixtures to {OUT}")


if __name__ == "__main__":
    main()
