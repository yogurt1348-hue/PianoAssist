"""
Reference performance: the part the user plays, as a musical map.

Adapted from Cadenza's ``Source/Adaptive/ReferencePerformance`` (C++): notes
are grouped into *events* (chords) when their onsets lie within a chord
window measured from the event's FIRST onset, so a rolled chord is one event
while a fast run never chains into one giant event. Unlike Cadenza, times stay
in FL Studio ticks; the tempo comes from the running project at follow time
(the Piano Roll API does not expose tempo).

JSON schema ``adaptive-follow.reference`` version 1 (see ARCHITECTURE.md).
"""

from . import compat

SCHEMA = "adaptive-follow.reference"
SCHEMA_VERSION = 1
MAX_PPQ = 3840 * 4


class ReferenceFormatError(ValueError):
    """The reference data is unusable (missing, corrupt, wrong schema, empty)."""


def popcount(mask):
    return bin(mask).count("1")


def mask_pitches(mask):
    out = []
    p = 0
    while mask:
        if mask & 1:
            out.append(p)
        mask >>= 1
        p += 1
    return out


class ReferenceNote(object):
    __slots__ = ("tick", "length", "number", "velocity", "color", "group")

    def __init__(self, tick, length, number, velocity=0.8, color=0, group=0):
        self.tick = int(tick)
        self.length = int(length)
        self.number = int(number)
        self.velocity = float(velocity)
        self.color = int(color)
        self.group = int(group)

    def to_dict(self):
        return {"tick": self.tick, "length": self.length, "number": self.number,
                "velocity": round(self.velocity, 4), "color": self.color, "group": self.group}

    def __repr__(self):
        return "ReferenceNote(tick=%d, number=%d, length=%d)" % (self.tick, self.number, self.length)


class ReferenceEvent(object):
    """One musical event: all notes starting within the chord window."""
    __slots__ = ("index", "tick", "last_tick", "mask", "note_count", "max_velocity")

    def __init__(self, index, tick):
        self.index = index
        self.tick = tick          # earliest onset
        self.last_tick = tick     # latest onset merged into the event
        self.mask = 0             # bit p set = pitch p expected
        self.note_count = 0
        self.max_velocity = 0.0

    @property
    def size(self):
        return popcount(self.mask)

    @property
    def pitches(self):
        return mask_pitches(self.mask)

    def has(self, pitch):
        return 0 <= pitch < 128 and (self.mask >> pitch) & 1 == 1

    def to_dict(self):
        return {"tick": self.tick, "pitches": self.pitches}


