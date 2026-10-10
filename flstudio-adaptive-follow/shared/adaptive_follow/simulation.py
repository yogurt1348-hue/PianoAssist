"""
Simulation helpers (no FL Studio): a virtual FL transport, a synthetic
performer and a session runner. Used by the tests and tools/simulate_session.py.

The virtual transport mimics the documented FL Studio behaviour that matters:
``play_pause`` toggles play/pause keeping the position, playback speed is
clamped to 0.25..4.0, the playhead advances at ``tempo * speed``.
"""

import math

from .model import FL_SPEED_MAX, FL_SPEED_MIN
from .reference import Reference, ReferenceNote


class VirtualTransport(object):
    def __init__(self, ppq=96, tempo=120.0, api=37, seek=True, tempo_control=True):
        self.ppq = ppq
        self._tempo = float(tempo)
        self.playing = False
        self.position = 0.0          # absolute ticks
        self.speed = 1.0
        self.api = api
        self._seek = seek
        self._tempo_control = tempo_control
        self.calls = []              # (time, name, args) log
        self.now = 0.0
        self.hints = []

    # ---- port interface ----
    def is_playing(self):
        return self.playing

    def song_tick(self):
        return int(self.position)

    def project_ppq(self):
        return self.ppq

    def tempo(self):
        return self._tempo

    def play_pause(self):
        self.playing = not self.playing
        self.calls.append((self.now, "play_pause", self.playing))

    def set_speed(self, multiplier):
        self.speed = min(FL_SPEED_MAX, max(FL_SPEED_MIN, float(multiplier)))
        self.calls.append((self.now, "set_speed", self.speed))

    def can_set_tempo(self):
        return self._tempo_control

    def set_tempo(self, bpm):
        self._tempo = float(bpm)
        self.calls.append((self.now, "set_tempo", self._tempo))

    def can_seek(self):
        return self._seek

    def set_song_tick(self, ticks):
        self.position = float(ticks)
        self.calls.append((self.now, "set_song_tick", ticks))

    def hint(self, text):
        self.hints.append(text)

    # ---- simulation ----
    def advance(self, dt):
        self.now += dt
        if self.playing:
            self.position += dt * self._tempo / 60.0 * self.ppq * self.speed

    def count(self, name):
        return sum(1 for c in self.calls if c[1] == name)


class Lcg(object):
    """Deterministic pseudo-random numbers (identical on every platform)."""

    def __init__(self, seed=12345):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.state

    def unit(self):
        return (self.next() >> 8) / 16777216.0

    def sym(self):
        return self.unit() * 2.0 - 1.0


def synthetic_reference(count=48, ppq=96, seed=7, chord_every=4, name="synthetic"):
    """A copyright-free, non-repeating test part (random walk, some chords, some 8ths)."""
    rng = Lcg(seed)
    notes = []
    pitch = 64
    tick = 0
    for i in range(count):
        pitch = max(52, min(84, pitch + int(rng.next() % 9) - 4))
        length = ppq if rng.next() % 3 else ppq // 2
        if chord_every and i % chord_every == 0:
            for p in (pitch - 12, pitch - 5, pitch):
                notes.append(ReferenceNote(tick, length - 4, p, 0.8))
        else:
            notes.append(ReferenceNote(tick, length - 4, pitch, 0.8))
        tick += length
    return Reference(name, ppq, notes, max(1, ppq // 16))


class LiveNote(object):
    __slots__ = ("pitch", "t", "true_event", "velocity", "channel")

    def __init__(self, pitch, t, true_event, velocity=90, channel=1):
        self.pitch = pitch
        self.t = t
        self.true_event = true_event
        self.velocity = velocity
        self.channel = channel


def perform(reference, base_bpm=120.0, rate_at=None, start=1.0, jitter=0.0, chord_spread=0.0, seed=3,
            pause_after_event=-1, pause_s=0.0):
    """Render the reference as live notes. ``rate_at(nominal_seconds) -> rate``."""
    rng = Lcg(seed)
    spt = 60.0 / (base_bpm * reference.ppq)
    rate_at = rate_at or (lambda n: 1.0)
    out = []
    live = start
    last_nom = reference.events[0].tick * spt if reference.events else 0.0
    for ev in reference.events:
        nom = ev.tick * spt
        dn = nom - last_nom
        live += dn / max(0.05, rate_at(last_nom + dn / 2.0))
        if pause_after_event >= 0 and ev.index == pause_after_event + 1:
            live += pause_s
        last_nom = nom
        onset = live + rng.sym() * jitter
        pitches = ev.pitches
        if len(pitches) > 1 and rng.next() & 1:
            pitches = list(reversed(pitches))
        for k, p in enumerate(pitches):
            spread = chord_spread * k / (len(pitches) - 1) if len(pitches) > 1 else 0.0
            out.append(LiveNote(p, onset + spread, ev.index))
    out.sort(key=lambda n: n.t)
    return out


class Trace(object):
    def __init__(self):
        self.rows = []           # (t, state, playhead_ref_tick, true_tick, speed, tempo, playing)
        self.transitions = []
        self.notes = []          # NoteResult per live note


def run_session(session, transport, notes, duration, dt=0.02, truth=None, arm_at=0.0, on_step=None):
    """Drive ``session`` with ``notes`` against ``transport`` for ``duration`` seconds."""
    trace = Trace()
    t = 0.0
    i = 0
    armed = False
    while t <= duration:
        transport.now = t
        if not armed and t >= arm_at:
            session.arm()
            armed = True
        while i < len(notes) and notes[i].t <= t:
            n = notes[i]
            res = session.on_note(n.pitch, n.velocity, n.channel, True, n.t)
            if res is not None:
                trace.notes.append((n, res))
            i += 1
        session.tick(t)
        if on_step is not None:
            on_step(t)
        true_tick = truth(t) if truth else None
        trace.rows.append((t, session.sm.state, transport.position, true_tick, transport.speed,
                           transport.tempo(), transport.playing))
        transport.advance(dt)
        t += dt
    trace.transitions = list(session.sm.history)
    return trace


def performer_truth(reference, notes):
    """Function t -> true performer position (reference ticks), interpolated between onsets."""
    onsets = {}
    for n in notes:
        if n.true_event >= 0 and n.true_event not in onsets:
            onsets[n.true_event] = n.t
    pts = sorted((t, reference.events[e].tick) for e, t in onsets.items())

    def truth(t):
        if not pts or t < pts[0][0]:
            return None
        for k in range(len(pts) - 1):
            (t0, x0), (t1, x1) = pts[k], pts[k + 1]
            if t0 <= t < t1:
                return x0 + (x1 - x0) * (t - t0) / max(1e-9, t1 - t0)
        return None
    return truth


def rms(values):
    values = [v for v in values if v is not None]
    if not values:
        return 0.0
    return math.sqrt(sum(v * v for v in values) / len(values))
