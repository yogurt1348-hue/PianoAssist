"""
Minimal Standard MIDI File (SMF) reader for the MIDI -> reference converter.

Reads format 0 and 1 files with PPQ (ticks per quarter note) timing: note
on/off pairs per track and channel, running status, meta and sysex events are
skipped. Pure Python, standard library only; no FL Studio dependency.

Used by ``tools/midi_to_reference.py`` because FL Studio's sandbox does not let
the Piano Roll script write a file the MIDI device script can read: export the
part from FL Studio as a MIDI file, convert it outside FL Studio, and the
converter writes the reference into the device script's data folder.
"""


class MidiFormatError(ValueError):
    """Not a readable Standard MIDI File."""


class MidiNote(object):
    """Note with the attribute names of ``flpianoroll.Note`` (for build_reference_from_notes)."""
    __slots__ = ("number", "time", "length", "velocity", "channel", "color", "group", "muted")

    def __init__(self, number, time, length, velocity, channel):
        self.number = number
        self.time = time
        self.length = length
        self.velocity = velocity      # 0..1
        self.channel = channel        # 1..16
        self.color = 0
        self.group = 0
        self.muted = False


class MidiTrack(object):
    def __init__(self, index):
        self.index = index            # 0-based track number in the file
        self.name = ""
        self.notes = []

    @property
    def channels(self):
        return sorted(set(n.channel for n in self.notes))

    def describe(self):
        return "track %d %r: %d notes, channel(s) %s" % (
            self.index + 1, self.name, len(self.notes), ",".join(str(c) for c in self.channels) or "-")


class MidiFile(object):
    def __init__(self, fmt, ppq, tracks):
        self.format = fmt
        self.ppq = ppq
        self.tracks = tracks

    @property
    def note_tracks(self):
        return [t for t in self.tracks if t.notes]


def _vlq(data, i):
    value = 0
    for _ in range(4):
        if i >= len(data):
            raise MidiFormatError("truncated variable-length value")
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, i
    raise MidiFormatError("variable-length value too long")


def _read_track(data, index):
    track = MidiTrack(index)
    i = 0
    tick = 0
    status = 0
    open_notes = {}               # (channel, pitch) -> [(start, velocity), ...] (FIFO)
    while i < len(data):
        delta, i = _vlq(data, i)
        tick += delta
        if i >= len(data):
            break
        b = data[i]
        if b & 0x80:
            status = b
            i += 1
        elif not status:
            raise MidiFormatError("data byte without status in track %d" % (index + 1))
        if status == 0xFF:                                  # meta event
            if i >= len(data):
                break
            mtype = data[i]
            length, i = _vlq(data, i + 1)
            payload = data[i:i + length]
            i += length
            if mtype == 0x03 and not track.name:
                track.name = payload.decode("latin-1", "replace").strip()
            if mtype == 0x2F:
                break
            status = 0                                      # meta cancels running status
            continue
        if status in (0xF0, 0xF7):                          # sysex
            length, i = _vlq(data, i)
            i += length
            status = 0
            continue
        kind = status & 0xF0
        channel = (status & 0x0F) + 1
        size = 1 if kind in (0xC0, 0xD0) else 2
        if i + size > len(data):
            raise MidiFormatError("truncated event in track %d" % (index + 1))
        d1 = data[i]
        d2 = data[i + 1] if size == 2 else 0
        i += size
        if kind == 0x90 and d2 > 0:
            open_notes.setdefault((channel, d1), []).append((tick, d2))
        elif kind == 0x80 or (kind == 0x90 and d2 == 0):
            stack = open_notes.get((channel, d1))
            if stack:
                start, vel = stack.pop(0)
                track.notes.append(MidiNote(d1, start, max(1, tick - start), vel / 127.0, channel))
    for (channel, pitch), stack in open_notes.items():      # notes never released
        for start, vel in stack:
            track.notes.append(MidiNote(pitch, start, max(1, tick - start), vel / 127.0, channel))
    track.notes.sort(key=lambda n: (n.time, n.number))
    return track


def read_midi(data):
    """Parse SMF bytes. Raises MidiFormatError with a clear message."""
    data = bytearray(data)
    if data[:4] != b"MThd" or len(data) < 14:
        raise MidiFormatError("not a Standard MIDI File (no MThd header)")
    hlen = int.from_bytes(bytes(data[4:8]), "big")
    fmt = int.from_bytes(bytes(data[8:10]), "big")
    ntracks = int.from_bytes(bytes(data[10:12]), "big")
    division = int.from_bytes(bytes(data[12:14]), "big")
    if fmt not in (0, 1):
        raise MidiFormatError("MIDI format %d is not supported (use format 0 or 1)" % fmt)
    if division & 0x8000 or division == 0:
        raise MidiFormatError("SMPTE time division is not supported (use ticks per quarter note)")
    i = 8 + hlen
    tracks = []
    while i + 8 <= len(data) and len(tracks) < ntracks:
        cid = bytes(data[i:i + 4])
        clen = int.from_bytes(bytes(data[i + 4:i + 8]), "big")
        body = data[i + 8:i + 8 + clen]
        i += 8 + clen
        if cid == b"MTrk":
            tracks.append(_read_track(body, len(tracks)))
    if not tracks:
        raise MidiFormatError("the MIDI file contains no tracks")
    return MidiFile(fmt, division, tracks)


def read_midi_file(path):
    with open(path, "rb") as f:
        return read_midi(f.read())