class Reference(object):
    def __init__(self, name, ppq, notes, chord_window_ticks, meta=None, warnings=None):
        self.name = name or "reference"
        self.ppq = int(ppq)
        self.notes = sorted(notes, key=lambda n: (n.tick, n.number))
        self.chord_window_ticks = max(0, int(chord_window_ticks))
        self.meta = dict(meta or {})
        self.warnings = list(warnings or [])
        self.events = _group(self.notes, self.chord_window_ticks)

    # ------------------------------------------------------------ queries
    @property
    def empty(self):
        return not self.events

    @property
    def event_count(self):
        return len(self.events)

    @property
    def first_tick(self):
        return self.events[0].tick if self.events else 0

    @property
    def end_tick(self):
        end = 0
        for n in self.notes:
            end = max(end, n.tick + n.length)
        return end

    def event_at_or_after(self, tick):
        """Index of the first event with tick >= ``tick`` (event_count if none)."""
        lo, hi = 0, len(self.events)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.events[mid].tick < tick:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def event_at_or_before(self, tick):
        return self.event_at_or_after(tick + 1) - 1

    def regrouped(self, chord_window_ticks):
        """Same notes, different chord window."""
        return Reference(self.name, self.ppq, self.notes, chord_window_ticks, self.meta, self.warnings)

    def pitch_index(self):
        """pitch -> sorted list of event indices containing it (for global search)."""
        index = {}
        for ev in self.events:
            for p in ev.pitches:
                index.setdefault(p, []).append(ev.index)
        return index

    # ------------------------------------------------------------ JSON
    def to_dict(self):
        return {
            "schema": SCHEMA,
            "schema_version": SCHEMA_VERSION,
            "name": self.name,
            "ppq": self.ppq,
            "chord_window_ticks": self.chord_window_ticks,
            "meta": self.meta,
            "stats": {
                "note_count": len(self.notes),
                "event_count": len(self.events),
                "first_tick": self.first_tick,
                "end_tick": self.end_tick,
            },
            "notes": [n.to_dict() for n in self.notes],
        }

    def to_json(self):
        return compat.json_dumps(self.to_dict(), indent=1)

    @classmethod
    def from_dict(cls, data):
        """Validate and load. Bad individual notes are skipped (with warnings);
        a structurally invalid or empty reference raises ReferenceFormatError."""
        if not isinstance(data, dict):
            raise ReferenceFormatError("reference must be a JSON object")
        if data.get("schema") != SCHEMA:
            raise ReferenceFormatError("not an Adaptive Follow reference (schema %r)" % (data.get("schema"),))
        version = data.get("schema_version")
        if isinstance(version, bool) or not isinstance(version, int) or version < 1:
            raise ReferenceFormatError("missing or invalid schema_version")
        if version > SCHEMA_VERSION:
            raise ReferenceFormatError("reference schema_version %d is newer than supported %d; update Adaptive Follow"
                                       % (version, SCHEMA_VERSION))
        ppq = data.get("ppq")
        if isinstance(ppq, bool) or not isinstance(ppq, int) or not 1 <= ppq <= MAX_PPQ:
            raise ReferenceFormatError("invalid ppq %r" % (ppq,))
        window = data.get("chord_window_ticks", max(1, ppq // 16))
        if isinstance(window, bool) or not isinstance(window, (int, float)) or window < 0:
            raise ReferenceFormatError("invalid chord_window_ticks %r" % (window,))
        raw_notes = data.get("notes")
        if not isinstance(raw_notes, list):
            raise ReferenceFormatError("'notes' must be a list")
        notes = []
        warnings = []
        skipped = 0
        for i, raw in enumerate(raw_notes):
            note, problem = _note_from_dict(raw)
            if note is None:
                skipped += 1
                if skipped <= 5:
                    warnings.append("note %d skipped: %s" % (i, problem))
                continue
            notes.append(note)
        if skipped > 5:
            warnings.append("%d invalid notes skipped in total" % skipped)
        if not notes:
            raise ReferenceFormatError("reference contains no valid notes")
        name = data.get("name") if isinstance(data.get("name"), str) else "reference"
        meta = data.get("meta") if isinstance(data.get("meta"), dict) else {}
        return cls(name, ppq, notes, int(window), meta, warnings)

    @classmethod
    def from_json(cls, text):
        try:
            data = compat.json_loads(text)
        except compat.JsonError as exc:
            raise ReferenceFormatError("corrupted reference file (invalid JSON: %s)" % exc)
        return cls.from_dict(data)


def _note_from_dict(raw):
    if not isinstance(raw, dict):
        return None, "not an object"
    def num(key, default=None):
        v = raw.get(key, default)
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v != v:
            return None
        return v
    tick, length, number = num("tick"), num("length", 1), num("number")
    velocity, color, group = num("velocity", 0.8), num("color", 0), num("group", 0)
    if tick is None or number is None:
        return None, "tick and number are required numbers"
    if tick < 0:
        return None, "negative tick"
    if not 0 <= number <= 127:
        return None, "note number out of range"
    if length is None or length < 0:
        return None, "invalid length"
    if velocity is None or not 0.0 <= velocity <= 1.0:
        return None, "velocity must be 0..1"
    if color is None or not 0 <= color <= 15:
        return None, "color must be 0..15"
    return ReferenceNote(int(tick), max(1, int(length)), int(number), float(velocity), int(color),
                         int(group or 0)), None


def _group(notes, window):
    events = []
    current = None
    for n in notes:
        if current is None or n.tick - current.tick > window:
            current = ReferenceEvent(len(events), n.tick)
            events.append(current)
        current.mask |= 1 << n.number
        current.last_tick = n.tick
        current.note_count += 1
        current.max_velocity = max(current.max_velocity, n.velocity)
    return events


# ------------------------------------------------------------------ export

FILTER_ALL = "all"
FILTER_COLOR = "color"


def build_reference_from_notes(raw_notes, ppq, name="reference", filter_mode=FILTER_ALL, color=0,
                               min_velocity=0.0, include_muted=False, chord_window_ticks=None, meta=None):
    """Build a Reference from Piano-Roll-like note objects.

    ``raw_notes`` items need attributes ``number``, ``time``, ``length``,
    ``velocity`` (0..1), and optionally ``color``, ``group``, ``muted`` - the
    documented ``flpianoroll.Note`` properties. Nothing is modified.
    Returns ``(reference_or_None, report)`` where report is a dict with
    counts and human-readable warnings/errors.
    """
    report = {"read": 0, "exported": 0, "skipped_filter": 0, "skipped_invalid": 0,
              "skipped_velocity": 0, "skipped_muted": 0, "duplicates": 0, "warnings": [], "errors": []}
    if isinstance(ppq, bool) or not isinstance(ppq, int) or not 1 <= ppq <= MAX_PPQ:
        report["errors"].append("invalid PPQ %r" % (ppq,))
        return None, report
    if chord_window_ticks is None:
        chord_window_ticks = max(1, ppq // 16)   # a 64th note (~31 ms at 120 BPM)
    if filter_mode not in (FILTER_ALL, FILTER_COLOR):
        report["errors"].append("unknown filter mode %r" % (filter_mode,))
        return None, report
    seen = set()
    notes = []
    for raw in raw_notes:
        report["read"] += 1
        try:
            number = int(getattr(raw, "number"))
            tick = int(getattr(raw, "time"))
            length = int(getattr(raw, "length", 1))
            velocity = float(getattr(raw, "velocity", 0.8))
            ncolor = int(getattr(raw, "color", 0) or 0)
            group = int(getattr(raw, "group", 0) or 0)
            muted = bool(getattr(raw, "muted", False))
        except Exception:
            report["skipped_invalid"] += 1
            continue
        if muted and not include_muted:
            report["skipped_muted"] += 1
            continue
        if filter_mode == FILTER_COLOR and ncolor != color:
            report["skipped_filter"] += 1
            continue
        if tick < 0 or not 0 <= number <= 127:
            report["skipped_invalid"] += 1
            continue
        velocity = min(1.0, max(0.0, velocity))
        if velocity < min_velocity:
            report["skipped_velocity"] += 1
            continue
        key = (tick, number)
        if key in seen:
            report["duplicates"] += 1   # stacked identical notes count once
            continue
        seen.add(key)
        notes.append(ReferenceNote(tick, max(1, length), number, velocity, max(0, min(15, ncolor)), group))
    if report["skipped_invalid"]:
        report["warnings"].append("%d notes with invalid data were skipped" % report["skipped_invalid"])
    if report["duplicates"]:
        report["warnings"].append("%d duplicate notes (same pitch, same tick) merged" % report["duplicates"])
    if not notes:
        report["errors"].append("no notes to export (Piano Roll empty or everything filtered out)")
        return None, report
    m = dict(meta or {})
    m["filter"] = {"mode": filter_mode, "color": color if filter_mode == FILTER_COLOR else None,
                   "min_velocity": min_velocity, "include_muted": bool(include_muted)}
    ref = Reference(name, ppq, notes, chord_window_ticks, m)
    report["exported"] = len(notes)
    report["events"] = ref.event_count
    return ref, report
